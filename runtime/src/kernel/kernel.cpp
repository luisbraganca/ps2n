// EE kernel HLE. Syscall numbers and structure layouts follow ps2sdk's
// kernel.h / syscallnr.h (which mirror Sony's libkernel).
#include "kernel/kernel.h"

#include "clock.h"
#include "hw/gs.h"
#include "hw/hw.h"
#include "internal.h"
#include "iop/iop.h"
#include "log.h"
#include "stubs/format.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

namespace ps2 {

int32_t g_slice = 0;

namespace kernel {

namespace {

using namespace regs;

constexpr int kMaxThreads = 256;
constexpr int kMaxSemas = 256;
constexpr int kMaxAlarms = 64;
constexpr int32_t kSliceLength = 2048;          // back-edges between safepoints
constexpr uint64_t kSliceCycles = 2048 * 8;     // virtual time charged per slice
constexpr uint64_t kSyscallCycles = 200;
constexpr uint32_t kStatusIE = 1u << 0, kStatusEIE = 1u << 16;

enum ThreadStatus : int { THS_RUN = 1, THS_READY = 2, THS_WAIT = 4, THS_SUSPEND = 8, THS_WAITSUSPEND = 12, THS_DORMANT = 16 };
enum WaitType : int { WAIT_NONE = 0, WAIT_SLEEP = 1, WAIT_SEMA = 2 };

struct Thread {
    bool used = false;
    int status = THS_DORMANT;
    uint32_t entry = 0, stack = 0, stackSize = 0, gp = 0, attr = 0, option = 0;
    int initPriority = 0, priority = 0;
    int waitType = WAIT_NONE, waitId = 0;
    int wakeupCount = 0;
    uint64_t readySeq = 0;
    Cpu ctx{};
};

struct Sema {
    bool used = false;
    int count = 0, maxCount = 0, initCount = 0;
    uint32_t attr = 0, option = 0;
    std::deque<int> waiters;
};

struct Handler {
    int id;
    uint32_t func;
    uint32_t arg;
    bool enabled;
};

struct Alarm {
    bool used = false;
    uint32_t func = 0, arg = 0;
    uint16_t time = 0;
    uint64_t event = 0;
};

std::array<Thread, kMaxThreads> g_threads;
std::array<Sema, kMaxSemas> g_semas;
std::array<Alarm, kMaxAlarms> g_alarms;
std::array<std::vector<Handler>, 16> g_intcHandlers;
std::array<std::vector<Handler>, 16> g_dmacHandlers;
int g_nextHandlerId = 1;
int g_current = -1;
uint64_t g_readyCounter = 0;
bool g_needResched = false;
int g_interruptDepth = 0;
bool g_dispatchDisabled = false;
uint32_t g_heapStart = 0, g_heapEnd = 0;
uint32_t g_vsyncFlagAddr = 0, g_vsyncCsrAddr = 0;
uint32_t g_osdConfig = 0;

// TLB entries as the game set them. The memory map always follows the
// default layout (which is what ps2sdk's crt0 and Sony's kernel install), so
// the table only makes Get/ProbeTLBEntry answer consistently.
struct TlbEntry {
    uint32_t pageMask = 0, entryHi = 0, entryLo0 = 0, entryLo1 = 0;
    bool used = false;
};
constexpr int kTlbEntries = 48;
std::array<TlbEntry, kTlbEntries> g_tlb;

const char* syscallName(int32_t n);

void ret(int64_t v) { cpu.gpr[V0].d[0] = static_cast<uint64_t>(v); }
uint32_t arg(int i) {
    static const uint32_t r[8] = {A0, A1, A2, A3, T0, T1, T2, T3};
    return ru32(r[i]);
}
int32_t sarg(int i) { return static_cast<int32_t>(arg(i)); }

bool validThread(int id) { return id > 0 && id < kMaxThreads && g_threads[id].used; }
bool validSema(int id) { return id >= 0 && id < kMaxSemas && g_semas[id].used; }

void makeReady(int id) {
    Thread& t = g_threads[id];
    t.status = THS_READY;
    t.readySeq = ++g_readyCounter;
    if (g_current < 0 || t.priority < g_threads[g_current].priority) g_needResched = true;
}

// Highest-priority READY thread (lower number wins; FIFO among equals).
int pickNext() {
    int best = -1;
    for (int i = 1; i < kMaxThreads; ++i) {
        const Thread& t = g_threads[i];
        if (!t.used || t.status != THS_READY) continue;
        if (best < 0 || t.priority < g_threads[best].priority ||
            (t.priority == g_threads[best].priority && t.readySeq < g_threads[best].readySeq))
            best = i;
    }
    return best;
}

void switchTo(int next) {
    if (next == g_current) return;
    if (g_current >= 0) {
        g_threads[g_current].ctx = cpu;   // cpu.pc is the resume point set by the caller
        if (g_threads[g_current].status == THS_RUN) makeReady(g_current);
    }
    g_current = next;
    if (next >= 0) {
        g_threads[next].status = THS_RUN;
        cpu = g_threads[next].ctx;
    }
    ++g_switchGeneration;
    PS2_TRACE("kernel", "switch to thread %d", next);
}

// Re-evaluates who should run. Only switches outside interrupt context; the
// current thread keeps running if it is still the best candidate.
void reschedule() {
    if (g_interruptDepth > 0) {
        g_needResched = true;
        return;
    }
    g_needResched = false;
    const int next = pickNext();
    if (g_current >= 0 && g_threads[g_current].status == THS_RUN) {
        if (g_dispatchDisabled) return;
        if (next < 0 || g_threads[next].priority >= g_threads[g_current].priority) return;
    }
    switchTo(next);
}

// The current thread stops running (it waits, sleeps, exits...): save its
// context and run someone else, or nobody.
void blockCurrent() {
    if (g_interruptDepth > 0) {
        PS2_ERROR("kernel", "blocking syscall from an interrupt handler (pc 0x%08x); ignoring", cpu.pc);
        return;
    }
    const int cur = g_current;
    g_threads[cur].ctx = cpu;
    g_current = -1;
    ++g_switchGeneration;
    const int next = pickNext();
    if (next >= 0) {
        g_current = next;
        g_threads[next].status = THS_RUN;
        cpu = g_threads[next].ctx;
    } else {
        cpu.pc = 0xFFFFFFFEu;   // idle: unwinds all generated frames; the dispatcher idles
    }
}

void setReturn(int id, int64_t v) { g_threads[id].ctx.gpr[V0].d[0] = static_cast<uint64_t>(v); }

void wakeThread(int id, int64_t result) {
    Thread& t = g_threads[id];
    setReturn(id, result);
    t.waitType = WAIT_NONE;
    t.waitId = 0;
    if (t.status == THS_WAITSUSPEND) t.status = THS_SUSPEND;
    else makeReady(id);
}

void removeFromSemaQueues(int id) {
    for (auto& s : g_semas) s.waiters.erase(std::remove(s.waiters.begin(), s.waiters.end(), id), s.waiters.end());
}

// ---- threads ------------------------------------------------------------------
void initThreadContext(Thread& t, uint32_t argValue) {
    t.ctx = Cpu{};
    t.ctx.cop0[COP0_STATUS] = cpu.cop0[COP0_STATUS] | kStatusEIE | kStatusIE;
    t.ctx.cop0[COP0_PRID] = cpu.cop0[COP0_PRID];
    t.ctx.fcr31 = 0x01000001;
    t.ctx.vf[0].c[3] = 0x3F800000;
    t.ctx.pc = t.entry;
    auto set = [&](uint32_t r, uint32_t v) { t.ctx.gpr[r].d[0] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v))); };
    set(A0, argValue);
    set(SP, (t.stack + t.stackSize - 16) & ~15u);
    set(GP, t.gp);
    set(RA, kTrampThreadExit);
}

