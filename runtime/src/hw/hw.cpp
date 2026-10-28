// EE register file: INTC, timers, SIO, SIF/MCH registers, vsync timing, and
// routing of everything else to the DMAC, GS, GIF and VIF models.
#include "hw/hw.h"

#include "clock.h"
#include "hw/dmac.h"
#include "hw/gs.h"
#include "hw/vif.h"
#include "internal.h"
#include "iop/iop.h"
#include "kernel/kernel.h"
#include "log.h"
#include "stubs/format.h"

#include <cstdio>
#include <string>
#include <unordered_map>

namespace ps2::hw {

namespace {

// ---- INTC ---------------------------------------------------------------------
uint32_t g_istat = 0, g_imask = 0;

// ---- timers --------------------------------------------------------------------
struct Timer {
    uint32_t mode = 0, comp = 0, hold = 0;
    uint32_t count = 0;        // value at baseCycle
    uint64_t baseCycle = 0;
    uint64_t event = 0;
};
Timer g_timers[4];

// ---- video timing ----------------------------------------------------------------
bool g_pal = false;
bool g_interlace = true;
uint64_t g_vsyncs = 0;
uint64_t g_frameCycles = 0, g_vblankCycles = 0;

std::unordered_map<uint32_t, uint32_t> g_misc;   // registers we only store
std::string g_sioLine;

uint64_t timerTickCycles(const Timer& t) {
    switch (t.mode & 3) {
    case 0: return 2;                  // BUSCLK
    case 1: return 2 * 16;
    case 2: return 2 * 256;
    default: return hsyncCycles();     // HBLANK
    }
}

uint32_t timerCount(int i) {
    Timer& t = g_timers[i];
    if (!(t.mode & 0x80)) return t.count;   // CUE off: stopped
    const uint64_t ticks = (nowCycles() - t.baseCycle) / timerTickCycles(t);
    uint64_t v = t.count + ticks;
    if ((t.mode & 0x40) && t.comp) v %= static_cast<uint64_t>(t.comp) + 1;   // ZRET
    return static_cast<uint32_t>(v & 0xFFFF);
}

void timerRebase(int i) {
    Timer& t = g_timers[i];
    t.count = timerCount(i);
    t.baseCycle = nowCycles();
}

void timerEvent(uint64_t idx);

void timerSchedule(int i) {
    Timer& t = g_timers[i];
    if (t.event) cancelEvent(t.event);
    t.event = 0;
    if (!(t.mode & 0x80) || !(t.mode & 0x300)) return;   // not counting, or no interrupt enabled
    const uint32_t cur = timerCount(i);
    uint32_t ticks = 0x10000 - cur;                       // until overflow
    if ((t.mode & 0x100) && t.comp > cur) ticks = t.comp - cur;
    t.event = scheduleEvent(nowCycles() + ticks * timerTickCycles(t), timerEvent, static_cast<uint64_t>(i));
}

void timerEvent(uint64_t idx) {
    const int i = static_cast<int>(idx);
    Timer& t = g_timers[i];
    t.event = 0;
    const uint32_t cur = timerCount(i);
    bool irq = false;
    if ((t.mode & 0x100) && cur == t.comp) {
        t.mode |= 0x400;   // EQUF
        irq = true;
        if (t.mode & 0x40) {   // ZRET: restart from zero
            t.count = 0;
            t.baseCycle = nowCycles();
        }
    }
    if ((t.mode & 0x200) && cur == 0) {
        t.mode |= 0x800;   // OVFF
        irq = true;
    }
    if (irq) raiseIntc(INTC_TIM0 + i);
    timerRebase(i);
    timerSchedule(i);
}

uint32_t timerRead(int i, uint32_t reg) {
    switch (reg) {
    case 0x00: return timerCount(i);
    case 0x10: return g_timers[i].mode;
    case 0x20: return g_timers[i].comp;
    case 0x30: return g_timers[i].hold;
    default: return 0;
    }
}

void timerWrite(int i, uint32_t reg, uint32_t v) {
    Timer& t = g_timers[i];
    switch (reg) {
    case 0x00:
        t.count = v & 0xFFFF;
        t.baseCycle = nowCycles();
        break;
    case 0x10: {
        timerRebase(i);
        const uint32_t flags = (t.mode & 0xC00) & ~(v & 0xC00);   // writing 1 to EQUF/OVFF clears them
        t.mode = (v & 0x3FF) | flags;
        break;
    }
    case 0x20: t.comp = v & 0xFFFF; break;
    case 0x30: t.hold = v & 0xFFFF; break;
    default: break;
    }
    timerSchedule(i);
}

// ---- vsync -----------------------------------------------------------------------
void vblankEnd(uint64_t);

void vblankStart(uint64_t) {
    ++g_vsyncs;
    gs::onVblankStart();
    raiseIntc(INTC_VBLANK_START);
    kernel::onVblankStart();
    iop::onVblank(true);
    scheduleEvent(nowCycles() + g_vblankCycles, vblankEnd, 0);
    scheduleEvent(nowCycles() + g_frameCycles, vblankStart, 0);
    if (g_opt.maxVsyncs && g_vsyncs >= g_opt.maxVsyncs) {
        PS2_INFO("hw", "reached --max-vsyncs %llu; exiting", static_cast<unsigned long long>(g_opt.maxVsyncs));
        requestExit(0);
    }
}

void vblankEnd(uint64_t) {
    gs::onVblankEnd();
    raiseIntc(INTC_VBLANK_END);
    iop::onVblank(false);
}

void unknownRegister(const char* what, uint32_t addr, uint64_t value) {
    char key[64];
    std::snprintf(key, sizeof key, "%s of unmodelled register 0x%08x", what, addr);
    PS2_UNIMPL("hw", key, "%s (value 0x%llx, pc 0x%08x)", key, static_cast<unsigned long long>(value), cpu.pc);
}

uint32_t readEe32(uint32_t a) {
    if (a < 0x10002000u) return timerRead(static_cast<int>((a >> 11) & 3), a & 0x7F0);
    if (a >= 0x10003000u && a < 0x10003800u) return gs::gifRegRead(a);
    if (a >= 0x10003800u && a < 0x10004000u) return vif::regRead(a);
    if (a >= 0x10008000u && a < 0x1000F000u) return dmac::regRead(a);
    switch (a) {
    case 0x1000F000u: return g_istat;
    case 0x1000F010u: return g_imask;
    case 0x1000F130u: return 0;                  // SIO_ISR: transmitter always ready
    case 0x1000F110u: return 0x00000060u;        // SIO_LSR-ish: TX empty
    case 0x1000F200u: case 0x1000F210u: case 0x1000F220u: case 0x1000F230u: case 0x1000F240u: case 0x1000F260u:
        return iop::sbusRead(a);
    case 0x1000F520u: case 0x1000F590u: return dmac::enableRegRead(a);
    case 0x1000F430u: case 0x1000F440u: return 0;   // MCH (RDRAM init; BIOS only)
    default: break;
    }
    if (auto it = g_misc.find(a); it != g_misc.end()) return it->second;
    unknownRegister("read", a, 0);
    return 0;
}

void writeEe32(uint32_t a, uint32_t v) {
    if (a < 0x10002000u) return timerWrite(static_cast<int>((a >> 11) & 3), a & 0x7F0, v);
    if (a >= 0x10003000u && a < 0x10003800u) return gs::gifRegWrite(a, v);
    if (a >= 0x10003800u && a < 0x10004000u) return vif::regWrite(a, v);
    if (a >= 0x10008000u && a < 0x1000F000u) return dmac::regWrite(a, v);
    switch (a) {
    case 0x1000F000u: g_istat &= ~v; return;     // write 1 to clear
    case 0x1000F010u:
        g_imask ^= v & 0x7FFF;                   // write 1 to toggle
        if (g_istat & g_imask) kernel::kickInterrupts();
        return;
    case 0x1000F180u: {                          // SIO TX FIFO: debug console output
        const char c = static_cast<char>(v & 0xFF);
        g_sioLine.push_back(c);
        if (c == '\n' || g_sioLine.size() > 512) {
            hostPrint(g_sioLine);
            g_sioLine.clear();
        }
        return;
    }
    case 0x1000F200u: case 0x1000F210u: case 0x1000F220u: case 0x1000F230u: case 0x1000F240u: case 0x1000F260u:
        return iop::sbusWrite(a, v);
    case 0x1000F590u: return dmac::enableRegWrite(a, v);
    case 0x1000F430u: case 0x1000F440u: return;
    default: break;
    }
    g_misc[a] = v;
    if (a < 0x1000F100u || a >= 0x1000F200u) unknownRegister("write", a, v);
}

bool isGsPriv(uint32_t a) { return a >= 0x12000000u && a < 0x12002000u; }

} // namespace

void init() {
    g_istat = g_imask = 0;
    for (auto& t : g_timers) t = Timer{};
    g_misc.clear();
    g_vsyncs = 0;
    setVideoMode(true, 2, false);
}

uint64_t hsyncCycles() { return g_pal ? kEeClockHz / 15625 : kEeClockHz * 1000 / 15734264; }
bool isPal() { return g_pal; }
uint64_t vsyncCount() { return g_vsyncs; }

void setVideoMode(bool interlace, int mode, bool field) {
    (void)field;
    g_interlace = interlace;
    g_pal = mode == 3;   // SetGsCrt: 2 = NTSC, 3 = PAL (other values: VESA/DTV, treated as NTSC timing)
    g_frameCycles = g_pal ? kEeClockHz / 50 : kEeClockHz * 1001 / 60000;
    g_vblankCycles = hsyncCycles() * (g_pal ? 25 : 22);
    gs::setVideoMode(interlace, g_pal);
}

void startVsync() { scheduleEvent(nowCycles() + g_frameCycles, vblankStart, 0); }

// ---- INTC ----------------------------------------------------------------------------
void raiseIntc(int cause) {
    g_istat |= 1u << cause;
    if (g_imask & (1u << cause)) kernel::kickInterrupts();
}
uint32_t intcPending() { return g_istat & g_imask; }
void ackIntc(int cause) { g_istat &= ~(1u << cause); }
void setIntcMask(int cause, bool enabled) {
    if (cause < 0 || cause > 14) return;
    if (enabled) g_imask |= 1u << cause;
    else g_imask &= ~(1u << cause);
    if (g_istat & g_imask) kernel::kickInterrupts();
}
void resetInterrupts() {
    g_istat = g_imask = 0;
    dmac::resetInterrupts();
}

uint32_t dmacPending() { return dmac::pending(); }
void ackDmac(int channel) { dmac::ack(channel); }
void setDmacMask(int channel, bool enabled) { dmac::setMask(channel, enabled); }
void dmacRaise(int channel) { dmac::raise(channel); }
bool dmacIdle() { return dmac::idle(); }

// ---- register access ----------------------------------------------------------------
uint32_t read32(uint32_t phys) {
    if (isGsPriv(phys)) return static_cast<uint32_t>(gs::privRead(phys & ~7u) >> ((phys & 4) * 8));
    return readEe32(phys);
}
void write32(uint32_t phys, uint32_t v) {
    if (isGsPriv(phys)) return gs::privWrite32(phys, v);
    writeEe32(phys, v);
}
uint64_t read64(uint32_t phys) {
    if (isGsPriv(phys)) return gs::privRead(phys);
    return static_cast<uint64_t>(readEe32(phys)) | (static_cast<uint64_t>(readEe32(phys + 4)) << 32);
}
void write64(uint32_t phys, uint64_t v) {
    if (isGsPriv(phys)) return gs::privWrite(phys, v);
    writeEe32(phys, static_cast<uint32_t>(v));
    if ((phys & 0xFF) == 0 && phys >= 0x10008000u && phys < 0x1000F000u) return;   // 64-bit write to CHCR etc.
    writeEe32(phys + 4, static_cast<uint32_t>(v >> 32));
}
void read128(uint32_t phys, uint64_t out[2]) {
    out[0] = read64(phys);
    out[1] = read64(phys + 8);
}
void write128(uint32_t phys, const uint64_t v[2]) {
    // FIFOs: VIF0 0x10004000, VIF1 0x10005000, GIF 0x10006000
    switch (phys & 0xFFFFF000u) {
    case 0x10004000u: return vif::fifoWrite(0, v);
    case 0x10005000u: return vif::fifoWrite(1, v);
    case 0x10006000u: return gs::gifFifoWrite(v);
    case 0x10007000u:
        PS2_UNIMPL("hw", "IPU FIFO write", "IPU (MPEG decoder) FIFO write ignored");
        return;
    default:
        write64(phys, v[0]);
        write64(phys + 8, v[1]);
        return;
    }
}
uint8_t read8(uint32_t phys) { return static_cast<uint8_t>(read32(phys & ~3u) >> ((phys & 3) * 8)); }
uint16_t read16(uint32_t phys) { return static_cast<uint16_t>(read32(phys & ~3u) >> ((phys & 2) * 8)); }
void write8(uint32_t phys, uint8_t v) {
    if (phys == 0x1000F180u) return writeEe32(phys, v);
    const uint32_t shift = (phys & 3) * 8;
    write32(phys & ~3u, (read32(phys & ~3u) & ~(0xFFu << shift)) | (static_cast<uint32_t>(v) << shift));
}
void write16(uint32_t phys, uint16_t v) {
    const uint32_t shift = (phys & 2) * 8;
    write32(phys & ~3u, (read32(phys & ~3u) & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(v) << shift));
}

} // namespace ps2::hw
