// IOP kernel libraries that are plain functions over IOP memory: sysclib,
// stdio, ioman and iomanX (host files), heaplib and timrman, plus the
// libraries whose calls only matter to real hardware (dmacman, ssbusc,
// excepman, secrman), which succeed without doing anything.
#include "iop/libs.h"

#include "clock.h"
#include "iop/cpu.h"
#include "iop/hle.h"
#include "iop/hostfs.h"
#include "iop/kernel.h"
#include "log.h"

#include <array>
#include <cctype>
#include <cstring>
#include <ctime>
#include <map>
#include <string>

namespace ps2::iop::libs {

using namespace hle;

namespace {

constexpr int32_t KE_OK = 0, KE_ERROR = -1, KE_NO_TIMER = -150, KE_ILLEGAL_TIMERID = -151;

// ---- sysclib ----------------------------------------------------------------------------------
uint32_t g_ctypeTable = 0;   // 256 entries of BSD ctype bits, in IOP memory
uint32_t g_strtokNext = 0;

uint8_t ctypeBits(int c) {
    uint8_t b = 0;
    if (c < 0 || c > 127) return 0;
    if (std::isupper(c)) b |= 0x01;
    if (std::islower(c)) b |= 0x02;
    if (std::isdigit(c)) b |= 0x04;
    if (std::isspace(c)) b |= 0x08;
    if (std::ispunct(c)) b |= 0x10;
    if (std::iscntrl(c)) b |= 0x20;
    if (std::isxdigit(c)) b |= 0x40;
    if (c == ' ') b |= 0x80;
    return b;
}

uint32_t strLen(uint32_t s) {
    uint32_t n = 0;
    while (mem::read8(s + n)) ++n;
    return n;
}

void copyGuest(uint32_t dst, uint32_t src, uint32_t n) {
    if (!n) return;
    std::string tmp(n, '\0');
    mem::readBlock(src, tmp.data(), n);
    mem::writeBlock(dst, tmp.data(), n);
}

void scSetjmp() {
    const uint32_t b = arg(0);
    static constexpr uint32_t regs[12] = {R_RA, R_SP, R_FP, 16, 17, 18, 19, 20, 21, 22, 23, R_GP};
    for (int i = 0; i < 12; ++i) mem::write32(b + 4 * static_cast<uint32_t>(i), g_cpu.gpr[regs[i]]);
    ret(0);
}

void scLongjmp() {
    const uint32_t b = arg(0);
    const uint32_t v = arg(1);
    static constexpr uint32_t regs[12] = {R_RA, R_SP, R_FP, 16, 17, 18, 19, 20, 21, 22, 23, R_GP};
    for (int i = 0; i < 12; ++i) g_cpu.gpr[regs[i]] = mem::read32(b + 4 * static_cast<uint32_t>(i));
    g_cpu.pc = g_cpu.gpr[R_RA];
    g_cpu.npc = g_cpu.pc + 4;
    retU(v ? v : 1);
}

void scToupper() { ret(std::toupper(sarg(0) & 0xFF)); }
void scTolower() { ret(std::tolower(sarg(0) & 0xFF)); }
void scLookCtypeTable() { ret(ctypeBits(sarg(0) & 0xFF)); }
void scGetCtypeTable() { retU(g_ctypeTable); }

void scMemchr() {
    const uint32_t s = arg(0), n = arg(2);
    const auto c = static_cast<uint8_t>(arg(1));
    for (uint32_t i = 0; i < n; ++i)
        if (mem::read8(s + i) == c) return retU(s + i);
    retU(0);
}
void scMemcmp() {
    for (uint32_t i = 0; i < arg(2); ++i) {
        const int d = mem::read8(arg(0) + i) - mem::read8(arg(1) + i);
        if (d) return ret(d);
    }
    ret(0);
}
void scMemcpy() {
    copyGuest(arg(0), arg(1), arg(2));
    retU(arg(0));
}
void scMemset() {
    mem::fill(arg(0), static_cast<uint8_t>(arg(1)), arg(2));
    retU(arg(0));
}
void scBcopy() {
    copyGuest(arg(1), arg(0), arg(2));
    ret(0);
}
void scBzero() {
    mem::fill(arg(0), 0, arg(1));
    ret(0);
}

void writeString(uint32_t dst, const std::string& s) { mem::writeBlock(dst, s.c_str(), static_cast<uint32_t>(s.size()) + 1); }

void scSprintf() {
    IopArgs args(2);
    const std::string s = formatPrintf(mem::readString(arg(1), 4096), args);
    writeString(arg(0), s);
    ret(static_cast<int32_t>(s.size()));
}
void scVsprintf() {
    IopArgs args = IopArgs::fromVaList(arg(2));
    const std::string s = formatPrintf(mem::readString(arg(1), 4096), args);
    writeString(arg(0), s);
    ret(static_cast<int32_t>(s.size()));
}
void scPrnt() {
    // prnt(callback, context, format, va_list): the callback gets each character.
    const uint32_t fn = arg(0), ctx = arg(1);
    IopArgs args = IopArgs::fromVaList(arg(3));
    const std::string s = formatPrintf(mem::readString(arg(2), 4096), args);
    const uint32_t gp = g_cpu.gpr[R_GP];
    for (const char c : s) callGuest(fn, ctx, static_cast<uint8_t>(c), 0, 0, gp);
    ret(static_cast<int32_t>(s.size()));
}

void scStrcat() {
    copyGuest(arg(0) + strLen(arg(0)), arg(1), strLen(arg(1)) + 1);
    retU(arg(0));
}
void scStrncat() {
    const uint32_t d = arg(0) + strLen(arg(0));
    const uint32_t n = std::min(strLen(arg(1)), arg(2));
    copyGuest(d, arg(1), n);
    mem::write8(d + n, 0);
    retU(arg(0));
}
void scStrchr() {
    const auto c = static_cast<uint8_t>(arg(1));
    for (uint32_t p = arg(0);; ++p) {
        const uint8_t v = mem::read8(p);
        if (v == c) return retU(p);
        if (!v) return retU(0);
    }
}
void scStrrchr() {
    const auto c = static_cast<uint8_t>(arg(1));
    uint32_t found = 0;
    for (uint32_t p = arg(0);; ++p) {
        const uint8_t v = mem::read8(p);
        if (v == c) found = p;
        if (!v) return retU(found);
    }
}
void scStrcmp() {
    for (uint32_t i = 0;; ++i) {
        const uint8_t a = mem::read8(arg(0) + i), b = mem::read8(arg(1) + i);
        if (a != b || !a) return ret(a - b);
    }
}
void scStrncmp() {
    for (uint32_t i = 0; i < arg(2); ++i) {
        const uint8_t a = mem::read8(arg(0) + i), b = mem::read8(arg(1) + i);
        if (a != b || !a) return ret(a - b);
    }
    ret(0);
}
void scStrcpy() {
    copyGuest(arg(0), arg(1), strLen(arg(1)) + 1);
    retU(arg(0));
}
void scStrncpy() {
    const uint32_t n = arg(2), len = std::min(strLen(arg(1)), n);
    copyGuest(arg(0), arg(1), len);
    mem::fill(arg(0) + len, 0, n - len);
    retU(arg(0));
}
void scStrlen() { retU(strLen(arg(0))); }

uint32_t spanOf(uint32_t s, const std::string& set, bool accept) {
    uint32_t n = 0;
    for (;; ++n) {
        const char c = static_cast<char>(mem::read8(s + n));
        if (!c || (set.find(c) != std::string::npos) != accept) return n;
    }
}
void scStrspn() { retU(spanOf(arg(0), mem::readString(arg(1)), true)); }
void scStrcspn() { retU(spanOf(arg(0), mem::readString(arg(1)), false)); }
void scStrpbrk() {
    const uint32_t n = spanOf(arg(0), mem::readString(arg(1)), false);
    retU(mem::read8(arg(0) + n) ? arg(0) + n : 0);
}
void scStrstr() {
    const std::string hay = mem::readString(arg(0), 1 << 20), needle = mem::readString(arg(1));
    const size_t at = hay.find(needle);
    retU(at == std::string::npos ? 0 : arg(0) + static_cast<uint32_t>(at));
}

uint32_t tokenize(uint32_t s, uint32_t delim, uint32_t& next) {
    const std::string set = mem::readString(delim);
    if (!s) s = next;
    if (!s) return 0;
    s += spanOf(s, set, true);
    if (!mem::read8(s)) {
        next = 0;
        return 0;
    }
    const uint32_t end = s + spanOf(s, set, false);
    if (mem::read8(end)) {
        mem::write8(end, 0);
        next = end + 1;
    } else {
        next = 0;
    }
    return s;
}
void scStrtok() { retU(tokenize(arg(0), arg(1), g_strtokNext)); }
void scStrtokR() {
    uint32_t next = arg(2) ? mem::read32(arg(2)) : 0;
    const uint32_t tok = tokenize(arg(0), arg(1), next);
    if (arg(2)) mem::write32(arg(2), next);
    retU(tok);
}

void scStrtol(bool isUnsigned) {
    const std::string s = mem::readString(arg(0), 256);
    char* end = nullptr;
    const long long v = isUnsigned ? static_cast<long long>(std::strtoull(s.c_str(), &end, sarg(2))) : std::strtoll(s.c_str(), &end, sarg(2));
    if (arg(1)) mem::write32(arg(1), arg(0) + static_cast<uint32_t>(end - s.c_str()));
    retU(static_cast<uint32_t>(v));
}
void scStrtolS() { scStrtol(false); }
void scStrtolU() { scStrtol(true); }
void scAtob() {
    const std::string s = mem::readString(arg(0), 256);
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (arg(1)) mem::write32(arg(1), static_cast<uint32_t>(v));
    retU(arg(0) + static_cast<uint32_t>(end - s.c_str()));
}
void scWmemcopy() {
    copyGuest(arg(0), arg(1), arg(2) & ~3u);
    retU(arg(0));
}
void scWmemset() {
    for (uint32_t i = 0; i + 4 <= arg(2); i += 4) mem::write32(arg(0) + i, arg(1));
    retU(arg(0));
}

// ---- stdio ----------------------------------------------------------------------------------------
void stPrintf() {
    const std::string s = formatIop(arg(0), 1);
    hostPrint(s);
    ret(static_cast<int32_t>(s.size()));
}
void stPutchar() {
    hostPrint(std::string(1, static_cast<char>(arg(0))));
    ret(sarg(0) & 0xFF);
}
void stPuts() {
    hostPrint(mem::readString(arg(0), 4096) + "\n");
    ret(0);
}
void stEof() { ret(-1); }
void stFdprintf() {
    const std::string s = formatIop(arg(1), 2);
    hostPrint(s);
    ret(static_cast<int32_t>(s.size()));
}
void stFdputc() {
    hostPrint(std::string(1, static_cast<char>(arg(0))));
    ret(sarg(0) & 0xFF);
}
void stFdputs() {
    hostPrint(mem::readString(arg(0), 4096));
    ret(0);
}
void stVfdprintf() {
    IopArgs args = IopArgs::fromVaList(arg(2));
    const std::string s = formatPrintf(mem::readString(arg(1), 4096), args);
    hostPrint(s);
    ret(static_cast<int32_t>(s.size()));
}
void stZero() { ret(0); }

// ---- ioman / iomanX ----------------------------------------------------------------------------------
void writeTime(uint32_t at, int64_t t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    const uint8_t b[8] = {0, static_cast<uint8_t>(tm.tm_sec), static_cast<uint8_t>(tm.tm_min), static_cast<uint8_t>(tm.tm_hour),
                          static_cast<uint8_t>(tm.tm_mday), static_cast<uint8_t>(tm.tm_mon + 1),
                          static_cast<uint8_t>((tm.tm_year + 1900) & 0xFF), static_cast<uint8_t>((tm.tm_year + 1900) >> 8)};
    mem::writeBlock(at, b, 8);
}

// io_stat_t (ioman, 40 bytes) or iox_stat_t (iomanX, 64 bytes).
void writeStat(uint32_t at, const hostfs::Stat& st, bool iox) {
    mem::fill(at, 0, iox ? 64 : 40);
    const uint32_t mode = iox ? (st.dir ? 0x1000u : 0x2000u) | 0x01FFu   // FIO_S_IFDIR / FIO_S_IFREG | rwxrwxrwx
                              : (st.dir ? 0x0020u : 0x0010u) | 0x0007u;  // FIO_SO_IFDIR / FIO_SO_IFREG | rwx
    mem::write32(at + 0, mode);
    mem::write32(at + 8, static_cast<uint32_t>(st.size));
    for (uint32_t t : {12u, 20u, 28u}) writeTime(at + t, st.mtime);
    mem::write32(at + 36, static_cast<uint32_t>(st.size >> 32));
}

void ioOpen() { ret(hostfs::open(mem::readString(arg(0), 1024), sarg(1))); }
void ioClose() { ret(hostfs::close(sarg(0))); }
void ioRead() {
    const int n = sarg(2);
    if (n < 0) return ret(hostfs::E_INVAL);
    std::string buf(static_cast<size_t>(n), '\0');
    const int r = hostfs::read(sarg(0), buf.data(), n);
    if (r > 0) mem::writeBlock(arg(1), buf.data(), static_cast<uint32_t>(r));
    ret(r);
}
void ioWrite() {
    const int n = sarg(2);
    if (n < 0) return ret(hostfs::E_INVAL);
    std::string buf(static_cast<size_t>(n), '\0');
    mem::readBlock(arg(1), buf.data(), static_cast<uint32_t>(n));
    ret(hostfs::write(sarg(0), buf.data(), n));
}
void ioLseek() { ret(static_cast<int32_t>(hostfs::lseek(sarg(0), sarg(1), sarg(2)))); }
void ioLseek64() {
    // lseek64(fd, s64 offset, whence): the offset takes the aligned pair $a2/$a3.
    const auto off = static_cast<int64_t>(arg(2) | (static_cast<uint64_t>(arg(3)) << 32));
    const int64_t r = hostfs::lseek(sarg(0), off, sarg(4));
    g_cpu.gpr[R_V0] = static_cast<uint32_t>(r);
    g_cpu.gpr[R_V1] = static_cast<uint32_t>(static_cast<uint64_t>(r) >> 32);
}
void ioRemove() { ret(hostfs::remove(mem::readString(arg(0), 1024))); }
void ioMkdir() { ret(hostfs::mkdir(mem::readString(arg(0), 1024))); }
void ioRmdir() { ret(hostfs::rmdir(mem::readString(arg(0), 1024))); }
void ioRename() { ret(hostfs::rename(mem::readString(arg(0), 1024), mem::readString(arg(1), 1024))); }
void ioDopen() { ret(hostfs::dopen(mem::readString(arg(0), 1024))); }
void ioDread(bool iox) {
    std::string name;
    hostfs::Stat st;
    const int r = hostfs::dread(sarg(0), name, st);
    if (r > 0) {
        const uint32_t d = arg(1);
        const uint32_t nameAt = d + (iox ? 64 : 40);
        writeStat(d, st, iox);
        mem::fill(nameAt, 0, 256);
        name = name.substr(0, 255);
        mem::writeBlock(nameAt, name.c_str(), static_cast<uint32_t>(name.size()));
    }
    ret(r);
}
void ioDreadOld() { ioDread(false); }
void ioDreadX() { ioDread(true); }
void ioGetstat(bool iox) {
    hostfs::Stat st;
    const int r = hostfs::getstat(mem::readString(arg(0), 1024), st);
    if (r == 0) writeStat(arg(1), st, iox);
    ret(r);
}
void ioGetstatOld() { ioGetstat(false); }
void ioGetstatX() { ioGetstat(true); }
void ioZero() { ret(0); }
void ioAddDrv() {
    const uint32_t dev = arg(0);
    const std::string name = dev ? mem::readString(mem::read32(dev), 32) : std::string("?");
    PS2_UNIMPL("iop", "IOP device driver " + name,
               "an IOP module registered device \"%s:\"; device drivers in module code are not supported, the host "
               "devices stay in charge", name.c_str());
    ret(0);
}

// ---- heaplib ----------------------------------------------------------------------------------------------
struct Heap {
    uint32_t base = 0, size = 0;
    std::map<uint32_t, uint32_t> used;   // address -> size
};
std::map<uint32_t, Heap> g_heaps;   // handle (base address) -> heap

void hpCreateHeap() {
    const uint32_t size = (arg(0) + 15) & ~15u;
    const uint32_t base = kernel::allocMemory(size, (arg(1) & 2) ? 1 : 0);
    if (!base) return retU(0);
    g_heaps[base] = Heap{base, size, {}};
    retU(base);
}
void hpDeleteHeap() {
    auto it = g_heaps.find(arg(0));
    if (it == g_heaps.end()) return ret(KE_ERROR);
    kernel::freeMemory(it->second.base);
    g_heaps.erase(it);
    ret(KE_OK);
}
void hpAllocHeapMemory() {
    auto it = g_heaps.find(arg(0));
    if (it == g_heaps.end()) return retU(0);
    Heap& h = it->second;
    const uint32_t size = std::max<uint32_t>((arg(1) + 15) & ~15u, 16);
    uint32_t at = h.base;
    for (const auto& [a, s] : h.used) {
        if (a - at >= size) break;
        at = a + s;
    }
    if (at + size > h.base + h.size) return retU(0);
    h.used[at] = size;
    retU(at);
}
void hpFreeHeapMemory() {
    auto it = g_heaps.find(arg(0));
    if (it == g_heaps.end() || !it->second.used.erase(arg(1))) return ret(KE_ERROR);
    ret(KE_OK);
}
void hpHeapTotalFreeSize() {
    auto it = g_heaps.find(arg(0));
    if (it == g_heaps.end()) return ret(0);
    uint32_t used = 0;
    for (const auto& [a, s] : it->second.used) used += s;
    retU(it->second.size - used);
}

// ---- timrman ---------------------------------------------------------------------------------------------------
struct HardTimer {
    bool allocated = false, running = false;
    int bits = 16, line = 4;
    uint32_t sources = 1, maxPrescale = 1, prescale = 1, compare = 0;
    uint32_t handler = 0, handlerArg = 0, gp = 0;
    uint64_t event = 0;
    uint64_t base = 0;   // system time the count started at
};
std::array<HardTimer, 6> g_timers;

HardTimer* timerOf(int id) { return id >= 1 && id <= 6 && g_timers[static_cast<size_t>(id - 1)].allocated ? &g_timers[static_cast<size_t>(id - 1)] : nullptr; }

void timerFired(uint64_t idx) {
    HardTimer& t = g_timers[idx];
    t.event = 0;
    if (!t.running || !t.handler) return;
    const uint32_t next = kernel::callHandler(t.handler, t.handlerArg, t.gp);
    if (!next || !t.running) {
        t.running = false;
        return;
    }
    t.compare = next;
    t.base = kernel::systemTime();
    t.event = scheduleEvent(nowCycles() + static_cast<uint64_t>(next) * t.prescale * kEeCyclesPerIopCycle, &timerFired, idx);
}

void tmAllocHardTimer() {
    const uint32_t source = arg(0), bits = arg(1), prescale = arg(2);
    for (size_t i = 0; i < g_timers.size(); ++i) {
        HardTimer& t = g_timers[i];
        if (t.allocated || !(t.sources & source) || static_cast<uint32_t>(t.bits) != bits || t.maxPrescale < prescale) continue;
        t.allocated = true;
        t.running = false;
        t.handler = 0;
        t.prescale = std::max<uint32_t>(prescale, 1);
        return ret(static_cast<int32_t>(i + 1));
    }
    ret(KE_NO_TIMER);
}
void tmFreeHardTimer() {
    HardTimer* t = timerOf(sarg(0));
    if (!t) return ret(KE_ILLEGAL_TIMERID);
    if (t->event) cancelEvent(t->event);
    *t = HardTimer{false, false, t->bits, t->line, t->sources, t->maxPrescale};
    ret(KE_OK);
}
void tmSetTimerHandler() {
    HardTimer* t = timerOf(sarg(0));
    if (!t) return ret(KE_ILLEGAL_TIMERID);
    t->compare = arg(1);
    t->handler = arg(2);
    t->handlerArg = arg(3);
    t->gp = g_cpu.gpr[R_GP];
    ret(KE_OK);
}
void tmSetupHardTimer() {
    HardTimer* t = timerOf(sarg(0));
    if (!t) return ret(KE_ILLEGAL_TIMERID);
    t->prescale = std::max<uint32_t>(arg(3), 1);
    ret(KE_OK);
}
void tmStartHardTimer() {
    HardTimer* t = timerOf(sarg(0));
    if (!t) return ret(KE_ILLEGAL_TIMERID);
    t->running = true;
    t->base = kernel::systemTime();
    if (t->handler && t->compare) {
        const auto idx = static_cast<uint64_t>(t - g_timers.data());
        t->event = scheduleEvent(nowCycles() + static_cast<uint64_t>(t->compare) * t->prescale * kEeCyclesPerIopCycle, &timerFired, idx);
    }
    ret(KE_OK);
}
void tmStopHardTimer() {
    HardTimer* t = timerOf(sarg(0));
    if (!t) return ret(KE_ILLEGAL_TIMERID);
    t->running = false;
    if (t->event) cancelEvent(t->event);
    t->event = 0;
    ret(KE_OK);
}
void tmGetTimerCounter() {
    HardTimer* t = timerOf(sarg(0));
    if (!t) return ret(0);
    const uint64_t ticks = (kernel::systemTime() - t->base) / t->prescale;
    retU(t->bits == 16 ? static_cast<uint32_t>(ticks & 0xFFFF) : static_cast<uint32_t>(ticks));
}
void tmSetTimerCompare() {
    if (HardTimer* t = timerOf(sarg(0))) t->compare = arg(1);
    ret(KE_OK);
}
void tmGetTimerCompare() {
    HardTimer* t = timerOf(sarg(0));
    retU(t ? t->compare : 0);
}
void tmGetHardTimerIntrCode() {
    HardTimer* t = timerOf(sarg(0));
    ret(t ? t->line : KE_ILLEGAL_TIMERID);
}
void tmZero() { ret(0); }

void retZero() { ret(0); }

} // namespace

void init() {
    g_heaps.clear();
    g_strtokNext = 0;
    g_timers = {};
    static const int lines[6] = {4, 5, 6, 14, 15, 16};
    static const uint32_t sources[6] = {0xB, 0xD, 1, 5, 1, 1}, prescales[6] = {1, 1, 8, 1, 256, 256};
    for (size_t i = 0; i < 6; ++i) {
        g_timers[i].bits = i < 3 ? 16 : 32;
        g_timers[i].line = lines[i];
        g_timers[i].sources = sources[i];
        g_timers[i].maxPrescale = prescales[i];
    }
    g_ctypeTable = kernel::kKernelData;
    for (int c = 0; c < 256; ++c) mem::write8(g_ctypeTable + static_cast<uint32_t>(c), ctypeBits(c));

    defineLibrary("sysclib", 0x0101, {
        {4, "setjmp", &scSetjmp},
        {5, "longjmp", &scLongjmp},
        {6, "_toupper", &scToupper},
        {7, "_tolower", &scTolower},
        {8, "look_ctype_table", &scLookCtypeTable},
        {9, "get_ctype_table", &scGetCtypeTable},
        {10, "memchr", &scMemchr},
        {11, "memcmp", &scMemcmp},
        {12, "memcpy", &scMemcpy},
        {13, "memmove", &scMemcpy},
        {14, "memset", &scMemset},
        {15, "bcmp", &scMemcmp},
        {16, "bcopy", &scBcopy},
        {17, "bzero", &scBzero},
        {18, "prnt", &scPrnt},
        {19, "sprintf", &scSprintf},
        {20, "strcat", &scStrcat},
        {21, "strchr", &scStrchr},
        {22, "strcmp", &scStrcmp},
        {23, "strcpy", &scStrcpy},
        {24, "strcspn", &scStrcspn},
        {25, "index", &scStrchr},
        {26, "rindex", &scStrrchr},
        {27, "strlen", &scStrlen},
        {28, "strncat", &scStrncat},
        {29, "strncmp", &scStrncmp},
        {30, "strncpy", &scStrncpy},
        {31, "strpbrk", &scStrpbrk},
        {32, "strrchr", &scStrrchr},
        {33, "strspn", &scStrspn},
        {34, "strstr", &scStrstr},
        {35, "strtok", &scStrtok},
        {36, "strtol", &scStrtolS},
        {37, "atob", &scAtob},
        {38, "strtoul", &scStrtolU},
        {40, "_wmemcopy", &scWmemcopy},
        {41, "_wmemset", &scWmemset},
        {42, "vsprintf", &scVsprintf},
        {43, "strtok_r", &scStrtokR},
    });
    defineLibrary("stdio", 0x0102, {
        {4, "printf", &stPrintf},
        {5, "getchar", &stEof},
        {6, "putchar", &stPutchar},
        {7, "puts", &stPuts},
        {8, "gets", &stZero},
        {9, "fdprintf", &stFdprintf},
        {10, "fdgetc", &stEof},
        {11, "fdputc", &stFdputc},
        {12, "fdputs", &stFdputs},
        {13, "fdgets", &stZero},
        {14, "vfdprintf", &stVfdprintf},
    });
    defineLibrary("ioman", 0x0101, {
        {4, "io_open", &ioOpen},
        {5, "io_close", &ioClose},
        {6, "io_read", &ioRead},
        {7, "io_write", &ioWrite},
        {8, "io_lseek", &ioLseek},
        {9, "io_ioctl", nullptr},
        {10, "io_remove", &ioRemove},
        {11, "io_mkdir", &ioMkdir},
        {12, "io_rmdir", &ioRmdir},
        {13, "io_dopen", &ioDopen},
        {14, "io_dclose", &ioClose},
        {15, "io_dread", &ioDreadOld},
        {16, "io_getstat", &ioGetstatOld},
        {17, "io_chstat", &ioZero},
        {18, "io_format", &ioZero},
        {20, "io_AddDrv", &ioAddDrv},
        {21, "io_DelDrv", &ioZero},
    });
    defineLibrary("iomanx", 0x0101, {
        {3, "iomanX_GetDeviceList", &ioZero},
        {4, "iomanX_open", &ioOpen},
        {5, "iomanX_close", &ioClose},
        {6, "iomanX_read", &ioRead},
        {7, "iomanX_write", &ioWrite},
        {8, "iomanX_lseek", &ioLseek},
        {9, "iomanX_ioctl", nullptr},
        {10, "iomanX_remove", &ioRemove},
        {11, "iomanX_mkdir", &ioMkdir},
        {12, "iomanX_rmdir", &ioRmdir},
        {13, "iomanX_dopen", &ioDopen},
        {14, "iomanX_dclose", &ioClose},
        {15, "iomanX_dread", &ioDreadX},
        {16, "iomanX_getstat", &ioGetstatX},
        {17, "iomanX_chstat", &ioZero},
        {18, "iomanX_format", &ioZero},
        {20, "iomanX_AddDrv", &ioAddDrv},
        {21, "iomanX_DelDrv", &ioZero},
        {22, "iomanX_GetDevType", nullptr},
        {23, "iomanX_StdioInit", &ioZero},
        {25, "iomanX_rename", &ioRename},
        {26, "iomanX_chdir", &ioZero},
        {27, "iomanX_sync", &ioZero},
        {28, "iomanX_mount", nullptr},
        {29, "iomanX_umount", nullptr},
        {30, "iomanX_lseek64", &ioLseek64},
        {31, "iomanX_devctl", nullptr},
        {32, "iomanX_symlink", nullptr},
        {33, "iomanX_readlink", nullptr},
        {34, "iomanX_ioctl2", nullptr},
    });
    defineLibrary("heaplib", 0x0101, {
        {4, "CreateHeap", &hpCreateHeap},
        {5, "DeleteHeap", &hpDeleteHeap},
        {6, "AllocHeapMemory", &hpAllocHeapMemory},
        {7, "FreeHeapMemory", &hpFreeHeapMemory},
        {8, "HeapTotalFreeSize", &hpHeapTotalFreeSize},
        {11, "HeapPrepare", nullptr},
        {15, "HeapChunkSize", nullptr},
    });
    defineLibrary("timrman", 0x0101, {
        {3, "GetTimersTable", &tmZero},
        {4, "AllocHardTimer", &tmAllocHardTimer},
        {5, "ReferHardTimer", nullptr},
        {6, "FreeHardTimer", &tmFreeHardTimer},
        {7, "SetTimerMode", &tmZero},
        {8, "GetTimerStatus", &tmZero},
        {9, "SetTimerCounter", &tmZero},
        {10, "GetTimerCounter", &tmGetTimerCounter},
        {11, "SetTimerCompare", &tmSetTimerCompare},
        {12, "GetTimerCompare", &tmGetTimerCompare},
        {13, "SetHoldMode", &tmZero},
        {14, "GetHoldMode", &tmZero},
        {15, "GetHoldReg", &tmZero},
        {16, "GetHardTimerIntrCode", &tmGetHardTimerIntrCode},
        {17, "GetTimerMode", &tmZero},
        {18, "GetTimerReadFunc", nullptr},
        {20, "SetTimerHandler", &tmSetTimerHandler},
        {21, "SetOverflowHandler", &tmZero},
        {22, "SetupHardTimer", &tmSetupHardTimer},
        {23, "StartHardTimer", &tmStartHardTimer},
        {24, "StopHardTimer", &tmStopHardTimer},
    });
    // Libraries whose calls only configure hardware the HLE does not model.
    defineLibrary("dmacman", 0x0101, {
        {4, "dmac_ch_set_madr", &retZero}, {5, "dmac_ch_get_madr", &retZero}, {6, "dmac_ch_set_bcr", &retZero},
        {7, "dmac_ch_get_bcr", &retZero}, {8, "dmac_ch_set_chcr", &retZero}, {9, "dmac_ch_get_chcr", &retZero},
        {10, "dmac_ch_set_tadr", &retZero}, {11, "dmac_ch_get_tadr", &retZero}, {14, "dmac_set_dpcr", &retZero},
        {15, "dmac_get_dpcr", &retZero}, {16, "dmac_set_dpcr2", &retZero}, {17, "dmac_get_dpcr2", &retZero},
        {20, "dmac_set_dicr", &retZero}, {21, "dmac_get_dicr", &retZero}, {22, "dmac_set_dicr2", &retZero},
        {23, "dmac_get_dicr2", &retZero}, {28, "sceSetSliceDMA", nullptr}, {32, "sceStartDMA", nullptr},
        {33, "sceSetDMAPriority", &retZero}, {34, "sceEnableDMAChannel", &retZero}, {35, "sceDisableDMAChannel", &retZero},
    });
    defineLibrary("ssbusc", 0x0101, {
        {4, "SetDelay", &retZero}, {5, "GetDelay", &retZero}, {6, "SetBaseAddress", &retZero},
        {7, "GetBaseAddress", &retZero}, {16, "SetCommonDelay", &retZero}, {17, "GetCommonDelay", &retZero},
    });
    defineLibrary("excepman", 0x0102, {
        {3, "GetExHandlersTable", &retZero}, {4, "RegisterExceptionHandler", &retZero},
        {5, "RegisterPriorityExceptionHandler", &retZero}, {6, "RegisterDefaultExceptionHandler", &retZero},
        {7, "ReleaseExceptionHandler", &retZero}, {8, "ReleaseDefaultExceptionHandler", &retZero},
    });
    defineLibrary("secrman", 0x0104, {
        {4, "SecrSetMcCommandHandler", &retZero}, {5, "SecrSetMcDevIDHandler", &retZero}, {6, "SecrAuthCard", nullptr},
        {7, "SecrResetAuthCard", &retZero},
    });
}

} // namespace ps2::iop::libs