void sysCreateThread() {
    const uint32_t p = arg(0);
    int id = -1;
    for (int i = 1; i < kMaxThreads; ++i)
        if (!g_threads[i].used) {
            id = i;
            break;
        }
    if (id < 0) return ret(-1);
    Thread& t = g_threads[id];
    t = Thread{};
    t.used = true;
    t.status = THS_DORMANT;
    t.entry = read32(p + 0x04);
    t.stack = read32(p + 0x08);
    t.stackSize = read32(p + 0x0C);
    t.gp = read32(p + 0x10);
    t.initPriority = t.priority = static_cast<int32_t>(read32(p + 0x14));
    t.attr = read32(p + 0x1C);
    t.option = read32(p + 0x20);
    PS2_DEBUG("kernel", "CreateThread %d entry=0x%08x stack=0x%08x+0x%x prio=%d", id, t.entry, t.stack, t.stackSize, t.priority);
    ret(id);
}

void sysStartThread() {
    const int id = sarg(0);
    if (!validThread(id) || g_threads[id].status != THS_DORMANT) return ret(-1);
    Thread& t = g_threads[id];
    t.priority = t.initPriority;
    initThreadContext(t, arg(1));
    ret(id);
    makeReady(id);
}

void exitCurrent(bool remove) {
    const int id = g_current;
    if (id < 0) return;
    Thread& t = g_threads[id];
    t.status = THS_DORMANT;
    t.wakeupCount = 0;
    blockCurrent();
    if (remove) t.used = false;
}

void sysTerminateThread() {
    const int id = sarg(0);
    if (!validThread(id) || id == g_current) return ret(-1);
    Thread& t = g_threads[id];
    removeFromSemaQueues(id);
    t.status = THS_DORMANT;
    t.waitType = WAIT_NONE;
    ret(id);
}

void sysDeleteThread() {
    const int id = sarg(0);
    if (!validThread(id) || g_threads[id].status != THS_DORMANT) return ret(-1);
    g_threads[id].used = false;
    ret(id);
}

void sysChangeThreadPriority() {
    int id = sarg(0);
    if (id == 0) id = g_current;
    const int prio = sarg(1);
    if (!validThread(id) || prio < 0 || prio > 127) return ret(-1);
    const int old = g_threads[id].priority;
    g_threads[id].priority = prio;
    if (g_threads[id].status == THS_READY) g_threads[id].readySeq = ++g_readyCounter;
    g_needResched = true;
    ret(old);
}

