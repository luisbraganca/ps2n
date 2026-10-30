// IOP kernel services (thbase, thsemap, thevent, thmsgbx, thfpool, thvpool,
// intrman, vblank, sysmem), following the semantics and error codes of the
// IOP kernel as ps2sdk documents and reimplements them.
#include "iop/kernel.h"

#include "clock.h"
#include "iop/cpu.h"
#include "iop/hle.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <bit>
#include <deque>
#include <map>
#include <vector>

namespace ps2::iop::kernel {

namespace {

using namespace hle;

constexpr int32_t KE_OK = 0, KE_ERROR = -1, KE_ILLEGAL_CONTEXT = -100, KE_ILLEGAL_INTRCODE = -101, KE_CPUDI = -102,
                  KE_INTRDISABLE = -103, KE_FOUND_HANDLER = -104, KE_NOTFOUND_HANDLER = -105, KE_NO_MEMORY = -400,
                  KE_ILLEGAL_ENTRY = -402, KE_ILLEGAL_PRIORITY = -403, KE_ILLEGAL_STACK_SIZE = -404,
                  KE_ILLEGAL_THID = -406, KE_UNKNOWN_THID = -407, KE_UNKNOWN_SEMID = -408, KE_UNKNOWN_EVFID = -409,
                  KE_UNKNOWN_MBXID = -410, KE_UNKNOWN_VPLID = -411, KE_UNKNOWN_FPLID = -412, KE_DORMANT = -413,
                  KE_NOT_DORMANT = -414, KE_NOT_SUSPEND = -415, KE_NOT_WAIT = -416, KE_CAN_NOT_WAIT = -417,
                  KE_RELEASE_WAIT = -418, KE_SEMA_ZERO = -419, KE_SEMA_OVF = -420, KE_EVF_COND = -421,
                  KE_EVF_MULTI = -422, KE_EVF_ILPAT = -423, KE_MBOX_NOMSG = -424, KE_WAIT_DELETE = -425,
                  KE_ILLEGAL_MEMBLOCK = -426, KE_ILLEGAL_MEMSIZE = -427;

enum Status : int { THS_RUN = 1, THS_READY = 2, THS_WAIT = 4, THS_SUSPEND = 8, THS_WAITSUSPEND = 12, THS_DORMANT = 16 };
constexpr uint32_t kRegContextSize = 0xB8;   // what the real kernel reserves at the top of a stack

struct Thread {
    bool used = false;
    int status = THS_DORMANT;
    uint32_t attr = 0, option = 0, entry = 0, stack = 0, stackSize = 0, gp = 0;
    int initPriority = 0, priority = 0;
    int waitType = TSW_NONE, waitId = 0;
    int wakeupCount = 0;
    int64_t readySeq = 0;
    uint32_t returnTo = 0;
    Cpu ctx;
    // Details of the current wait.
    uint32_t waitBits = 0, waitResult = 0, waitSize = 0;
    int waitMode = 0;
    uint64_t delayEvent = 0;
};

struct Sema {
    uint32_t attr = 0, option = 0;
    int initial = 0, max = 0, count = 0;
    std::vector<int> waiters;
};
struct EventFlag {
    uint32_t attr = 0, option = 0, initBits = 0, bits = 0;
    std::vector<int> waiters;
};
struct Mbx {
    uint32_t attr = 0, option = 0;
    std::deque<uint32_t> messages;
    std::vector<int> waiters;
};
struct Fpl {
    uint32_t attr = 0, option = 0, blockSize = 0, blocks = 0, base = 0;
    std::vector<uint32_t> free;
    std::vector<int> waiters;
};
struct Vpl {
    uint32_t attr = 0, option = 0, size = 0, base = 0;
    std::map<uint32_t, uint32_t> used;   // address -> size
    std::vector<int> waiters;
};
struct Alarm {
    uint32_t handler = 0, arg = 0, gp = 0;
    uint64_t event = 0;
};
struct IntrHandler {
    bool used = false;
    uint32_t fn = 0, arg = 0, gp = 0;
    int mode = 0;
};
struct VblankHandler {
    int edge = 0, priority = 0;
    uint32_t fn = 0, arg = 0, gp = 0;
};
struct Block {
    uint32_t addr, size;
};

std::vector<Thread> g_threads;     // index = thread id; 0 unused
std::map<int, Sema> g_semas;
std::map<int, EventFlag> g_evfs;
std::map<int, Mbx> g_mbxs;
std::map<int, Fpl> g_fpls;
std::map<int, Vpl> g_vpls;
std::map<int, Alarm> g_alarms;
int g_nextId = 1;                  // shared counter for synchronisation objects
int g_current = -1;
bool g_stop = false;               // the CPU loop must return (block, preemption)
bool g_resched = false;
int64_t g_readyCounter = 0, g_frontCounter = 0;

std::array<IntrHandler, 64> g_intr;
uint64_t g_intrPending = 0, g_intrMask = 0;
bool g_intrEnabled = true;         // CPU interrupts (CpuSuspendIntr & co.)
bool g_ictrl = true;               // INTC master enable (I_CTRL)

std::vector<VblankHandler> g_vblankHandlers;
int g_vblankEf = 0, g_systemStatusEf = 0;
constexpr uint32_t EF_VBLANK_START = 1, EF_VBLANK_END = 2, EF_NON_VBLANK = 4, EF_VBLANK = 8;

std::vector<Block> g_blocks;       // sysmem: allocated blocks, sorted by address
uint32_t g_threadExitTrap = 0;

// ---- helpers ------------------------------------------------------------------------------------
Thread* thread(int id) {
    if (id <= 0 || id >= static_cast<int>(g_threads.size()) || !g_threads[static_cast<size_t>(id)].used) return nullptr;
    return &g_threads[static_cast<size_t>(id)];
}
Thread& cur() { return g_threads[static_cast<size_t>(g_current)]; }
int selfOr(int id) { return id == 0 ? g_current : id; }

bool interruptContext() { return nestingDepth() > 0; }

void makeReady(Thread& t, bool front = false) {
    t.status = THS_READY;
    t.readySeq = front ? --g_frontCounter : ++g_readyCounter;
    if (g_current < 0 || t.priority < cur().priority) {
        g_resched = true;
        if (!interruptContext()) g_stop = true;
    }
}

// Takes the running thread off the CPU with a new status.
void leaveCurrent(int status) {
    Thread& t = cur();
    t.ctx = g_cpu;
    t.status = status;
    g_current = -1;
    g_stop = g_resched = true;
}

void removeWaiter(std::vector<int>& q, int id) { q.erase(std::remove(q.begin(), q.end(), id), q.end()); }

// The waiter to release next: FIFO, or highest priority first.
size_t pickWaiter(const std::vector<int>& q, bool byPriority) {
    size_t best = 0;
    if (byPriority)
        for (size_t i = 1; i < q.size(); ++i)
            if (thread(q[i])->priority < thread(q[best])->priority) best = i;
    return best;
}

// Takes a waiting thread off whatever queue it waits in.
void detachWait(Thread& t, int id) {
    switch (t.waitType) {
    case TSW_SEMA: if (auto it = g_semas.find(t.waitId); it != g_semas.end()) removeWaiter(it->second.waiters, id); break;
    case TSW_EVENTFLAG: if (auto it = g_evfs.find(t.waitId); it != g_evfs.end()) removeWaiter(it->second.waiters, id); break;
    case TSW_MBX: if (auto it = g_mbxs.find(t.waitId); it != g_mbxs.end()) removeWaiter(it->second.waiters, id); break;
    case TSW_FPL: if (auto it = g_fpls.find(t.waitId); it != g_fpls.end()) removeWaiter(it->second.waiters, id); break;
    case TSW_VPL: if (auto it = g_vpls.find(t.waitId); it != g_vpls.end()) removeWaiter(it->second.waiters, id); break;
    default: break;
    }
    if (t.delayEvent) {
        cancelEvent(t.delayEvent);
        t.delayEvent = 0;
    }
}

void schedule() {
    g_resched = false;
    int best = -1;
    for (size_t i = 1; i < g_threads.size(); ++i) {
        const Thread& t = g_threads[i];
        if (!t.used || t.status != THS_READY) continue;
        if (best < 0) {
            best = static_cast<int>(i);
            continue;
        }
        const Thread& b = g_threads[static_cast<size_t>(best)];
        if (t.priority < b.priority || (t.priority == b.priority && t.readySeq < b.readySeq)) best = static_cast<int>(i);
    }
    if (g_current >= 0) {
        Thread& c = cur();
        if (best < 0 || c.priority <= g_threads[static_cast<size_t>(best)].priority) return;   // keep running
        c.ctx = g_cpu;   // preempted: first in line at its priority
        c.status = THS_READY;
        c.readySeq = --g_frontCounter;
    }
    g_current = best;
    if (best >= 0) {
        Thread& n = cur();
        n.status = THS_RUN;
        g_cpu = n.ctx;
    }
}

uint32_t interruptStack() { return nestingDepth() > 0 ? 0 : kInterruptStackTop; }

void threadReturned();   // trap: a thread's entry function returned

// ---- sysmem --------------------------------------------------------------------------------------
std::vector<Block> freeGaps() {
    std::vector<Block> gaps;
    uint32_t at = kUserMemory;
    for (const Block& b : g_blocks) {
        if (b.addr > at) gaps.push_back({at, b.addr - at});
        at = std::max(at, b.addr + b.size);
    }
    if (at < mem::kRamSize) gaps.push_back({at, mem::kRamSize - at});
    return gaps;
}

void insertBlock(uint32_t addr, uint32_t size) {
    auto it = std::lower_bound(g_blocks.begin(), g_blocks.end(), addr, [](const Block& b, uint32_t a) { return b.addr < a; });
    g_blocks.insert(it, Block{addr, size});
}

void smAllocSysMemory() {
    const uint32_t r = allocMemory(arg(1), sarg(0), arg(2));
    retU(r);
}
void smFreeSysMemory() { ret(freeMemory(arg(0))); }
void smQueryMemSize() { retU(mem::kRamSize); }
void smQueryMaxFreeMemSize() {
    uint32_t best = 0;
    for (const Block& g : freeGaps()) best = std::max(best, g.size);
    retU(best);
}
void smQueryTotalFreeMemSize() { retU(freeMemorySize()); }
void smQueryBlockTopAddress() {
    const uint32_t a = arg(0) & 0x1FFFFFFFu;
    for (const Block& b : g_blocks)
        if (a >= b.addr && a < b.addr + b.size) return retU(b.addr);
    for (const Block& g : freeGaps())
        if (a >= g.addr && a < g.addr + g.size) return retU(g.addr | 0x80000000u);
    ret(-1);
}
void smQueryBlockSize() {
    const uint32_t a = arg(0) & 0x1FFFFFFFu;
    for (const Block& b : g_blocks)
        if (a >= b.addr && a < b.addr + b.size) return retU(b.size);
    for (const Block& g : freeGaps())
        if (a >= g.addr && a < g.addr + g.size) return retU(g.size | 0x80000000u);
    ret(-1);
}
void smKprintf() {
    hostPrint(formatIop(arg(0), 1));
    ret(0);
}
void smGetSysmemInternalData() { retU(kKernelData + 0x800); }

// ---- thbase ---------------------------------------------------------------------------------------
int newThread(uint32_t entry, int priority, uint32_t stackSize, uint32_t attr, uint32_t option, uint32_t gp) {
    stackSize = (stackSize + 255) & ~255u;
    const uint32_t stack = allocMemory(stackSize, 1);
    if (!stack) return KE_NO_MEMORY;
    if (!(attr & 0x00100000u)) mem::fill(stack, 0xFF, stackSize - 0x30);   // TH_NO_FILLSTACK not set
    size_t id = 1;
    while (id < g_threads.size() && g_threads[id].used) ++id;
    if (id == g_threads.size()) g_threads.emplace_back();
    Thread& t = g_threads[id];
    t = Thread{};
    t.used = true;
    t.entry = entry;
    t.stack = stack;
    t.stackSize = stackSize;
    t.initPriority = t.priority = priority;
    t.attr = attr;
    t.option = option;
    t.gp = gp;
    return static_cast<int>(id);
}

int startThreadWith(int id, uint32_t a0, uint32_t a1, uint32_t sp, uint32_t returnTo) {
    Thread* t = thread(id);
    if (!t) return KE_UNKNOWN_THID;
    if (t->status != THS_DORMANT) return KE_NOT_DORMANT;
    t->ctx = Cpu{};
    t->ctx.gpr[R_A0] = a0;
    t->ctx.gpr[R_A1] = a1;
    t->ctx.gpr[R_SP] = sp;
    t->ctx.gpr[R_GP] = t->gp;
    t->ctx.gpr[R_RA] = returnTo ? returnTo : g_threadExitTrap;
    t->ctx.pc = t->entry;
    t->ctx.npc = t->entry + 4;
    t->returnTo = returnTo;
    t->priority = t->initPriority;
    t->wakeupCount = 0;
    t->waitType = TSW_NONE;
    makeReady(*t);
    return KE_OK;
}

void thCreateThread() {
    const uint32_t p = arg(0);
    const uint32_t attr = mem::read32(p), option = mem::read32(p + 4), entry = mem::read32(p + 8);
    const uint32_t stackSize = mem::read32(p + 12);
    const auto priority = static_cast<int>(mem::read32(p + 16));
    if (interruptContext()) return ret(KE_ILLEGAL_CONTEXT);
    if (priority < 1 || priority > 126) return ret(KE_ILLEGAL_PRIORITY);
    if (entry & 3) return ret(KE_ILLEGAL_ENTRY);
    if (stackSize < 0x130) return ret(KE_ILLEGAL_STACK_SIZE);
    ret(newThread(entry, priority, stackSize, attr, option, g_cpu.gpr[R_GP]));
}

void thDeleteThread() {
    if (interruptContext()) return ret(KE_ILLEGAL_CONTEXT);
    const int id = sarg(0);
    if (id == 0) return ret(KE_ILLEGAL_THID);
    Thread* t = thread(id);
    if (!t) return ret(KE_UNKNOWN_THID);
    if (t->status != THS_DORMANT) return ret(KE_NOT_DORMANT);
    freeMemory(t->stack);
    *t = Thread{};
    ret(KE_OK);
}

void thStartThread() {
    if (interruptContext()) return ret(KE_ILLEGAL_CONTEXT);
    const int id = sarg(0);
    if (id == 0) return ret(KE_ILLEGAL_THID);
    Thread* t = thread(id);
    if (!t) return ret(KE_UNKNOWN_THID);
    ret(startThreadWith(id, arg(1), 0, t->stack + t->stackSize - kRegContextSize, 0));
}

void thStartThreadArgs() {
    if (interruptContext()) return ret(KE_ILLEGAL_CONTEXT);
    const int id = sarg(0);
    if (id == 0) return ret(KE_ILLEGAL_THID);
    Thread* t = thread(id);
    if (!t) return ret(KE_UNKNOWN_THID);
    if (t->status != THS_DORMANT) return ret(KE_NOT_DORMANT);
    const auto size = static_cast<uint32_t>(sarg(1));
    const uint32_t aligned = (size + 7) & ~7u;
    const uint32_t argsAt = t->stack + t->stackSize - aligned;
    if (sarg(1) > 0 && arg(2)) {
        std::vector<uint8_t> tmp(size);
        mem::readBlock(arg(2), tmp.data(), size);
        mem::writeBlock(argsAt, tmp.data(), size);
    }
    ret(startThreadWith(id, arg(1), argsAt, argsAt - kRegContextSize, 0));
}

void thExitThread() {
    if (interruptContext()) return ret(KE_ILLEGAL_CONTEXT);
    exitCurrent(false);
}
void thExitDeleteThread() {
    if (interruptContext()) return ret(KE_ILLEGAL_CONTEXT);
    exitCurrent(true);
}

void terminate(int id) {
    Thread& t = *thread(id);
    if (t.status == THS_WAIT || t.status == THS_WAITSUSPEND) detachWait(t, id);
    t.status = THS_DORMANT;
    t.waitType = TSW_NONE;
}

void thTerminateThread() {
    const int id = sarg(0);
    if (id == 0 || id == g_current) return ret(KE_ILLEGAL_THID);
    Thread* t = thread(id);
    if (!t) return ret(KE_UNKNOWN_THID);
    if (t->status == THS_DORMANT) return ret(KE_DORMANT);
    terminate(id);
    ret(KE_OK);
}

void thDisableDispatchThread() { ret(KE_OK); }
void thEnableDispatchThread() { ret(KE_OK); }

void thChangeThreadPriority() {
    const int id = selfOr(sarg(0));
    int prio = sarg(1);
    Thread* t = thread(id);
    if (!t) return ret(KE_UNKNOWN_THID);
    if (prio == 0) prio = t->initPriority;
    if (prio < 1 || prio > 126) return ret(KE_ILLEGAL_PRIORITY);
    if (t->status == THS_DORMANT) return ret(KE_DORMANT);
    t->priority = prio;
    ret(KE_OK);
    g_resched = true;   // a lower priority may let someone else run; a higher one may preempt
    if (!interruptContext()) g_stop = true;
}

void thRotateThreadReadyQueue() {
    int prio = sarg(0);
    if (prio == 0 && g_current >= 0) prio = cur().priority;
    ret(KE_OK);
    if (!interruptContext() && g_current >= 0 && cur().priority == prio) {
        const int self = g_current;
        leaveCurrent(THS_READY);   // to the back of its queue
        g_threads[static_cast<size_t>(self)].readySeq = ++g_readyCounter;
        return;
    }
    // Rotate among ready threads of that priority.
    int first = -1;
    for (size_t i = 1; i < g_threads.size(); ++i) {
        const Thread& t = g_threads[i];
        if (t.used && t.status == THS_READY && t.priority == prio &&
            (first < 0 || t.readySeq < g_threads[static_cast<size_t>(first)].readySeq))
            first = static_cast<int>(i);
    }
    if (first >= 0) g_threads[static_cast<size_t>(first)].readySeq = ++g_readyCounter;
    g_resched = true;
}

void thReleaseWaitThread() {
    const int id = sarg(0);
    Thread* t = thread(id);
    if (!t || id == g_current) return ret(id == g_current ? KE_ILLEGAL_THID : KE_UNKNOWN_THID);
    if (t->status != THS_WAIT && t->status != THS_WAITSUSPEND) return ret(KE_NOT_WAIT);
    detachWait(*t, id);
    wakeThread(id, KE_RELEASE_WAIT);
    ret(KE_OK);
}

void thGetThreadId() { ret(interruptContext() ? KE_ILLEGAL_CONTEXT : g_current); }
void thCheckThreadStack() { ret(g_current >= 0 ? static_cast<int32_t>(g_cpu.gpr[R_SP] - cur().stack) : 0); }

void writeThreadInfo(const Thread& t, uint32_t info) {
    mem::fill(info, 0, 68);
    mem::write32(info + 0, t.attr);
    mem::write32(info + 4, t.option);
    mem::write32(info + 8, static_cast<uint32_t>(t.status));
    mem::write32(info + 12, t.entry);
    mem::write32(info + 16, t.stack);
    mem::write32(info + 20, t.stackSize);
    mem::write32(info + 24, t.gp);
    mem::write32(info + 28, static_cast<uint32_t>(t.initPriority));
    mem::write32(info + 32, static_cast<uint32_t>(t.priority));
    mem::write32(info + 36, static_cast<uint32_t>(t.waitType == TSW_SERVICE ? TSW_SLEEP : t.waitType));
    mem::write32(info + 40, static_cast<uint32_t>(t.waitId));
    mem::write32(info + 44, static_cast<uint32_t>(t.wakeupCount));
}

void thReferThreadStatus() {
    const int id = selfOr(sarg(0));
    Thread* t = thread(id);
    if (!t) return ret(KE_UNKNOWN_THID);
    if (arg(1)) writeThreadInfo(*t, arg(1));
    ret(KE_OK);
}

void thSleepThread() {
    if (interruptContext()) return ret(KE_ILLEGAL_CONTEXT);
    Thread& t = cur();
    if (t.wakeupCount > 0) {
        --t.wakeupCount;
        return ret(KE_OK);
    }
    blockCurrent(TSW_SLEEP, 0);
}

void thWakeupThread() {
    const int id = sarg(0);
    if (id == 0 || (id == g_current && !interruptContext())) return ret(KE_ILLEGAL_THID);
    Thread* t = thread(id);
    if (!t) return ret(KE_UNKNOWN_THID);
    if (t->status == THS_DORMANT) return ret(KE_DORMANT);
    if ((t->status == THS_WAIT || t->status == THS_WAITSUSPEND) && t->waitType == TSW_SLEEP) wakeThread(id, KE_OK);
    else ++t->wakeupCount;
    ret(KE_OK);
}

void thCancelWakeupThread() {
    Thread* t = thread(selfOr(sarg(0)));
    if (!t) return ret(KE_UNKNOWN_THID);
    const int n = t->wakeupCount;
    t->wakeupCount = 0;
    ret(n);
}

void thSuspendThread() {
    const int id = sarg(0);
    if (id == 0 || id == g_current) return ret(KE_ILLEGAL_THID);
    Thread* t = thread(id);
    if (!t) return ret(KE_UNKNOWN_THID);
    if (t->status == THS_DORMANT) return ret(KE_DORMANT);
    if (t->status == THS_READY) t->status = THS_SUSPEND;
    else if (t->status == THS_WAIT) t->status = THS_WAITSUSPEND;
    ret(KE_OK);
}

void thResumeThread() {
    const int id = sarg(0);
    Thread* t = thread(id);
    if (!t) return ret(KE_UNKNOWN_THID);
    if (t->status == THS_SUSPEND) makeReady(*t);
    else if (t->status == THS_WAITSUSPEND) t->status = THS_WAIT;
    else return ret(KE_NOT_SUSPEND);
    ret(KE_OK);
}

void delayExpired(uint64_t id) {
    Thread* t = thread(static_cast<int>(id));
    if (!t) return;
    t->delayEvent = 0;
    if (t->waitType == TSW_DELAY) wakeThread(static_cast<int>(id), KE_OK);
}

void thDelayThread() {
    if (interruptContext()) return ret(KE_ILLEGAL_CONTEXT);
    const uint64_t usec = std::max<uint32_t>(arg(0), 1);
    const int id = g_current;
    blockCurrent(TSW_DELAY, 0);
    g_threads[static_cast<size_t>(id)].delayEvent =
        scheduleEvent(nowCycles() + usec * kEeClockHz / 1000000, &delayExpired, static_cast<uint64_t>(id));
}

void writeClock(uint32_t addr, uint64_t cycles) {
    mem::write32(addr, static_cast<uint32_t>(cycles));
    mem::write32(addr + 4, static_cast<uint32_t>(cycles >> 32));
}
uint64_t readClock(uint32_t addr) { return mem::read32(addr) | (static_cast<uint64_t>(mem::read32(addr + 4)) << 32); }

void thGetSystemTime() {
    writeClock(arg(0), systemTime());
    ret(KE_OK);
}
void thGetSystemTimeLow() { retU(static_cast<uint32_t>(systemTime())); }

void alarmFired(uint64_t id) {
    auto it = g_alarms.find(static_cast<int>(id));
    if (it == g_alarms.end()) return;
    const Alarm a = it->second;
    const uint32_t next = callHandler(a.handler, a.arg, a.gp);
    it = g_alarms.find(static_cast<int>(id));   // the handler may have cancelled it
    if (it == g_alarms.end()) return;
    if (next == 0) {
        g_alarms.erase(it);
        return;
    }
    it->second.event = scheduleEvent(nowCycles() + static_cast<uint64_t>(next) * kEeCyclesPerIopCycle, &alarmFired, id);
}

void thSetAlarm() {
    const uint64_t delay = std::max<uint64_t>(readClock(arg(0)), 1);
    const int id = g_nextId++;
    Alarm a;
    a.handler = arg(1);
    a.arg = arg(2);
    a.gp = g_cpu.gpr[R_GP];
    a.event = scheduleEvent(nowCycles() + delay * kEeCyclesPerIopCycle, &alarmFired, static_cast<uint64_t>(id));
    g_alarms[id] = a;
    ret(KE_OK);
}

void thCancelAlarm() {
    for (auto it = g_alarms.begin(); it != g_alarms.end(); ++it) {
        if (it->second.handler == arg(0) && it->second.arg == arg(1)) {
            cancelEvent(it->second.event);
            g_alarms.erase(it);
            return ret(KE_OK);
        }
    }
    ret(KE_NOTFOUND_HANDLER);
}

void thUSec2SysClock() {
    writeClock(arg(1), static_cast<uint64_t>(arg(0)) * kIopClockHz / 1000000);
    ret(KE_OK);
}
void thSysClock2USec() {
    const uint64_t usec = readClock(arg(0)) * 1000000 / kIopClockHz;
    if (arg(1)) mem::write32(arg(1), static_cast<uint32_t>(usec / 1000000));
    if (arg(2)) mem::write32(arg(2), static_cast<uint32_t>(usec % 1000000));
    ret(KE_OK);
}
void thGetSystemStatusFlag() { ret(g_systemStatusEf); }
void thGetThreadCurrentPriority() { ret(g_current >= 0 ? cur().priority : KE_ILLEGAL_CONTEXT); }
void thGetThreadmanData() { retU(kKernelData + 0x900); }
void thGetThreadStackFreeSize() {
    Thread* t = thread(selfOr(sarg(0)));
    if (!t) return ret(KE_UNKNOWN_THID);
    uint32_t n = 0;
    while (n < t->stackSize && mem::read8(t->stack + n) == 0xFF) ++n;
    ret(static_cast<int32_t>(n));
}
void thGetThreadmanIdList() {
    // (type, readbuf, readbufsize, objectcount): only the thread list (type 1) is supported.
    const uint32_t buf = arg(1), cap = arg(2);
    uint32_t n = 0;
    for (size_t i = 1; i < g_threads.size(); ++i) {
        if (!g_threads[i].used) continue;
        if (n < cap && buf) mem::write32(buf + 4 * n, static_cast<uint32_t>(i));
        ++n;
    }
    if (arg(3)) mem::write32(arg(3), n);
    ret(static_cast<int32_t>(std::min(n, cap)));
}

// ---- thsemap ---------------------------------------------------------------------------------------
void smCreateSema() {
    const uint32_t p = arg(0);
    Sema s;
    s.attr = mem::read32(p);
    s.option = mem::read32(p + 4);
    s.initial = s.count = static_cast<int32_t>(mem::read32(p + 8));
    s.max = static_cast<int32_t>(mem::read32(p + 12));
    const int id = g_nextId++;
    g_semas[id] = std::move(s);
    ret(id);
}

void deleteWaiters(std::vector<int>& q) {
    for (int w : std::vector<int>(q)) wakeThread(w, KE_WAIT_DELETE);
    q.clear();
}

void smDeleteSema() {
    auto it = g_semas.find(sarg(0));
    if (it == g_semas.end()) return ret(KE_UNKNOWN_SEMID);
    deleteWaiters(it->second.waiters);
    g_semas.erase(it);
    ret(KE_OK);
}

void smSignalSema() {
    auto it = g_semas.find(sarg(0));
    if (it == g_semas.end()) return ret(KE_UNKNOWN_SEMID);
    Sema& s = it->second;
    if (!s.waiters.empty()) {
        const size_t i = pickWaiter(s.waiters, s.attr & 1);
        const int w = s.waiters[i];
        s.waiters.erase(s.waiters.begin() + static_cast<std::ptrdiff_t>(i));
        wakeThread(w, KE_OK);
        return ret(KE_OK);
    }
    if (s.count >= s.max) return ret(KE_SEMA_OVF);
    ++s.count;
    ret(KE_OK);
}

void smWaitSema() {
    auto it = g_semas.find(sarg(0));
    if (it == g_semas.end()) return ret(KE_UNKNOWN_SEMID);
    Sema& s = it->second;
    if (s.count > 0) {
        --s.count;
        return ret(KE_OK);
    }
    if (interruptContext()) return ret(KE_CAN_NOT_WAIT);
    s.waiters.push_back(g_current);
    blockCurrent(TSW_SEMA, it->first);
}

void smPollSema() {
    auto it = g_semas.find(sarg(0));
    if (it == g_semas.end()) return ret(KE_UNKNOWN_SEMID);
    if (it->second.count <= 0) return ret(KE_SEMA_ZERO);
    --it->second.count;
    ret(KE_OK);
}

void smReferSemaStatus() {
    auto it = g_semas.find(sarg(0));
    if (it == g_semas.end()) return ret(KE_UNKNOWN_SEMID);
    const Sema& s = it->second;
    const uint32_t info = arg(1);
    mem::fill(info, 0, 32);
    mem::write32(info + 0, s.attr);
    mem::write32(info + 4, s.option);
    mem::write32(info + 8, static_cast<uint32_t>(s.initial));
    mem::write32(info + 12, static_cast<uint32_t>(s.max));
    mem::write32(info + 16, static_cast<uint32_t>(s.count));
    mem::write32(info + 20, static_cast<uint32_t>(s.waiters.size()));
    ret(KE_OK);
}

// ---- thevent -----------------------------------------------------------------------------------------
constexpr uint32_t EA_MULTI = 2, WEF_OR = 1, WEF_CLEAR = 0x10, WEF_CLEARALL = 0x20;

bool evfSatisfied(uint32_t bits, uint32_t pattern, int mode) {
    return (mode & WEF_OR) ? (bits & pattern) != 0 : (bits & pattern) == pattern;
}
void evfClear(EventFlag& e, uint32_t pattern, int mode) {
    if (mode & WEF_CLEARALL) e.bits = 0;
    else if (mode & WEF_CLEAR) e.bits &= ~pattern;
}

void evfRelease(EventFlag& e) {
    for (size_t i = 0; i < e.waiters.size();) {
        Thread& t = *thread(e.waiters[i]);
        if (!evfSatisfied(e.bits, t.waitBits, t.waitMode)) {
            ++i;
            continue;
        }
        if (t.waitResult) mem::write32(t.waitResult, e.bits);
        evfClear(e, t.waitBits, t.waitMode);
        const int w = e.waiters[i];
        e.waiters.erase(e.waiters.begin() + static_cast<std::ptrdiff_t>(i));
        wakeThread(w, KE_OK);
    }
}

void evCreateEventFlag() {
    const uint32_t p = arg(0);
    const int id = createEventFlag(mem::read32(p), mem::read32(p + 8));
    g_evfs[id].option = mem::read32(p + 4);
    ret(id);
}

void evDeleteEventFlag() {
    auto it = g_evfs.find(sarg(0));
    if (it == g_evfs.end()) return ret(KE_UNKNOWN_EVFID);
    deleteWaiters(it->second.waiters);
    g_evfs.erase(it);
    ret(KE_OK);
}

void evSetEventFlag() {
    auto it = g_evfs.find(sarg(0));
    if (it == g_evfs.end()) return ret(KE_UNKNOWN_EVFID);
    it->second.bits |= arg(1);
    evfRelease(it->second);
    ret(KE_OK);
}

void evClearEventFlag() {
    auto it = g_evfs.find(sarg(0));
    if (it == g_evfs.end()) return ret(KE_UNKNOWN_EVFID);
    it->second.bits &= arg(1);
    ret(KE_OK);
}

void evWaitEventFlag() {
    auto it = g_evfs.find(sarg(0));
    if (it == g_evfs.end()) return ret(KE_UNKNOWN_EVFID);
    EventFlag& e = it->second;
    const uint32_t pattern = arg(1), result = arg(3);
    const auto mode = static_cast<int>(arg(2));
    if (pattern == 0) return ret(KE_EVF_ILPAT);
    if (!(e.attr & EA_MULTI) && !e.waiters.empty()) return ret(KE_EVF_MULTI);
    if (evfSatisfied(e.bits, pattern, mode)) {
        if (result) mem::write32(result, e.bits);
        evfClear(e, pattern, mode);
        return ret(KE_OK);
    }
    if (interruptContext()) return ret(KE_CAN_NOT_WAIT);
    Thread& t = cur();
    t.waitBits = pattern;
    t.waitMode = mode;
    t.waitResult = result;
    e.waiters.push_back(g_current);
    blockCurrent(TSW_EVENTFLAG, it->first);
}

void evPollEventFlag() {
    auto it = g_evfs.find(sarg(0));
    if (it == g_evfs.end()) return ret(KE_UNKNOWN_EVFID);
    EventFlag& e = it->second;
    const uint32_t pattern = arg(1), result = arg(3);
    const auto mode = static_cast<int>(arg(2));
    if (pattern == 0) return ret(KE_EVF_ILPAT);
    if (!(e.attr & EA_MULTI) && !e.waiters.empty()) return ret(KE_EVF_MULTI);
    if (!evfSatisfied(e.bits, pattern, mode)) return ret(KE_EVF_COND);
    if (result) mem::write32(result, e.bits);
    evfClear(e, pattern, mode);
    ret(KE_OK);
}

void evReferEventFlagStatus() {
    auto it = g_evfs.find(sarg(0));
    if (it == g_evfs.end()) return ret(KE_UNKNOWN_EVFID);
    const EventFlag& e = it->second;
    const uint32_t info = arg(1);
    mem::fill(info, 0, 28);
    mem::write32(info + 0, e.attr);
    mem::write32(info + 4, e.option);
    mem::write32(info + 8, e.initBits);
    mem::write32(info + 12, e.bits);
    mem::write32(info + 16, static_cast<uint32_t>(e.waiters.size()));
    ret(KE_OK);
}

// ---- thmsgbx ------------------------------------------------------------------------------------------
constexpr uint32_t MBA_THPRI = 1, MBA_MSPRI = 4;

void mbCreateMbx() {
    Mbx m;
    m.attr = mem::read32(arg(0));
    m.option = mem::read32(arg(0) + 4);
    const int id = g_nextId++;
    g_mbxs[id] = std::move(m);
    ret(id);
}

void mbDeleteMbx() {
    auto it = g_mbxs.find(sarg(0));
    if (it == g_mbxs.end()) return ret(KE_UNKNOWN_MBXID);
    deleteWaiters(it->second.waiters);
    g_mbxs.erase(it);
    ret(KE_OK);
}

void mbSendMbx() {
    auto it = g_mbxs.find(sarg(0));
    if (it == g_mbxs.end()) return ret(KE_UNKNOWN_MBXID);
    Mbx& m = it->second;
    const uint32_t msg = arg(1);
    mem::write32(msg, 0);
    if (!m.waiters.empty()) {
        const size_t i = pickWaiter(m.waiters, m.attr & MBA_THPRI);
        const int w = m.waiters[i];
        m.waiters.erase(m.waiters.begin() + static_cast<std::ptrdiff_t>(i));
        mem::write32(thread(w)->waitResult, msg);
        wakeThread(w, KE_OK);
        return ret(KE_OK);
    }
    if (m.attr & MBA_MSPRI) {
        const uint8_t prio = mem::read8(msg + 4);
        auto pos = std::find_if(m.messages.begin(), m.messages.end(), [&](uint32_t o) { return mem::read8(o + 4) > prio; });
        m.messages.insert(pos, msg);
    } else {
        m.messages.push_back(msg);
    }
    ret(KE_OK);
}

void mbReceiveMbx() {
    auto it = g_mbxs.find(sarg(1));
    if (it == g_mbxs.end()) return ret(KE_UNKNOWN_MBXID);
    Mbx& m = it->second;
    if (!m.messages.empty()) {
        mem::write32(arg(0), m.messages.front());
        m.messages.pop_front();
        return ret(KE_OK);
    }
    if (interruptContext()) return ret(KE_CAN_NOT_WAIT);
    cur().waitResult = arg(0);
    m.waiters.push_back(g_current);
    blockCurrent(TSW_MBX, it->first);
}

void mbPollMbx() {
    auto it = g_mbxs.find(sarg(1));
    if (it == g_mbxs.end()) return ret(KE_UNKNOWN_MBXID);
    Mbx& m = it->second;
    if (m.messages.empty()) return ret(KE_MBOX_NOMSG);
    mem::write32(arg(0), m.messages.front());
    m.messages.pop_front();
    ret(KE_OK);
}

void mbReferMbxStatus() {
    auto it = g_mbxs.find(sarg(0));
    if (it == g_mbxs.end()) return ret(KE_UNKNOWN_MBXID);
    const Mbx& m = it->second;
    const uint32_t info = arg(1);
    mem::fill(info, 0, 28);
    mem::write32(info + 0, m.attr);
    mem::write32(info + 4, m.option);
    mem::write32(info + 8, static_cast<uint32_t>(m.waiters.size()));
    mem::write32(info + 12, static_cast<uint32_t>(m.messages.size()));
    mem::write32(info + 16, m.messages.empty() ? 0 : m.messages.front());
    ret(KE_OK);
}

// ---- thfpool -------------------------------------------------------------------------------------------
void fpCreateFpl() {
    const uint32_t p = arg(0);
    Fpl f;
    f.attr = mem::read32(p);
    f.option = mem::read32(p + 4);
    f.blockSize = (mem::read32(p + 8) + 3) & ~3u;
    f.blocks = mem::read32(p + 12);
    if (!f.blockSize || !f.blocks) return ret(KE_ILLEGAL_MEMSIZE);
    f.base = allocMemory(f.blockSize * f.blocks, (f.attr & 0x200) ? 1 : 0);
    if (!f.base) return ret(KE_NO_MEMORY);
    for (uint32_t i = f.blocks; i-- > 0;) f.free.push_back(f.base + i * f.blockSize);
    const int id = g_nextId++;
    g_fpls[id] = std::move(f);
    ret(id);
}

void fpDeleteFpl() {
    auto it = g_fpls.find(sarg(0));
    if (it == g_fpls.end()) return ret(KE_UNKNOWN_FPLID);
    deleteWaiters(it->second.waiters);
    freeMemory(it->second.base);
    g_fpls.erase(it);
    ret(KE_OK);
}

void fpAllocate(bool wait) {
    auto it = g_fpls.find(sarg(0));
    if (it == g_fpls.end()) return ret(KE_UNKNOWN_FPLID);
    Fpl& f = it->second;
    if (!f.free.empty()) {
        retU(f.free.back());
        f.free.pop_back();
        return;
    }
    if (!wait) return ret(KE_NO_MEMORY);
    if (interruptContext()) return ret(KE_CAN_NOT_WAIT);
    f.waiters.push_back(g_current);
    blockCurrent(TSW_FPL, it->first);
}
void fpAllocateFpl() { fpAllocate(true); }
void fpPAllocateFpl() { fpAllocate(false); }

void fpFreeFpl() {
    auto it = g_fpls.find(sarg(0));
    if (it == g_fpls.end()) return ret(KE_UNKNOWN_FPLID);
    Fpl& f = it->second;
    const uint32_t block = arg(1);
    if (block < f.base || block >= f.base + f.blockSize * f.blocks) return ret(KE_ILLEGAL_MEMBLOCK);
    if (!f.waiters.empty()) {
        const size_t i = pickWaiter(f.waiters, f.attr & 1);
        const int w = f.waiters[i];
        f.waiters.erase(f.waiters.begin() + static_cast<std::ptrdiff_t>(i));
        wakeThread(w, static_cast<int32_t>(block));
    } else {
        f.free.push_back(block);
    }
    ret(KE_OK);
}

void fpReferFplStatus() {
    auto it = g_fpls.find(sarg(0));
    if (it == g_fpls.end()) return ret(KE_UNKNOWN_FPLID);
    const Fpl& f = it->second;
    const uint32_t info = arg(1);
    mem::fill(info, 0, 40);
    mem::write32(info + 0, f.attr);
    mem::write32(info + 4, f.option);
    mem::write32(info + 8, f.blockSize);
    mem::write32(info + 12, f.blocks);
    mem::write32(info + 16, static_cast<uint32_t>(f.free.size()));
    mem::write32(info + 20, static_cast<uint32_t>(f.waiters.size()));
    ret(KE_OK);
}

// ---- thvpool --------------------------------------------------------------------------------------------
uint32_t vplAlloc(Vpl& v, uint32_t size) {
    size = (size + 7) & ~7u;
    uint32_t at = v.base;
    for (const auto& [a, s] : v.used) {
        if (a - at >= size) break;
        at = a + s;
    }
    if (at + size > v.base + v.size) return 0;
    v.used[at] = size;
    return at;
}

void vpCreateVpl() {
    const uint32_t p = arg(0);
    Vpl v;
    v.attr = mem::read32(p);
    v.option = mem::read32(p + 4);
    v.size = (mem::read32(p + 8) + 7) & ~7u;
    v.base = allocMemory(v.size, (v.attr & 0x200) ? 1 : 0);
    if (!v.base) return ret(KE_NO_MEMORY);
    const int id = g_nextId++;
    g_vpls[id] = std::move(v);
    ret(id);
}

void vpDeleteVpl() {
    auto it = g_vpls.find(sarg(0));
    if (it == g_vpls.end()) return ret(KE_UNKNOWN_VPLID);
    deleteWaiters(it->second.waiters);
    freeMemory(it->second.base);
    g_vpls.erase(it);
    ret(KE_OK);
}

void vpAllocate(bool wait) {
    auto it = g_vpls.find(sarg(0));
    if (it == g_vpls.end()) return ret(KE_UNKNOWN_VPLID);
    Vpl& v = it->second;
    const uint32_t size = arg(1);
    if (size == 0 || size > v.size) return ret(KE_ILLEGAL_MEMSIZE);
    if (const uint32_t a = vplAlloc(v, size)) return retU(a);
    if (!wait) return ret(KE_NO_MEMORY);
    if (interruptContext()) return ret(KE_CAN_NOT_WAIT);
    cur().waitSize = size;
    v.waiters.push_back(g_current);
    blockCurrent(TSW_VPL, it->first);
}
void vpAllocateVpl() { vpAllocate(true); }
void vpPAllocateVpl() { vpAllocate(false); }

void vpFreeVpl() {
    auto it = g_vpls.find(sarg(0));
    if (it == g_vpls.end()) return ret(KE_UNKNOWN_VPLID);
    Vpl& v = it->second;
    if (!v.used.erase(arg(1))) return ret(KE_ILLEGAL_MEMBLOCK);
    for (size_t i = 0; i < v.waiters.size();) {
        const int w = v.waiters[i];
        if (const uint32_t a = vplAlloc(v, thread(w)->waitSize)) {
            v.waiters.erase(v.waiters.begin() + static_cast<std::ptrdiff_t>(i));
            wakeThread(w, static_cast<int32_t>(a));
        } else {
            ++i;
        }
    }
    ret(KE_OK);
}

void vpReferVplStatus() {
    auto it = g_vpls.find(sarg(0));
    if (it == g_vpls.end()) return ret(KE_UNKNOWN_VPLID);
    const Vpl& v = it->second;
    uint32_t used = 0;
    for (const auto& [a, s] : v.used) used += s;
    const uint32_t info = arg(1);
    mem::fill(info, 0, 32);
    mem::write32(info + 0, v.attr);
    mem::write32(info + 4, v.option);
    mem::write32(info + 8, v.size);
    mem::write32(info + 12, v.size - used);
    mem::write32(info + 16, static_cast<uint32_t>(v.waiters.size()));
    ret(KE_OK);
}

// ---- intrman -------------------------------------------------------------------------------------------------
void inRegisterIntrHandler() {
    const int line = sarg(0);
    if (line < 0 || line >= 64) return ret(KE_ILLEGAL_INTRCODE);
    if (line == 0 || line == 11) return ret(KE_FOUND_HANDLER);   // owned by the vblank service
    IntrHandler& h = g_intr[static_cast<size_t>(line)];
    if (h.used) return ret(KE_FOUND_HANDLER);
    h = {true, arg(2), arg(3), g_cpu.gpr[R_GP], sarg(1)};
    ret(KE_OK);
}

void inReleaseIntrHandler() {
    const int line = sarg(0);
    if (line < 0 || line >= 64) return ret(KE_ILLEGAL_INTRCODE);
    if (!g_intr[static_cast<size_t>(line)].used) return ret(KE_NOTFOUND_HANDLER);
    g_intr[static_cast<size_t>(line)] = IntrHandler{};
    ret(KE_OK);
}

void inEnableIntr() {
    const int line = sarg(0) & 0xFF;
    if (line >= 64) return ret(KE_ILLEGAL_INTRCODE);
    g_intrMask |= 1ull << line;
    ret(KE_OK);
}

void inDisableIntr() {
    const int line = sarg(0) & 0xFF;
    if (line >= 64) return ret(KE_ILLEGAL_INTRCODE);
    const bool was = (g_intrMask >> line) & 1;
    g_intrMask &= ~(1ull << line);
    if (arg(1)) mem::write32(arg(1), was ? static_cast<uint32_t>(line) : 0);
    ret(was ? KE_OK : KE_INTRDISABLE);
}

void inCpuDisableIntr() {
    const bool was = g_intrEnabled;
    g_intrEnabled = false;
    ret(was ? KE_OK : KE_CPUDI);
}
void inCpuEnableIntr() {
    g_intrEnabled = true;
    ret(KE_OK);
}
void inCpuSuspendIntr() {
    const bool was = g_intrEnabled;
    g_intrEnabled = false;
    if (arg(0)) mem::write32(arg(0), was ? 0x404u : 0x400u);   // the Status bits the real kernel saves
    ret(was ? KE_OK : KE_CPUDI);
}
void inCpuResumeIntr() {
    g_intrEnabled = (arg(0) & 4) != 0;
    ret(KE_OK);
}
void inQueryIntrContext() { ret(interruptContext() ? 1 : 0); }
void inQueryIntrStack() {
    const uint32_t sp = arg(0);
    ret(sp >= kInterruptStackTop - 0x1000 && sp <= kInterruptStackTop ? 1 : 0);
}
void inZero() { ret(0); }

// ---- vblank -----------------------------------------------------------------------------------------------------
void vbWait(uint32_t bit) {
    if (interruptContext()) return ret(KE_CAN_NOT_WAIT);
    EventFlag& e = g_evfs[g_vblankEf];
    if (e.bits & bit) return ret(KE_OK);
    Thread& t = cur();
    t.waitBits = bit;
    t.waitMode = static_cast<int>(WEF_OR);
    t.waitResult = 0;
    e.waiters.push_back(g_current);
    blockCurrent(TSW_EVENTFLAG, g_vblankEf);
}
void vbWaitVblankStart() { vbWait(EF_VBLANK_START); }
void vbWaitVblankEnd() { vbWait(EF_VBLANK_END); }
void vbWaitVblank() { vbWait(EF_VBLANK); }
void vbWaitNonVblank() { vbWait(EF_NON_VBLANK); }

void vbRegisterVblankHandler() {
    const int edge = sarg(0) ? 1 : 0;
    for (const auto& h : g_vblankHandlers)
        if (h.edge == edge && h.fn == arg(2)) return ret(KE_FOUND_HANDLER);
    if (g_vblankHandlers.size() >= 16) return ret(KE_NO_MEMORY);
    VblankHandler h{edge, sarg(1), arg(2), arg(3), g_cpu.gpr[R_GP]};
    auto pos = std::find_if(g_vblankHandlers.begin(), g_vblankHandlers.end(),
                            [&](const VblankHandler& o) { return o.priority > h.priority; });
    g_vblankHandlers.insert(pos, h);
    ret(KE_OK);
}

void vbReleaseVblankHandler() {
    const int edge = sarg(0) ? 1 : 0;
    for (auto it = g_vblankHandlers.begin(); it != g_vblankHandlers.end(); ++it) {
        if (it->edge == edge && it->fn == arg(1)) {
            g_vblankHandlers.erase(it);
            return ret(KE_OK);
        }
    }
    ret(KE_NOTFOUND_HANDLER);
}

void vblankInterrupt(bool start) {
    const int edge = start ? 0 : 1;
    for (size_t i = 0; i < g_vblankHandlers.size();) {
        const VblankHandler h = g_vblankHandlers[i];
        if (h.edge != edge) {
            ++i;
            continue;
        }
        if (callHandler(h.fn, h.arg, h.gp) == 0) {
            auto it = std::find_if(g_vblankHandlers.begin(), g_vblankHandlers.end(),
                                   [&](const VblankHandler& o) { return o.edge == h.edge && o.fn == h.fn; });
            if (it != g_vblankHandlers.end()) g_vblankHandlers.erase(it);
            continue;
        }
        ++i;
    }
    // The bits are pulses: setting them releases whoever waits now, and they
    // are cleared again at once (as the vblank module does), so a wait that
    // starts later lasts until the next edge.
    const uint32_t bits = start ? EF_VBLANK_START | EF_VBLANK : EF_VBLANK_END | EF_NON_VBLANK;
    setEventFlag(g_vblankEf, bits);
    clearEventFlag(g_vblankEf, ~bits);
}

void deliverInterrupts() {
    if (!g_intrEnabled || !g_ictrl || interruptContext()) return;
    for (uint64_t pending = g_intrPending & g_intrMask; pending; pending = g_intrPending & g_intrMask) {
        const int line = std::countr_zero(pending);
        g_intrPending &= ~(1ull << line);
        if (line == 0 || line == 11) {
            vblankInterrupt(line == 0);
            continue;
        }
        IntrHandler& h = g_intr[static_cast<size_t>(line)];
        if (!h.used) continue;
        g_intrMask &= ~(1ull << line);   // masked while the handler runs; it re-enables by returning non-zero
        if (callHandler(h.fn, h.arg, h.gp) != 0) g_intrMask |= 1ull << line;
    }
}

void threadReturned() {
    if (g_current < 0) return;
    exitCurrent(false);
}

} // namespace

// ---- public API ------------------------------------------------------------------------------------------------
int64_t runSlice(int64_t budget) {
    int64_t used = 0;
    while (used < budget) {
        deliverInterrupts();
        if (g_resched || g_current < 0) schedule();
        if (g_current < 0) break;
        g_stop = false;
        used += execute(std::min<int64_t>(budget - used, 2048));
    }
    return used;
}

bool running() { return g_current >= 0 && !g_stop; }

bool hasReadyThread() {
    if (g_current >= 0) return true;
    if (g_intrEnabled && (g_intrPending & g_intrMask)) return true;
    for (size_t i = 1; i < g_threads.size(); ++i)
        if (g_threads[i].used && g_threads[i].status == THS_READY) return true;
    return false;
}

uint64_t systemTime() { return nowCycles() / kEeCyclesPerIopCycle; }

int currentThread() { return g_current; }
bool inInterrupt() { return interruptContext(); }

void blockCurrent(int waitType, int waitId) {
    if (g_current < 0 || interruptContext()) {
        PS2_ERROR("iop", "a blocking call was made outside a thread (interrupt context or idle)");
        return;
    }
    Thread& t = cur();
    t.waitType = waitType;
    t.waitId = waitId;
    leaveCurrent(THS_WAIT);
}

void wakeThread(int id, int32_t result) {
    Thread* t = thread(id);
    if (!t || (t->status != THS_WAIT && t->status != THS_WAITSUSPEND)) return;
    if (t->delayEvent) {
        cancelEvent(t->delayEvent);
        t->delayEvent = 0;
    }
    t->ctx.gpr[R_V0] = static_cast<uint32_t>(result);
    t->waitType = TSW_NONE;
    t->waitId = 0;
    if (t->status == THS_WAITSUSPEND) t->status = THS_SUSPEND;
    else makeReady(*t);
}

void wakeupThread(int id) {
    Thread* t = thread(id);
    if (!t || t->status == THS_DORMANT) return;
    if ((t->status == THS_WAIT || t->status == THS_WAITSUSPEND) && t->waitType == TSW_SLEEP) wakeThread(id, KE_OK);
    else ++t->wakeupCount;
}

int spawnThread(uint32_t entry, int priority, uint32_t stackSize, uint32_t gp, uint32_t returnTo, uint32_t a0, uint32_t a1,
                uint32_t a2, uint32_t a3) {
    const int id = newThread(entry, priority, stackSize, 0x02000000u, 0, gp);
    if (id < 0) return id;
    Thread& t = g_threads[static_cast<size_t>(id)];
    startThreadWith(id, a0, a1, t.stack + t.stackSize - kRegContextSize, returnTo);
    t.ctx.gpr[R_A2] = a2;
    t.ctx.gpr[R_A3] = a3;
    return id;
}

void exitCurrent(bool deleteThread) {
    if (g_current < 0) return;
    const int id = g_current;
    leaveCurrent(THS_DORMANT);
    if (deleteThread) {
        Thread& t = g_threads[static_cast<size_t>(id)];
        freeMemory(t.stack);
        t = Thread{};
    }
}

uint32_t callHandler(uint32_t fn, uint32_t arg0, uint32_t gp, uint32_t a1, uint32_t a2) {
    return callGuest(fn, arg0, a1, a2, 0, gp, interruptStack());
}

uint32_t allocMemory(uint32_t size, int mode, uint32_t addr) {
    size = std::max<uint32_t>((size + 255) & ~255u, 256);
    const std::vector<Block> gaps = freeGaps();
    uint32_t at = 0;
    if (mode == 2) {
        const uint32_t want = (addr & 0x1FFFFFFFu) & ~255u;
        for (const Block& g : gaps)
            if (want >= g.addr && want + size <= g.addr + g.size) at = want;
    } else if (mode == 1) {
        for (auto it = gaps.rbegin(); it != gaps.rend(); ++it)
            if (it->size >= size) {
                at = it->addr + it->size - size;
                break;
            }
    } else {
        for (const Block& g : gaps)
            if (g.size >= size) {
                at = g.addr;
                break;
            }
    }
    if (!at) return 0;
    insertBlock(at, size);
    return at;
}

int freeMemory(uint32_t addr) {
    const uint32_t a = addr & 0x1FFFFFFFu;
    for (auto it = g_blocks.begin(); it != g_blocks.end(); ++it) {
        if (it->addr == a) {
            g_blocks.erase(it);
            return KE_OK;
        }
    }
    return KE_ERROR;
}

uint32_t freeMemorySize() {
    uint32_t total = 0;
    for (const Block& g : freeGaps()) total += g.size;
    return total;
}

int createEventFlag(uint32_t attr, uint32_t initBits) {
    EventFlag e;
    e.attr = attr;
    e.initBits = e.bits = initBits;
    const int id = g_nextId++;
    g_evfs[id] = std::move(e);
    return id;
}

void setEventFlag(int id, uint32_t bits) {
    auto it = g_evfs.find(id);
    if (it == g_evfs.end()) return;
    it->second.bits |= bits;
    evfRelease(it->second);
}

void clearEventFlag(int id, uint32_t mask) {
    auto it = g_evfs.find(id);
    if (it != g_evfs.end()) it->second.bits &= mask;
}

void raiseInterrupt(int line) {
    if (line >= 0 && line < 64) g_intrPending |= 1ull << line;
}

bool registerInterrupt(int line, uint32_t fn, uint32_t arg) {
    if (line <= 0 || line >= 64 || line == 11) return false;
    IntrHandler& h = g_intr[static_cast<size_t>(line)];
    if (h.used) return false;
    h = {true, fn, arg, 0, 0};
    return true;
}

void releaseInterrupt(int line) {
    if (line >= 0 && line < 64) g_intr[static_cast<size_t>(line)] = IntrHandler{};
}

void enableInterrupt(int line, bool on) {
    if (line < 0 || line >= 64) return;
    if (on) g_intrMask |= 1ull << line;
    else g_intrMask &= ~(1ull << line);
}

void onVblank(bool start) { raiseInterrupt(start ? 0 : 11); }

uint32_t intcRead(uint32_t phys) {
    switch (phys & 0xF) {
    case 0x0: return static_cast<uint32_t>(g_intrPending);
    case 0x4: return static_cast<uint32_t>(g_intrMask);
    case 0x8: {   // I_CTRL: reading disables, returns the previous state
        const uint32_t v = g_ictrl ? 1 : 0;
        g_ictrl = false;
        return v;
    }
    default: return 0;
    }
}

void intcWrite(uint32_t phys, uint32_t v) {
    switch (phys & 0xF) {
    case 0x0: g_intrPending &= (0xFFFFFFFF00000000ull | v); break;   // write 0 to acknowledge
    case 0x4: g_intrMask = (g_intrMask & 0xFFFFFFFF00000000ull) | v; break;
    case 0x8: g_ictrl = (v & 1) != 0; break;
    default: break;
    }
}

void setStatusRegister(uint32_t sr) {
    g_cpu.sr = sr;
    g_intrEnabled = (sr & 1) != 0 || (sr & 4) != 0;
}

void syscallInstruction(uint32_t pc, uint32_t insn) {
    PS2_UNIMPL("iop", "IOP SYSCALL instruction", "SYSCALL 0x%x at 0x%08x in IOP code (kernel calls go through imports here)",
               (insn >> 6) & 0xFFFFF, pc);
}

void faultCurrent() {
    if (g_current < 0) return;
    PS2_ERROR("iop", "IOP thread %d stopped after a fault at 0x%08x\n%s", g_current, g_cpu.pc, describeThreads().c_str());
    exitCurrent(false);
}

std::string describeThreads() {
    std::string s;
    for (size_t i = 1; i < g_threads.size(); ++i) {
        const Thread& t = g_threads[i];
        if (!t.used) continue;
        char buf[200];
        std::snprintf(buf, sizeof buf, "  IOP thread %zu: status %d prio %d wait %d/%d pc 0x%08x entry 0x%08x\n", i, t.status,
                      t.priority, t.waitType, t.waitId, static_cast<int>(i) == g_current ? g_cpu.pc : t.ctx.pc, t.entry);
        s += buf;
    }
    return s;
}

void init() {
    g_threads.assign(1, Thread{});
    g_semas.clear();
    g_evfs.clear();
    g_mbxs.clear();
    g_fpls.clear();
    g_vpls.clear();
    for (auto& a : g_alarms) cancelEvent(a.second.event);
    g_alarms.clear();
    g_nextId = 1;
    g_current = -1;
    g_stop = g_resched = false;
    g_readyCounter = g_frontCounter = 0;
    g_intr.fill(IntrHandler{});
    g_intrPending = 0;
    g_intrMask = (1ull << 0) | (1ull << 11);   // vblank start/end, enabled by the vblank service at boot
    g_intrEnabled = g_ictrl = true;
    g_vblankHandlers.clear();
    g_blocks.clear();
    g_threadExitTrap = addTrap(&threadReturned, "(thread entry returned)");
    g_vblankEf = createEventFlag(EA_MULTI, 0);
    g_systemStatusEf = createEventFlag(EA_MULTI, 0xFFFFFFFFu);

    defineLibrary("sysmem", 0x0101, {
        {3, "GetSysmemInternalData", &smGetSysmemInternalData},
        {4, "AllocSysMemory", &smAllocSysMemory},
        {5, "FreeSysMemory", &smFreeSysMemory},
        {6, "QueryMemSize", &smQueryMemSize},
        {7, "QueryMaxFreeMemSize", &smQueryMaxFreeMemSize},
        {8, "QueryTotalFreeMemSize", &smQueryTotalFreeMemSize},
        {9, "QueryBlockTopAddress", &smQueryBlockTopAddress},
        {10, "QueryBlockSize", &smQueryBlockSize},
        {11, "GetSysMemoryInfo", nullptr},
        {14, "Kprintf", &smKprintf},
        {15, "KprintfSet", &inZero},
    });
    defineLibrary("thbase", 0x0101, {
        {3, "GetThreadmanData", &thGetThreadmanData},
        {4, "CreateThread", &thCreateThread},
        {5, "DeleteThread", &thDeleteThread},
        {6, "StartThread", &thStartThread},
        {7, "StartThreadArgs", &thStartThreadArgs},
        {8, "ExitThread", &thExitThread},
        {9, "ExitDeleteThread", &thExitDeleteThread},
        {10, "TerminateThread", &thTerminateThread},
        {11, "iTerminateThread", &thTerminateThread},
        {12, "DisableDispatchThread", &thDisableDispatchThread},
        {13, "EnableDispatchThread", &thEnableDispatchThread},
        {14, "ChangeThreadPriority", &thChangeThreadPriority},
        {15, "iChangeThreadPriority", &thChangeThreadPriority},
        {16, "RotateThreadReadyQueue", &thRotateThreadReadyQueue},
        {17, "iRotateThreadReadyQueue", &thRotateThreadReadyQueue},
        {18, "ReleaseWaitThread", &thReleaseWaitThread},
        {19, "iReleaseWaitThread", &thReleaseWaitThread},
        {20, "GetThreadId", &thGetThreadId},
        {21, "CheckThreadStack", &thCheckThreadStack},
        {22, "ReferThreadStatus", &thReferThreadStatus},
        {23, "iReferThreadStatus", &thReferThreadStatus},
        {24, "SleepThread", &thSleepThread},
        {25, "WakeupThread", &thWakeupThread},
        {26, "iWakeupThread", &thWakeupThread},
        {27, "CancelWakeupThread", &thCancelWakeupThread},
        {28, "iCancelWakeupThread", &thCancelWakeupThread},
        {29, "SuspendThread", &thSuspendThread},
        {30, "iSuspendThread", &thSuspendThread},
        {31, "ResumeThread", &thResumeThread},
        {32, "iResumeThread", &thResumeThread},
        {33, "DelayThread", &thDelayThread},
        {34, "GetSystemTime", &thGetSystemTime},
        {35, "SetAlarm", &thSetAlarm},
        {36, "iSetAlarm", &thSetAlarm},
        {37, "CancelAlarm", &thCancelAlarm},
        {38, "iCancelAlarm", &thCancelAlarm},
        {39, "USec2SysClock", &thUSec2SysClock},
        {40, "SysClock2USec", &thSysClock2USec},
        {41, "GetSystemStatusFlag", &thGetSystemStatusFlag},
        {42, "GetThreadCurrentPriority", &thGetThreadCurrentPriority},
        {43, "GetSystemTimeLow", &thGetSystemTimeLow},
        {44, "ReferSystemStatus", nullptr},
        {45, "ReferThreadRunStatus", nullptr},
        {46, "GetThreadStackFreeSize", &thGetThreadStackFreeSize},
        {47, "GetThreadmanIdList", &thGetThreadmanIdList},
    });
    defineLibrary("thsemap", 0x0102, {
        {4, "CreateSema", &smCreateSema},
        {5, "DeleteSema", &smDeleteSema},
        {6, "SignalSema", &smSignalSema},
        {7, "iSignalSema", &smSignalSema},
        {8, "WaitSema", &smWaitSema},
        {9, "PollSema", &smPollSema},
        {11, "ReferSemaStatus", &smReferSemaStatus},
        {12, "iReferSemaStatus", &smReferSemaStatus},
    });
    defineLibrary("thevent", 0x0101, {
        {4, "CreateEventFlag", &evCreateEventFlag},
        {5, "DeleteEventFlag", &evDeleteEventFlag},
        {6, "SetEventFlag", &evSetEventFlag},
        {7, "iSetEventFlag", &evSetEventFlag},
        {8, "ClearEventFlag", &evClearEventFlag},
        {9, "iClearEventFlag", &evClearEventFlag},
        {10, "WaitEventFlag", &evWaitEventFlag},
        {11, "PollEventFlag", &evPollEventFlag},
        {13, "ReferEventFlagStatus", &evReferEventFlagStatus},
        {14, "iReferEventFlagStatus", &evReferEventFlagStatus},
    });
    defineLibrary("thmsgbx", 0x0101, {
        {4, "CreateMbx", &mbCreateMbx},
        {5, "DeleteMbx", &mbDeleteMbx},
        {6, "SendMbx", &mbSendMbx},
        {7, "iSendMbx", &mbSendMbx},
        {8, "ReceiveMbx", &mbReceiveMbx},
        {9, "PollMbx", &mbPollMbx},
        {11, "ReferMbxStatus", &mbReferMbxStatus},
        {12, "iReferMbxStatus", &mbReferMbxStatus},
    });
    defineLibrary("thfpool", 0x0101, {
        {4, "CreateFpl", &fpCreateFpl},
        {5, "DeleteFpl", &fpDeleteFpl},
        {6, "AllocateFpl", &fpAllocateFpl},
        {7, "pAllocateFpl", &fpPAllocateFpl},
        {8, "ipAllocateFpl", &fpPAllocateFpl},
        {9, "FreeFpl", &fpFreeFpl},
        {11, "ReferFplStatus", &fpReferFplStatus},
        {12, "iReferFplStatus", &fpReferFplStatus},
    });
    defineLibrary("thvpool", 0x0101, {
        {4, "CreateVpl", &vpCreateVpl},
        {5, "DeleteVpl", &vpDeleteVpl},
        {6, "AllocateVpl", &vpAllocateVpl},
        {7, "pAllocateVpl", &vpPAllocateVpl},
        {8, "ipAllocateVpl", &vpPAllocateVpl},
        {9, "FreeVpl", &vpFreeVpl},
        {11, "ReferVplStatus", &vpReferVplStatus},
        {12, "iReferVplStatus", &vpReferVplStatus},
    });
    defineLibrary("intrman", 0x0102, {
        {3, "GetIntrmanInternalData", nullptr},
        {4, "RegisterIntrHandler", &inRegisterIntrHandler},
        {5, "ReleaseIntrHandler", &inReleaseIntrHandler},
        {6, "EnableIntr", &inEnableIntr},
        {7, "DisableIntr", &inDisableIntr},
        {8, "CpuDisableIntr", &inCpuDisableIntr},
        {9, "CpuEnableIntr", &inCpuEnableIntr},
        {14, "CpuInvokeInKmode", nullptr},   // guest thunk, see iop.cpp
        {15, "DisableDispatchIntr", &inZero},
        {16, "EnableDispatchIntr", &inZero},
        {17, "CpuSuspendIntr", &inCpuSuspendIntr},
        {18, "CpuResumeIntr", &inCpuResumeIntr},
        {23, "QueryIntrContext", &inQueryIntrContext},
        {24, "QueryIntrStack", &inQueryIntrStack},
        {25, "iCatchMultiIntr", &inZero},
        {28, "SetNewCtxCb", &inZero},
        {30, "SetShouldPreemptCb", &inZero},
    });
    defineLibrary("vblank", 0x0101, {
        {4, "WaitVblankStart", &vbWaitVblankStart},
        {5, "WaitVblankEnd", &vbWaitVblankEnd},
        {6, "WaitVblank", &vbWaitVblank},
        {7, "WaitNonVblank", &vbWaitNonVblank},
        {8, "RegisterVblankHandler", &vbRegisterVblankHandler},
        {9, "ReleaseVblankHandler", &vbReleaseVblankHandler},
    });
}

} // namespace ps2::iop::kernel