void sysRotateThreadReadyQueue() {
    const int prio = sarg(0);
    if (g_current >= 0 && g_threads[g_current].priority == prio) {
        // Yield to the other threads at this priority.
        Thread& cur = g_threads[g_current];
        cur.readySeq = ++g_readyCounter;
        const int next = pickNext();
        if (next >= 0 && g_threads[next].priority == prio && g_interruptDepth == 0) {
            ret(prio);
            switchTo(next);
            return;
        }
    } else {
        for (int i = 1; i < kMaxThreads; ++i)
            if (g_threads[i].used && g_threads[i].status == THS_READY && g_threads[i].priority == prio) {
                g_threads[i].readySeq = ++g_readyCounter;
                break;
            }
    }
    ret(prio);
}

void sysReferThreadStatus() {
    int id = sarg(0);
    if (id == 0) id = g_current;
    if (!validThread(id)) return ret(-1);
    const Thread& t = g_threads[id];
    const uint32_t p = arg(1);
    if (p) {
        write32(p + 0x00, static_cast<uint32_t>(t.status));
        write32(p + 0x04, t.entry);
        write32(p + 0x08, t.stack);
        write32(p + 0x0C, t.stackSize);
        write32(p + 0x10, t.gp);
        write32(p + 0x14, static_cast<uint32_t>(t.initPriority));
        write32(p + 0x18, static_cast<uint32_t>(t.priority));
        write32(p + 0x1C, t.attr);
        write32(p + 0x20, t.option);
        write32(p + 0x24, static_cast<uint32_t>(t.waitType));
        write32(p + 0x28, static_cast<uint32_t>(t.waitId));
        write32(p + 0x2C, static_cast<uint32_t>(t.wakeupCount));
    }
    ret(t.status);
}

void sysSleepThread() {
    Thread& t = g_threads[g_current];
    if (t.wakeupCount > 0) {
        --t.wakeupCount;
        return ret(g_current);
    }
    ret(g_current);
    t.status = THS_WAIT;
    t.waitType = WAIT_SLEEP;
    blockCurrent();
}

void sysWakeupThread() {
    const int id = sarg(0);
    if (!validThread(id) || id == g_current) return ret(-1);
    Thread& t = g_threads[id];
    if ((t.status & THS_WAIT) && t.waitType == WAIT_SLEEP) wakeThread(id, id);
    else ++t.wakeupCount;
    ret(id);
}

void sysCancelWakeupThread() {
    int id = sarg(0);
    if (id == 0) id = g_current;
    if (!validThread(id)) return ret(-1);
    const int n = g_threads[id].wakeupCount;
    g_threads[id].wakeupCount = 0;
    ret(n);
}

void sysSuspendThread() {
    const int id = sarg(0);
    if (!validThread(id) || id == g_current) return ret(-1);
    Thread& t = g_threads[id];
    if (t.status == THS_READY) t.status = THS_SUSPEND;
    else if (t.status == THS_WAIT) t.status = THS_WAITSUSPEND;
    ret(id);
}

void sysResumeThread() {
    const int id = sarg(0);
    if (!validThread(id)) return ret(-1);
    Thread& t = g_threads[id];
    if (t.status == THS_SUSPEND) makeReady(id);
    else if (t.status == THS_WAITSUSPEND) t.status = THS_WAIT;
    ret(id);
}

void sysReleaseWaitThread() {
    const int id = sarg(0);
    if (!validThread(id) || !(g_threads[id].status & THS_WAIT)) return ret(-1);
    removeFromSemaQueues(id);
    wakeThread(id, -418);   // KE_RELEASE_WAIT
    ret(id);
}

// SetupThread(gp, stack, stack_size, args, root): crt0 calls this first.
void sysSetupThread() {
    const uint32_t gp = arg(0);
    uint32_t stack = arg(1);
    const uint32_t size = arg(2);
    const uint32_t argsAddr = arg(3);
    if (stack == 0xFFFFFFFFu) stack = kRamSize - size;
    Thread& t = g_threads[g_current];
    t.stack = stack;
    t.stackSize = size;
    t.gp = gp;

    if (argsAddr) {   // struct sargs { int32 argc; char *argv[16]; char payload[256]; }
        std::vector<std::string> argv;
        argv.push_back("host:" + g_opt.elfPath.filename().string());
        for (const auto& a : g_opt.guestArgs) argv.push_back(a);
        if (argv.size() > 16) argv.resize(16);
        uint32_t payload = argsAddr + 4 + 16 * 4, used = 0;
        uint32_t argc = 0;
        for (const auto& a : argv) {
            if (used + a.size() + 1 > 256) break;
            write32(argsAddr + 4 + 4 * argc, payload + used);
            writeBytes(payload + used, a.c_str(), static_cast<uint32_t>(a.size() + 1));
            used += static_cast<uint32_t>(a.size() + 1);
            ++argc;
        }
        write32(argsAddr, argc);
    }
    ret((stack + size - 16) & ~15u);
}

void sysSetupHeap() {
    g_heapStart = arg(0);
    const int32_t size = sarg(1);
    const Thread& t = g_threads[g_current];
    g_heapEnd = size < 0 ? (t.stack ? t.stack : kRamSize) : g_heapStart + static_cast<uint32_t>(size);
    ret(g_heapStart);
}

// ---- semaphores -----------------------------------------------------------------
void sysCreateSema() {
    const uint32_t p = arg(0);
    for (int i = 0; i < kMaxSemas; ++i) {
        if (g_semas[i].used) continue;
        Sema& s = g_semas[i];
        s = Sema{};
        s.used = true;
        s.count = static_cast<int32_t>(read32(p + 0x00));
        s.maxCount = static_cast<int32_t>(read32(p + 0x04));
        s.initCount = static_cast<int32_t>(read32(p + 0x08));
        s.attr = read32(p + 0x10);
        s.option = read32(p + 0x14);
        s.count = s.initCount;
        return ret(i);
    }
    ret(-1);
}

void sysDeleteSema() {
    const int id = sarg(0);
    if (!validSema(id)) return ret(-1);
    for (int w : g_semas[id].waiters) wakeThread(w, -425);   // KE_WAIT_DELETE
    g_semas[id] = Sema{};
    ret(id);
}

int signalSema(int id) {
    if (!validSema(id)) return -1;
    Sema& s = g_semas[id];
    if (!s.waiters.empty()) {
        const int w = s.waiters.front();
        s.waiters.pop_front();
        wakeThread(w, id);
    } else {
        if (s.maxCount > 0 && s.count >= s.maxCount) return -420;   // KE_SEMA_OVF
        ++s.count;
    }
    return id;
}

void sysSignalSema() { ret(signalSema(sarg(0))); }

void sysWaitSema() {
    const int id = sarg(0);
    if (!validSema(id)) return ret(-1);
    Sema& s = g_semas[id];
    if (s.count > 0) {
        --s.count;
        return ret(id);
    }
    if (g_interruptDepth > 0 || g_current < 0) return ret(-1);
    Thread& t = g_threads[g_current];
    t.status = THS_WAIT;
    t.waitType = WAIT_SEMA;
    t.waitId = id;
    s.waiters.push_back(g_current);
    ret(id);
    blockCurrent();
}

void sysPollSema() {
    const int id = sarg(0);
    if (!validSema(id)) return ret(-1);
    if (g_semas[id].count > 0) {
        --g_semas[id].count;
        return ret(id);
    }
    ret(-419);   // KE_SEMA_ZERO
}

void sysReferSemaStatus() {
    const int id = sarg(0);
    if (!validSema(id)) return ret(-1);
    const Sema& s = g_semas[id];
    const uint32_t p = arg(1);
    write32(p + 0x00, static_cast<uint32_t>(s.count));
    write32(p + 0x04, static_cast<uint32_t>(s.maxCount));
    write32(p + 0x08, static_cast<uint32_t>(s.initCount));
    write32(p + 0x0C, static_cast<uint32_t>(s.waiters.size()));
    write32(p + 0x10, s.attr);
    write32(p + 0x14, s.option);
    ret(id);
}

// ---- interrupt handlers ------------------------------------------------------------
int addHandler(std::vector<Handler>& list, uint32_t func, int32_t next, uint32_t argValue) {
    const Handler h{g_nextHandlerId++, func, argValue, true};
    if (next == 0) list.insert(list.begin(), h);
    else list.push_back(h);
    return h.id;
}

bool removeHandler(std::vector<Handler>& list, int id) {
    auto it = std::find_if(list.begin(), list.end(), [id](const Handler& h) { return h.id == id; });
    if (it == list.end()) return false;
    list.erase(it);
    return true;
}

void setHandlerEnabled(std::vector<Handler>& list, int id, bool on) {
    for (auto& h : list)
        if (h.id == id) h.enabled = on;
}

void runHandlers(const std::vector<Handler>& list, uint32_t cause) {
    const std::vector<Handler> copy = list;   // handlers may add/remove handlers
    for (const Handler& h : copy) {
        if (!h.enabled) continue;
        const uint64_t args[3] = {cause, h.arg, cpu.pc};
        const auto r = static_cast<int32_t>(callGuestOnStack(h.func, args, 3, kInterruptStackTop));
        if (g_exitRequested || r < 0) break;
    }
}

bool interruptsEnabled() {
    if (g_interruptDepth > 0) return false;
    if (g_current < 0) return true;   // the idle thread always takes interrupts
    const uint32_t st = cpu.cop0[COP0_STATUS];
    return (st & kStatusEIE) && (st & kStatusIE);
}

// ---- alarms -------------------------------------------------------------------------
void alarmFired(uint64_t id) {
    Alarm& a = g_alarms[id];
    if (!a.used) return;
    if (!interruptsEnabled()) {   // delivered through the timer interrupt: wait until it can be taken
        a.event = scheduleEvent(nowCycles() + 2000, alarmFired, id);
        return;
    }
    a.used = false;
    ++g_interruptDepth;
    const uint64_t args[3] = {id, a.time, a.arg};
    callGuestOnStack(a.func, args, 3, kInterruptStackTop);
    --g_interruptDepth;
}

void sysSetAlarm() {
    for (int i = 0; i < kMaxAlarms; ++i) {
        if (g_alarms[i].used) continue;
        Alarm& a = g_alarms[i];
        a.used = true;
        a.time = static_cast<uint16_t>(arg(0));
        a.func = arg(1);
        a.arg = arg(2);
        a.event = scheduleEvent(nowCycles() + static_cast<uint64_t>(a.time) * hw::hsyncCycles(), alarmFired, static_cast<uint64_t>(i));
        return ret(i);
    }
    ret(-1);
}

void sysReleaseAlarm() {
    const int id = sarg(0);
    if (id < 0 || id >= kMaxAlarms || !g_alarms[id].used) return ret(-1);
    cancelEvent(g_alarms[id].event);
    g_alarms[id].used = false;
    ret(id);
}

// ---- TLB ------------------------------------------------------------------------------
int setTlb(int index, uint32_t pageMask, uint32_t entryHi, uint32_t entryLo0, uint32_t entryLo1) {
    if (index < 0 || index >= kTlbEntries) return -1;
    g_tlb[static_cast<size_t>(index)] = {pageMask, entryHi, entryLo0, entryLo1, true};
    PS2_DEBUG("kernel", "TLB[%d] = mask %08x hi %08x lo0 %08x lo1 %08x", index, pageMask, entryHi, entryLo0, entryLo1);
    return index;
}

void sysSetTlbEntry() { ret(setTlb(sarg(0), arg(1), arg(2), arg(3), arg(4))); }

void sysPutTlbEntry() {
    for (int i = 0; i < kTlbEntries; ++i)
        if (!g_tlb[static_cast<size_t>(i)].used) return ret(setTlb(i, arg(0), arg(1), arg(2), arg(3)));
    ret(-1);
}

void sysGetTlbEntry() {
    const int index = sarg(0);
    if (index < 0 || index >= kTlbEntries) return ret(-1);
    const TlbEntry& e = g_tlb[static_cast<size_t>(index)];
    if (arg(1)) write32(arg(1), e.pageMask);
    if (arg(2)) write32(arg(2), e.entryHi);
    if (arg(3)) write32(arg(3), e.entryLo0);
    if (arg(4)) write32(arg(4), e.entryLo1);
    ret(index);
}

void sysProbeTlbEntry() {
    const uint32_t vpn2 = arg(0) & ~0x1FFFu;
    for (int i = 0; i < kTlbEntries; ++i) {
        const TlbEntry& e = g_tlb[static_cast<size_t>(i)];
        const uint32_t mask = ~(e.pageMask | 0x1FFFu);
        if (!e.used || (e.entryHi & mask) != (vpn2 & mask)) continue;
        if (arg(1)) write32(arg(1), e.pageMask);
        if (arg(2)) write32(arg(2), e.entryLo0);
        if (arg(3)) write32(arg(3), e.entryLo1);
        return ret(i);
    }
    ret(-1);
}

// ---- misc -----------------------------------------------------------------------------
void sysUnimplemented(int32_t n) {
    char key[96];
    std::snprintf(key, sizeof key, "syscall %d (%s)", n, syscallName(n));
    // cpu.pc is still syscall + 4 here; $ra usually identifies the game code
    // that called the libkernel wrapper.
    PS2_UNIMPL("kernel", key, "unimplemented %s at 0x%08x ($ra 0x%08x); returning 0", key, cpu.pc - 4, ru32(RA));
    ret(0);
}

void dispatchSyscall(int32_t n) {
    switch (n) {
    case 0x01: ret(0); break;                                       // ResetEE
    case 0x02: hw::setVideoMode(arg(0) & 1, sarg(1), arg(2) & 1); ret(0); break;   // SetGsCrt
    case 0x04:                                                      // Exit
        PS2_INFO("kernel", "Exit(%d)", sarg(0));
        requestExit(sarg(0));
        break;
    case 0x06:                                                      // LoadExecPS2
        PS2_WARN("kernel", "LoadExecPS2(\"%s\") is not supported: recompile the other ELF and run it separately",
                 readCString(arg(0)).c_str());
        requestExit(0);
        break;
    case 0x07: sysUnimplemented(n); break;                          // ExecPS2
    case 0x18: case 0xFC: case -0x1E: case -0xFD: sysSetAlarm(); break;
    case 0x19: case 0xFE: case -0x1F: case -0xFF: sysReleaseAlarm(); break;
    case 0x20: sysCreateThread(); break;
    case 0x21: sysDeleteThread(); break;
    case 0x22: sysStartThread(); break;
    case 0x23: exitCurrent(false); break;                           // ExitThread
    case 0x24: exitCurrent(true); break;                            // ExitDeleteThread
    case 0x25: case -0x26: sysTerminateThread(); break;
    case 0x27: g_dispatchDisabled = true; ret(0); break;            // DisableDispatchThread
    case 0x28: g_dispatchDisabled = false; g_needResched = true; ret(0); break;
    case 0x29: case -0x2A: sysChangeThreadPriority(); break;
    case 0x2B: case -0x2C: sysRotateThreadReadyQueue(); break;
    case 0x2D: case -0x2E: sysReleaseWaitThread(); break;
    case 0x2F: case -0x2F: ret(g_current); break;                   // GetThreadId
    case 0x30: case -0x31: sysReferThreadStatus(); break;
    case 0x32: sysSleepThread(); break;
    case 0x33: case -0x34: sysWakeupThread(); break;
    case 0x35: case -0x36: sysCancelWakeupThread(); break;
    case 0x37: case -0x38: sysSuspendThread(); break;
    case 0x39: case -0x3A: sysResumeThread(); break;
    case 0x3C: sysSetupThread(); break;
    case 0x3D: sysSetupHeap(); break;
    case 0x3E: ret(g_heapEnd); break;                               // EndOfHeap
    case 0x40: sysCreateSema(); break;
    case 0x41: case -0x49: sysDeleteSema(); break;
    case 0x42: case -0x43: sysSignalSema(); break;
    case 0x44: sysWaitSema(); break;
    case 0x45: case -0x46: sysPollSema(); break;
    case 0x47: case -0x48: sysReferSemaStatus(); break;
    case 0x4A: g_osdConfig = read32(arg(0)); ret(0); break;          // SetOsdConfigParam
    case 0x4B: write32(arg(0), g_osdConfig); ret(0); break;         // GetOsdConfigParam
    case 0x4C: case 0x4D: case 0x4E: case 0x4F: ret(0); break;      // Get/SetGsH/VParam
    case 0x55: case -0x55: sysPutTlbEntry(); break;
    case 0x56: case -0x56: sysSetTlbEntry(); break;
    case 0x57: case -0x57: sysGetTlbEntry(); break;
    case 0x58: case -0x58: sysProbeTlbEntry(); break;
    case 0x59: ret(0); break;                                       // ExpandScratchPad
    case 0x5A: {                                                    // Copy (kernel patching): harmless here
        const uint32_t dst = arg(0), src = arg(1), size = arg(2);
        for (uint32_t i = 0; i < size; ++i) write8(dst + i, read8(src + i));
        ret(size);
        break;
    }
    case 0x5B: ret(static_cast<int32_t>(kKernelScratch)); break;    // GetEntryAddress
    case 0x60: ret(0); break;                                       // KSeg0
    case 0x61: case 0x62: ret(0); break;                            // EnableCache / DisableCache
    case 0x63: case -0x67: ret(static_cast<int32_t>(cop0Read(arg(0)))); break;   // GetCop0
    case 0x64: case -0x68: ret(0); break;                           // FlushCache
    case 0x66: case -0x6A: ret(0); break;                           // CpuConfig
    case 0x6B: ret(0); break;                                       // SifStopDma
    case 0x6C: case 0x6D: sysUnimplemented(n); break;               // SetCPUTimerHandler / SetCPUTimer
    case 0x6E: ret(0); break;                                       // SetOsdConfigParam2
    case 0x6F: fillBytes(arg(0), 0, arg(1)); ret(0); break;         // GetOsdConfigParam2
    case 0x70: case -0x70: ret(static_cast<int64_t>(gs::imr())); break;          // GsGetIMR
    case 0x71: case -0x71: ret(static_cast<int64_t>(gs::putImr(arg(0)))); break; // GsPutIMR
    case 0x72: ret(0); break;                                       // SetPgifHandler
    case 0x73: g_vsyncFlagAddr = arg(0); g_vsyncCsrAddr = arg(1); ret(0); break; // SetVSyncFlag
    case 0x74: PS2_DEBUG("kernel", "SetSyscall(0x%x) ignored (kernel patch)", arg(0)); ret(0); break;
    case 0x75: hostPrint(formatGuestString(arg(0), 1)); ret(0); break;   // _print
    case 0x76: case -0x76: ret(iop::sifDmaStat(arg(0))); break;     // SifDmaStat
    case 0x77: case -0x77: ret(iop::sifSetDma(arg(0), sarg(1))); break;   // SifSetDma
    case 0x78: case -0x78: iop::sifSetDChain(); ret(0); break;      // SifSetDChain
    case 0x79: ret(iop::sifSetReg(arg(0), arg(1))); break;          // SifSetReg
    case 0x7A: ret(iop::sifGetReg(arg(0))); break;                  // SifGetReg
    case 0x7B: PS2_INFO("kernel", "ExecOSD: exiting"); requestExit(0); break;
    case 0x7C: ret(0); break;                                       // Deci2Call
    case 0x7D: ret(0); break;                                       // PSMode
    case 0x7E: ret(0); break;                                       // MachineType
    case 0x7F: ret(kRamSize); break;                                // GetMemorySize
    case 0x80: ret(0); break;                                       // _GetGsDxDyOffset
    case 0x82: ret(0); break;                                       // _InitTLB
    case 0x85: case 0x86: ret(0); break;                            // Set/GetMemoryMode
    default: sysUnimplemented(n); break;
    }
}

// Interrupt-handler management syscalls (kept separate for readability).
bool handleInterruptSyscall(int32_t n) {
    switch (n) {
    case 0x10: {   // AddIntcHandler(cause, handler, next, arg)
        const uint32_t cause = arg(0);
        ret(cause < 16 ? addHandler(g_intcHandlers[cause], arg(1), sarg(2), arg(3)) : -1);
        return true;
    }
    case 0x11:     // RemoveIntcHandler(cause, id)
        ret(arg(0) < 16 && removeHandler(g_intcHandlers[arg(0)], sarg(1)) ? sarg(1) : -1);
        return true;
    case 0x12: {   // AddDmacHandler(channel, handler, next, arg)
        const uint32_t ch = arg(0);
        ret(ch < 16 ? addHandler(g_dmacHandlers[ch], arg(1), sarg(2), arg(3)) : -1);
        return true;
    }
    case 0x13:
        ret(arg(0) < 16 && removeHandler(g_dmacHandlers[arg(0)], sarg(1)) ? sarg(1) : -1);
        return true;
    case 0x14: case -0x1A: hw::setIntcMask(sarg(0), true); ret(1); return true;    // EnableIntc
    case 0x15: case -0x1B: hw::setIntcMask(sarg(0), false); ret(1); return true;   // DisableIntc
    case 0x16: case -0x1C: hw::setDmacMask(sarg(0), true); ret(1); return true;    // EnableDmac
    case 0x17: case -0x1D: hw::setDmacMask(sarg(0), false); ret(1); return true;   // DisableDmac
    case 0x5C: case -0x5C: if (arg(0) < 16) setHandlerEnabled(g_intcHandlers[arg(0)], sarg(1), true); ret(0); return true;
    case 0x5D: case -0x5D: if (arg(0) < 16) setHandlerEnabled(g_intcHandlers[arg(0)], sarg(1), false); ret(0); return true;
    case 0x5E: case -0x5E: if (arg(0) < 16) setHandlerEnabled(g_dmacHandlers[arg(0)], sarg(1), true); ret(0); return true;
    case 0x5F: case -0x5F: if (arg(0) < 16) setHandlerEnabled(g_dmacHandlers[arg(0)], sarg(1), false); ret(0); return true;
    default: return false;
    }
}

const char* syscallName(int32_t n) {
    switch (n < 0 ? -n : n) {
    case 0x01: return "ResetEE";
    case 0x02: return "SetGsCrt";
    case 0x04: return "Exit";
    case 0x06: return "LoadExecPS2";
    case 0x07: return "ExecPS2";
    case 0x10: return "AddIntcHandler";
    case 0x11: return "RemoveIntcHandler";
    case 0x12: return "AddDmacHandler";
    case 0x13: return "RemoveDmacHandler";
    case 0x14: return "_EnableIntc";
    case 0x15: return "_DisableIntc";
    case 0x16: return "_EnableDmac";
    case 0x17: return "_DisableDmac";
    case 0x18: case 0xFC: return "SetAlarm";
    case 0x19: case 0xFE: return "ReleaseAlarm";
    case 0x1A: return "_iEnableIntc";
    case 0x1B: return "_iDisableIntc";
    case 0x1C: return "_iEnableDmac";
    case 0x1D: return "_iDisableDmac";
    case 0x1E: case 0xFD: return "iSetAlarm";
    case 0x1F: case 0xFF: return "iReleaseAlarm";
    case 0x20: return "CreateThread";
    case 0x21: return "DeleteThread";
    case 0x22: return "StartThread";
    case 0x23: return "ExitThread";
    case 0x24: return "ExitDeleteThread";
    case 0x25: return "TerminateThread";
    case 0x26: return "iTerminateThread";
    case 0x27: return "DisableDispatchThread";
    case 0x28: return "EnableDispatchThread";
    case 0x29: return "ChangeThreadPriority";
    case 0x2A: return "iChangeThreadPriority";
    case 0x2B: return "RotateThreadReadyQueue";
    case 0x2C: return "iRotateThreadReadyQueue";
    case 0x2D: return "ReleaseWaitThread";
    case 0x2E: return "iReleaseWaitThread";
    case 0x2F: return "GetThreadId";
    case 0x30: return "ReferThreadStatus";
    case 0x31: return "iReferThreadStatus";
    case 0x32: return "SleepThread";
    case 0x33: return "WakeupThread";
    case 0x34: return "iWakeupThread";
    case 0x35: return "CancelWakeupThread";
    case 0x36: return "iCancelWakeupThread";
    case 0x37: return "SuspendThread";
    case 0x38: return "iSuspendThread";
    case 0x39: return "ResumeThread";
    case 0x3A: return "iResumeThread";
    case 0x3C: return "SetupThread";
    case 0x3D: return "SetupHeap";
    case 0x3E: return "EndOfHeap";
    case 0x40: return "CreateSema";
    case 0x41: return "DeleteSema";
    case 0x42: return "SignalSema";
    case 0x43: return "iSignalSema";
    case 0x44: return "WaitSema";
    case 0x45: return "PollSema";
    case 0x46: return "iPollSema";
    case 0x47: return "ReferSemaStatus";
    case 0x48: return "iReferSemaStatus";
    case 0x49: return "iDeleteSema";
    case 0x4A: return "SetOsdConfigParam";
    case 0x4B: return "GetOsdConfigParam";
    case 0x50: return "CreateEventFlag";
    case 0x51: return "DeleteEventFlag";
    case 0x52: return "SetEventFlag";
    case 0x53: return "iSetEventFlag";
    case 0x55: return "PutTLBEntry";
    case 0x56: return "SetTLBEntry";
    case 0x57: return "GetTLBEntry";
    case 0x58: return "ProbeTLBEntry";
    case 0x5A: return "Copy";
    case 0x5B: return "GetEntryAddress";
    case 0x64: return "FlushCache";
    case 0x6C: return "SetCPUTimerHandler";
    case 0x6D: return "SetCPUTimer";
    case 0x70: return "GsGetIMR";
    case 0x71: return "GsPutIMR";
    case 0x73: return "SetVSyncFlag";
    case 0x74: return "SetSyscall";
    case 0x75: return "_print";
    case 0x76: return "SifDmaStat";
    case 0x77: return "SifSetDma";
    case 0x78: return "SifSetDChain";
    case 0x79: return "SifSetReg";
    case 0x7A: return "SifGetReg";
    case 0x7B: return "ExecOSD";
    case 0x7C: return "Deci2Call";
    case 0x7F: return "GetMemorySize";
    default: return "?";
    }
}

} // namespace

// ---- public kernel API ------------------------------------------------------------------
void init(uint32_t entry) {
    for (auto& t : g_threads) t = Thread{};
    for (auto& s : g_semas) s = Sema{};
    for (auto& a : g_alarms) a = Alarm{};
    for (auto& t : g_tlb) t = TlbEntry{};
    for (auto& l : g_intcHandlers) l.clear();
    for (auto& l : g_dmacHandlers) l.clear();
    g_osdConfig = 1u << 16;   // English, 4:3, RGB
    // The program starts in thread 1 (thread 0 is the kernel's idle thread).
    Thread& main = g_threads[1];
    main.used = true;
    main.status = THS_RUN;
    main.entry = entry;
    main.priority = main.initPriority = 0;
    g_current = 1;
    cpu.pc = entry;
    cpu.gpr[SP].d[0] = kRamSize - 0x100;
    cpu.gpr[RA].d[0] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(kTrampThreadExit)));
    g_slice = kSliceLength;
}

bool hasRunningThread() { return g_current >= 0; }

void onVblankStart() {
    if (g_vsyncFlagAddr) {
        write32(g_vsyncFlagAddr, 1);
        if (g_vsyncCsrAddr) write64(g_vsyncCsrAddr, gs::csr());
    }
}
int currentThread() { return g_current; }
bool inInterrupt() { return g_interruptDepth > 0; }
void markRescheduleNeeded() { g_needResched = true; }
void kickInterrupts() { g_slice = 0; }

int signalSemaFromHost(int id) {
    const int r = signalSema(id);
    return r;
}

void onThreadReturned() {
    PS2_DEBUG("kernel", "thread %d returned from its entry function", g_current);
    if (g_current == 1) {   // main() returned without calling Exit
        requestExit(static_cast<int32_t>(ru32(V0)));
        return;
    }
    exitCurrent(false);
}

void serviceInterrupts() {
    for (int guard = 0; interruptsEnabled() && guard < 64; ++guard) {
        if (const uint32_t intc = hw::intcPending()) {
            const int cause = std::countr_zero(intc);
            hw::ackIntc(cause);
            ++g_interruptDepth;
            runHandlers(g_intcHandlers[cause], static_cast<uint32_t>(cause));
            --g_interruptDepth;
            continue;
        }
        if (const uint32_t dmac = hw::dmacPending()) {
            const int ch = std::countr_zero(dmac);
            hw::ackDmac(ch);
            ++g_interruptDepth;
            runHandlers(g_dmacHandlers[ch], static_cast<uint32_t>(ch));
            --g_interruptDepth;
            continue;
        }
        break;
    }
    if (g_exitRequested) return;
    if (g_needResched) reschedule();
}

void idle() {
    // Every EE thread is waiting. Give the time to the IOP if it has work
    // (the EE is often waiting for it), else skip ahead to the next event.
    if (iop::busy()) {
        iop::runWhileEeIdle();
        runDueEvents();
        serviceInterrupts();
        if (g_current < 0) reschedule();
        return;
    }
    const uint64_t next = nextEventTime();
    if (next == UINT64_MAX) {
        PS2_ERROR("kernel", "deadlock: no runnable thread and no pending event\n%s%s", describeThreads().c_str(),
                  iop::describeThreads().c_str());
        requestExit(1);
        return;
    }
    clockWaitUntil(next);
    runDueEvents();
    serviceInterrupts();
    if (g_current < 0) reschedule();
}

std::string describeThreads() {
    std::string s;
    for (int i = 1; i < kMaxThreads; ++i) {
        const Thread& t = g_threads[i];
        if (!t.used) continue;
        char buf[160];
        std::snprintf(buf, sizeof buf, "  thread %d: status=%d prio=%d wait=%d/%d pc=0x%08x entry=0x%08x\n", i, t.status, t.priority,
                      t.waitType, t.waitId, i == g_current ? cpu.pc : t.ctx.pc, t.entry);
        s += buf;
    }
    return s;
}

} // namespace kernel

// ---- entry points used by generated code ------------------------------------------------
void syscall(uint32_t pc) {
    if (g_exitRequested) {   // stopping (--strict, Ctrl+C): unwind instead of doing more guest work
        cpu.pc = kExitPc;
        return;
    }
    const int32_t n = rs32(regs::V1);
    if (g_opt.traceSyscalls)
        PS2_INFO("syscall", "%s (%d) a0=0x%08x a1=0x%08x a2=0x%08x from 0x%08x", kernel::syscallName(n), n, ru32(regs::A0),
                 ru32(regs::A1), ru32(regs::A2), pc);
    clockAdvance(kernel::kSyscallCycles);
    if (!kernel::handleInterruptSyscall(n)) kernel::dispatchSyscall(n);
    if (g_exitRequested || !kernel::hasRunningThread()) return;
    runDueEvents();
    iop::sync();
    kernel::serviceInterrupts();
}

bool safepoint(uint32_t resumePc) {
    g_slice = kernel::kSliceLength;
    if (g_interruptRequested && !g_exitRequested) {
        PS2_INFO("runtime", "interrupted");
        requestExit(130);
    }
    if (g_exitRequested) {
        cpu.pc = kExitPc;   // guest code may have returned through $ra since the request
        return false;
    }
    cpu.pc = resumePc;
    clockAdvance(kernel::kSliceCycles);
    runDueEvents();
    iop::sync();
    kernel::serviceInterrupts();
    return cpu.pc == resumePc && !g_exitRequested && kernel::hasRunningThread();
}

} // namespace ps2
