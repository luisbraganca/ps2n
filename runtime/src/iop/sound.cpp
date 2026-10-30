// libsd on the SPU2 (sound.h). Which registers each call reads and writes
// follows ps2sdk's open libsd ("freesd", iop/sound/libsd), a reimplementation
// of Sony's, as do the reverb presets. Transfers go through spu2.h instead of
// the IOP's DMA channels 4 and 7: a voice transfer moves its data at once and
// raises the core's DMA interrupt (0x24 or 0x28) a little later; a block
// transfer streams PCM into the core's input (AutoDMA), raising it each time
// a buffer, or half of a looping one, has been taken. The SPU2's own
// interrupt (9) means it touched an interrupt address. The library's
// handlers for those lines run when the kernel delivers them, and call the
// driver's.
#include "iop/sound.h"

#include "clock.h"
#include "iop/cpu.h"
#include "iop/hle.h"
#include "iop/kernel.h"
#include "iop/spu2.h"
#include "log.h"
#include "ps2/memory.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ps2::iop::sound {

using namespace hle;

namespace {

constexpr uint32_t kSpu2 = 0x1F900000u;
constexpr int kIntrSpu2 = 9;
constexpr int kIntrDma[2] = {0x24, 0x28};
constexpr uint64_t kPcmBytesPerSecond = spu2::kSampleRate * 4;   // 16-bit stereo
constexpr uint16_t kTransWrite = 0, kTransRead = 1, kTransStop = 2, kTransWriteFrom = 3, kTransLoop = 0x10, kTransIo = 8;
constexpr uint16_t kAttrDma = 0x30, kAttrDmaWrite = 0x20, kAttrDmaRead = 0x30, kAttrIrq = 0x40, kAttrEffects = 0x80;

// Entries, the u16 that names a register to libsd: what in bits 8-15, the
// voice in bits 1-5, the core in bit 0. Bit 7 marks a core parameter (the
// cores' copies are 40 bytes apart instead of 1 KB); Sony's headers also set
// bit 6 on voice addresses, which the library ignores. The register each
// "what" names, for core 0 and voice 0:
constexpr uint16_t kEntryReg[] = {
    0x000, 0x002, 0x004, 0x006, 0x008, 0x00A, 0x00C, 0x00E,   // VOLL VOLR PITCH ADSR1 ADSR2 ENVX VOLXL VOLXR
    0x198, 0x760, 0x762, 0x764, 0x766, 0x768, 0x76A, 0x76C,   // MMIX MVOLL MVOLR EVOLL EVOLR AVOLL AVOLR BVOLL
    0x76E, 0x770, 0x772, 0x180, 0x184, 0x1A0, 0x1A4, 0x340,   // BVOLR MVOLXL MVOLXR PMON NON KON KOFF ENDX
    0x188, 0x18C, 0x190, 0x194, 0x2E0, 0x33C, 0x1A8, 0x19C,   // VMIXL VMIXEL VMIXR VMIXER ESA EEA TSA IRQA
    0x1C0, 0x1C4, 0x1C8, 0x19A, 0x1A8, 0x1AC, 0x1AE, 0x1B0,   // SSA LSAX NAX ATTR TSA STD 1AE ADMAS
};
constexpr uint16_t kEea = 0x1D;

// Reverb presets, in libsd's mode order (off, room, studio small, medium,
// large, hall, space echo, echo, delay, pipe): the work area's size in 16-byte
// units, then the 32 reverb registers in the PS1 SPU's order (dAPF1, dAPF2,
// the eight volumes vIIR...vAPF2, the twenty buffer offsets mLSAME...mRAPF2
// in 8-byte units, vLIN, vRIN). These are the values libsd loads.
struct Preset {
    uint16_t size;
    uint16_t regs[32];
};
constexpr Preset kPresets[10] = {
    {0x0001, {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0001, 0x0000, 0x0000,
              0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
              0x0000, 0x0000, 0x0000, 0x0000}},
    {0x026C, {0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80, 0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227,
              0x0374, 0x01EF, 0x0336, 0x01B7, 0x0335, 0x01B6, 0x0334, 0x01B5, 0x0334, 0x01B5, 0x0334, 0x01B5, 0x01B4, 0x0136,
              0x00B8, 0x005C, 0x8000, 0x8000}},
    {0x01F4, {0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00, 0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF,
              0x0372, 0x0266, 0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7, 0x018F, 0x00B5, 0x00B4, 0x0080,
              0x004C, 0x0026, 0x8000, 0x8000}},
    {0x0484, {0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0, 0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F,
              0x07A2, 0x0616, 0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7, 0x042F, 0x0265, 0x0264, 0x01B2,
              0x0100, 0x0080, 0x8000, 0x8000}},
    {0x06FE, {0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680, 0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C,
              0x0BD9, 0x0973, 0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2, 0x05EA, 0x031D, 0x031C, 0x0238,
              0x0154, 0x00AA, 0x8000, 0x8000}},
    {0x0ADE, {0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000, 0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD,
              0x11BC, 0x0DC1, 0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD, 0x09C2, 0x05C1, 0x05C0, 0x041A,
              0x0274, 0x013A, 0x8000, 0x8000}},
    {0x0F6C, {0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000, 0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B,
              0x1BC2, 0x16B2, 0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D, 0x1056, 0x0AE1, 0x0AE0, 0x07A2,
              0x0464, 0x0232, 0x8000, 0x8000}},
    {0x1804, {0x0003, 0x0003, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100, 0x0000, 0x0000, 0x1FFD, 0x0FFD, 0x1009, 0x0009,
              0x0000, 0x0000, 0x1009, 0x0009, 0x1FFF, 0x1FFF, 0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE, 0x1008, 0x1004,
              0x0008, 0x0004, 0x8000, 0x8000}},
    {0x1804, {0x0003, 0x0003, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1FFD, 0x0FFD, 0x1009, 0x0009,
              0x0000, 0x0000, 0x1009, 0x0009, 0x1FFF, 0x1FFF, 0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE, 0x1FFE, 0x1008, 0x1004,
              0x0008, 0x0004, 0x8000, 0x8000}},
    {0x03C0, {0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500, 0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF,
              0x02B0, 0x01D7, 0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059, 0x01A0, 0x00E3, 0x0058, 0x0040,
              0x0028, 0x0014, 0x8000, 0x8000}},
};
constexpr int kModeEcho = 7, kModeDelay = 8;

struct Handler {
    uint32_t fn = 0, arg = 0, gp = 0;
};

struct EffectAttr {   // sceSdEffectAttr
    int32_t core = 0, mode = 0;
    int16_t depthL = 0, depthR = 0;
    int32_t delay = 0, feedback = 0;
};

struct Core {
    Handler intr;       // sceSdSetTransIntrHandler: fn(core, arg)
    Handler callback;   // sceSdSetTransCallback: fn(0), when there is no intr handler
    // Voice transfer.
    bool voiceBusy = false, voiceIo = false, voicePending = false;
    std::vector<int> voiceWaiters;   // threads in sceSdVoiceTransStatus(.., wait)
    uint64_t voiceEvent = 0;
    // Block transfer: one buffer, or the two halves of a looping one.
    bool blockActive = false, blockLoop = false, blockPending = false;
    uint32_t blockAddr = 0, blockSize = 0;   // the whole buffer, or one half
    uint32_t blockHalf = 0;                  // the half being taken
    bool blockRead = false;                  // reading the output back (not modelled: silence)
    uint64_t blockEvent = 0;
    EffectAttr effect;
};

Core g_core[2];
Handler g_spu2Intr, g_irqCallback;
uint16_t g_spdifMode = 0;
uint32_t g_transTrap = 0, g_spu2Trap = 0;

uint16_t rd(uint32_t reg) { return spu2::read16(kSpu2 + reg); }
void wr(uint32_t reg, uint16_t v) { spu2::write16(kSpu2 + reg, v); }
uint32_t coreBase(int core) { return core ? 0x400u : 0u; }

// The register an entry names.
uint32_t paramReg(uint16_t entry) {
    const uint32_t what = entry >> 8;
    const uint32_t core = entry & 1;
    if (what >= std::size(kEntryReg)) return 0x7FE;   // nothing there
    const uint32_t coreStep = (entry & 0x80) ? 40u : 1024u;
    return kEntryReg[what] + core * coreStep + ((entry & 0x3Eu) << 3);
}
uint32_t switchReg(uint16_t entry) { return (entry >> 8) < std::size(kEntryReg) ? kEntryReg[entry >> 8] + ((entry & 1u) << 10) : 0x7FC; }
uint32_t addrReg(uint16_t entry) {
    if ((entry >> 8) >= std::size(kEntryReg)) return 0x7FC;
    return kEntryReg[entry >> 8] + ((entry & 1u) << 10) + ((entry & 0x3Eu) >> 1) * 12;
}

void setParam(uint16_t entry, uint16_t v) { wr(paramReg(entry), v); }
uint16_t getParam(uint16_t entry) { return rd(paramReg(entry)); }
void setSwitch(uint16_t entry, uint32_t v) {
    const uint32_t r = switchReg(entry);
    wr(r, static_cast<uint16_t>(v));
    wr(r + 2, static_cast<uint16_t>((v >> 16) & 0xFF));
}
uint32_t getSwitch(uint16_t entry) {
    const uint32_t r = switchReg(entry);
    return rd(r) | (static_cast<uint32_t>(rd(r + 2)) << 16);
}
// Addresses are bytes to libsd and words to the SPU2, whose low register
// keeps them on a 16-byte block. EEA has only its high register.
void setAddr(uint16_t entry, uint32_t v) {
    const uint32_t r = addrReg(entry);
    wr(r, static_cast<uint16_t>(v >> 17));
    if ((entry >> 8) != kEea) wr(r + 2, static_cast<uint16_t>((v >> 1) & 0xFFF8));
}
uint32_t getAddr(uint16_t entry) {
    const uint32_t r = addrReg(entry);
    const uint32_t hi = static_cast<uint32_t>(rd(r)) << 17;
    if ((entry >> 8) == kEea) return hi | 0x1FFFF;
    return hi | (static_cast<uint32_t>(rd(r + 2)) << 1);
}

// Core attributes: the bit of ATTR each names; 8 is the noise clock, 0xA the S/PDIF mode.
int attrBit(uint16_t entry) {
    switch (entry & ~1) {
    case 0x2: return 7;    // effects
    case 0x4: return 6;    // interrupt
    case 0x6: return 14;   // mute
    default: return -1;
    }
}
void setCoreAttr(uint16_t entry, uint16_t v) {
    const uint32_t a = coreBase(entry & 1) + 0x19A;
    if ((entry & ~1) == 0x8) return wr(a, static_cast<uint16_t>((rd(a) & ~0x3F00) | ((v & 0x3F) << 8)));
    if ((entry & ~1) == 0xA) {
        g_spdifMode = v;   // the S/PDIF output is not modelled
        return;
    }
    const int bit = attrBit(entry);
    if (bit < 0) return;
    wr(a, static_cast<uint16_t>((rd(a) & ~(1u << bit)) | ((v & 1u) << bit)));
}
uint16_t getCoreAttr(uint16_t entry) {
    const uint32_t a = coreBase(entry & 1) + 0x19A;
    if ((entry & ~1) == 0x8) return (rd(a) >> 8) & 0x3F;
    if ((entry & ~1) == 0xA) return g_spdifMode;
    const int bit = attrBit(entry);
    return bit < 0 ? 0 : (rd(a) >> bit) & 1;
}

void setAttrDma(int core, uint16_t bits) {
    const uint32_t a = coreBase(core) + 0x19A;
    wr(a, static_cast<uint16_t>((rd(a) & ~kAttrDma) | bits));
}

// ---- Interrupts ------------------------------------------------------------------------------
void callTransferHandler(int core) {
    Core& c = g_core[core];
    if (c.intr.fn) kernel::callHandler(c.intr.fn, static_cast<uint32_t>(core), c.intr.gp, c.intr.arg);
    else if (c.callback.fn) kernel::callHandler(c.callback.fn, 0, c.callback.gp);
}

uint64_t pcmCycles(uint32_t bytes) { return std::max<uint64_t>(1, bytes * kEeClockHz / kPcmBytesPerSecond); }

// Reading the output back: silence, one interrupt per half at the playback rate.
void readTick(uint64_t core) {
    Core& c = g_core[core];
    c.blockEvent = 0;
    if (!c.blockActive) return;
    mem::fill(c.blockAddr + c.blockHalf * c.blockSize, 0, c.blockSize);
    c.blockPending = true;
    kernel::raiseInterrupt(kIntrDma[core]);
}

// Lines 0x24 and 0x28: a core's transfer finished ($a0: the core).
void transInterrupt() {
    const int core = sarg(0) & 1;
    Core& c = g_core[core];
    if (c.voicePending) {
        c.voicePending = false;
        c.voiceBusy = false;
        setAttrDma(core, 0);
        for (int t : c.voiceWaiters) kernel::wakeThread(t, 1);
        c.voiceWaiters.clear();
        callTransferHandler(core);
    }
    if (c.blockPending) {
        c.blockPending = false;
        if (c.blockLoop) {
            // The other half follows, armed here as the library does: an IOP
            // that answers late leaves the input playing what it had.
            if (c.blockActive) {
                c.blockHalf ^= 1;
                if (c.blockRead) c.blockEvent = scheduleEvent(nowCycles() + pcmCycles(c.blockSize), &readTick, static_cast<uint64_t>(core));
                else spu2::startAutoDma(core, c.blockAddr + c.blockHalf * c.blockSize, c.blockSize);
            }
        } else {   // the input goes quiet as the transfer ends
            c.blockActive = false;
            setAttrDma(core, 0);
            const uint32_t mmix = coreBase(core) + 0x198;
            wr(mmix, static_cast<uint16_t>(rd(mmix) & 0xFF3F));
            wr(coreBase(core) + 0x1B0, 0);
        }
        callTransferHandler(core);
    }
    retU(1);
}

// Line 9: the SPU2 touched a core's interrupt address. The library turns
// that core's interrupt off (which acknowledges it) and calls the driver.
void spu2Interrupt() {
    const uint32_t cores = (rd(0x7C2) & 0xC) >> 2;
    if (cores) {
        for (int core = 0; core < 2; ++core)
            if (cores & (1u << core)) wr(coreBase(core) + 0x19A, static_cast<uint16_t>(rd(coreBase(core) + 0x19A) & ~kAttrIrq));
        if (g_spu2Intr.fn) kernel::callHandler(g_spu2Intr.fn, cores, g_spu2Intr.gp, g_spu2Intr.arg);
        else if (g_irqCallback.fn) kernel::callHandler(g_irqCallback.fn, 0, g_irqCallback.gp);
    }
    retU(1);
}

void registerInterrupts() {
    for (int core = 0; core < 2; ++core) {
        kernel::releaseInterrupt(kIntrDma[core]);
        kernel::registerInterrupt(kIntrDma[core], g_transTrap, static_cast<uint32_t>(core));
    }
    kernel::releaseInterrupt(kIntrSpu2);
    kernel::registerInterrupt(kIntrSpu2, g_spu2Trap, 0);
}

void voiceDone(uint64_t core) {
    Core& c = g_core[core];
    c.voiceEvent = 0;
    c.voicePending = true;
    kernel::raiseInterrupt(kIntrDma[core]);
}

// The SPU2 took a block transfer's last block: the interrupt handler goes on.
void autoDmaDone(int core) {
    g_core[core].blockPending = true;
    kernel::raiseInterrupt(kIntrDma[core]);
}

uint32_t stopBlock(int core) {
    Core& c = g_core[core];
    const uint32_t where = c.blockActive ? (c.blockRead ? c.blockAddr + c.blockHalf * c.blockSize : spu2::autoDmaAddress(core)) : 0;
    if (c.blockEvent) cancelEvent(c.blockEvent);
    c.blockEvent = 0;
    if (c.blockActive && !c.blockRead) wr(coreBase(core) + 0x1B0, 0);   // stops AutoDMA, clears the input
    c.blockActive = false;
    return (c.blockHalf << 24) | (where & 0xFFFFFF);
}

// ---- Exports ---------------------------------------------------------------------------------
void initVoices() {
    // A silent block that loops on itself, at 0x5000, for idle voices.
    wr(0x1A8, 0);
    wr(0x1AA, 0x5000 >> 1);
    for (int i = 0; i < 16; ++i) wr(0x1AC, i < 8 ? 0x0707 : 0);
    for (int core = 0; core < 2; ++core) {
        const uint32_t b = coreBase(core);
        for (uint32_t v = 0; v < 24; ++v) {
            wr(b + v * 16 + 0x0, 0);
            wr(b + v * 16 + 0x2, 0);
            wr(b + v * 16 + 0x4, 0x3FFF);
            wr(b + v * 16 + 0x6, 0);
            wr(b + v * 16 + 0x8, 0);
            wr(b + 0x1C0 + v * 12, 0);
            wr(b + 0x1C2 + v * 12, 0x5000 >> 1);
        }
    }
    // Every voice on, then off: they end up looping the silent block.
    for (uint32_t r : {0x1A0u, 0x5A0u}) {
        wr(r, 0xFFFF);
        wr(r + 2, 0xFF);
    }
    for (uint32_t r : {0x1A4u, 0x5A4u}) {
        wr(r, 0xFFFF);
        wr(r + 2, 0xFF);
    }
    wr(0x340, 0);
    wr(0x342, 0);
}

void initCoreVolume(bool keep) {
    wr(0x7C0, 0xC032);
    wr(0x19A, keep ? 0xC080 : 0xC000);            // on, unmuted, effects as they were
    wr(0x59A, keep ? 0xC081 : 0xC001);            // core 1 also takes core 0's output
    for (uint32_t b : {0x000u, 0x400u})
        for (uint32_t r : {0x188u, 0x18Cu, 0x190u, 0x194u}) {   // every voice to the dry and wet mixes
            wr(b + r, 0xFFFF);
            wr(b + r + 2, 0xFF);
        }
    wr(0x198, 0xFF0);
    wr(0x598, 0xFFC);
    if (!keep) {
        for (uint32_t r : {0x760u, 0x762u, 0x788u, 0x78Au, 0x764u, 0x766u, 0x78Cu, 0x78Eu}) wr(r, 0);   // master and effect volumes
        wr(0x33C, 0xE);
        wr(0x73C, 0xF);
    }
    wr(0x768, 0);
    wr(0x76A, 0);
    wr(0x790, 0x7FFF);   // core 1's external input: core 0
    wr(0x792, 0x7FFF);
    for (uint32_t r : {0x76Cu, 0x76Eu, 0x794u, 0x796u}) wr(r, 0);
}

void setEsa(int core, uint32_t addr) {
    wr(coreBase(core) + 0x2E0, static_cast<uint16_t>(addr >> 17));
    wr(coreBase(core) + 0x2E2, static_cast<uint16_t>(addr >> 1));
}
uint32_t eea(int core) { return (static_cast<uint32_t>(rd(coreBase(core) + 0x33C)) << 17) | 0x1FFFF; }
uint32_t effectStart(int core, int mode) { return eea(core) - ((static_cast<uint32_t>(kPresets[mode].size) << 4) - 2); }

void sdInit() {
    const bool keep = sarg(0) & 1;
    wr(0x7C6, 0x900);
    wr(0x7C8, 0x200);
    wr(0x7CA, 8);
    if (!keep) {
        wr(0x7C0, 0);
        wr(0x7C0, 0x8000);
        for (int core = 0; core < 2; ++core) {
            stopBlock(core);
            wr(coreBase(core) + 0x19A, 0);
            wr(coreBase(core) + 0x19A, 0x8000);
            wr(0x760 + core * 40, 0);
            wr(0x762 + core * 40, 0);
            wr(coreBase(core) + 0x1A4, 0xFFFF);
            wr(coreBase(core) + 0x1A6, 0xFF);
        }
        for (uint32_t r : {0x580u, 0x582u, 0x584u, 0x586u}) wr(r, 0);   // core 1: no pitch modulation, no noise
    }
    for (Core& c : g_core) {
        if (c.voiceEvent) cancelEvent(c.voiceEvent);
        if (c.blockEvent) cancelEvent(c.blockEvent);
        c = Core{};
    }
    g_spu2Intr = g_irqCallback = Handler{};
    registerInterrupts();
    for (int core = 0; core < 2; ++core) {
        g_core[core].effect.core = core;
        if (!keep) setEsa(core, effectStart(core, 0));
    }
    initVoices();
    initCoreVolume(keep);
    for (int line : {kIntrDma[0], kIntrDma[1], kIntrSpu2}) kernel::enableInterrupt(line, true);
    ret(0);
}

void sdQuit() {
    for (int core = 0; core < 2; ++core) stopBlock(core);
    for (int line : {kIntrDma[0], kIntrDma[1], kIntrSpu2}) {
        kernel::enableInterrupt(line, false);
        kernel::releaseInterrupt(line);
    }
    ret(0);
}

void sdSetParam() { setParam(static_cast<uint16_t>(arg(0)), static_cast<uint16_t>(arg(1))); }
void sdGetParam() { retU(getParam(static_cast<uint16_t>(arg(0)))); }
void sdSetSwitch() { setSwitch(static_cast<uint16_t>(arg(0)), arg(1)); }
void sdGetSwitch() { retU(getSwitch(static_cast<uint16_t>(arg(0)))); }
void sdSetAddr() { setAddr(static_cast<uint16_t>(arg(0)), arg(1)); }
void sdGetAddr() { retU(getAddr(static_cast<uint16_t>(arg(0)))); }
void sdSetCoreAttr() { setCoreAttr(static_cast<uint16_t>(arg(0)), static_cast<uint16_t>(arg(1))); }
void sdGetCoreAttr() { retU(getCoreAttr(static_cast<uint16_t>(arg(0)))); }

// Pitch 0x1000 plays at the sample's own rate; an octave doubles it.
void sdNote2Pitch() {
    const double semis = (static_cast<int>(arg(2) & 0xFFFF) - static_cast<int>(arg(0) & 0xFFFF)) +
                         (static_cast<int16_t>(arg(3)) - static_cast<int>(arg(1) & 0xFFFF)) / 128.0;
    const double pitch = 4096.0 * std::pow(2.0, semis / 12.0);
    retU(static_cast<uint32_t>(std::clamp(pitch, 0.0, 16383.0)));
}
void sdPitch2Note() {
    const double pitch = std::max<double>(1, arg(2) & 0xFFFF);
    const double semis = 12.0 * std::log2(pitch / 4096.0) + (arg(0) & 0xFFFF) + (arg(1) & 0xFFFF) / 128.0;
    const int fine = static_cast<int>(std::lround(semis * 128));
    retU(static_cast<uint32_t>(((fine / 128) << 8) | (fine % 128)) & 0xFFFF);
}

uint32_t batchOne(uint16_t func, uint16_t entry, uint32_t value, uint32_t rets, uint32_t index, bool& ok) {
    ok = true;
    switch (func) {
    case 0x01: setParam(entry, static_cast<uint16_t>(value)); return 0;
    case 0x02: setSwitch(entry, value); return 0;
    case 0x03: setAddr(entry, value); return 0;
    case 0x04: setCoreAttr(entry, static_cast<uint16_t>(value)); return 0;
    case 0x05: mem::write32(value, entry); return 0;   // WRITEIOP
    case 0x06: write32(value, entry); return 0;        // WRITEEE
    case 0x07:                                         // EERETURN: the results so far to EE memory
        for (uint32_t i = 0; i < entry / 4u && i < index && rets; ++i) write32(value + 4 * i, mem::read32(rets + 4 * i));
        return 0;
    case 0x11: return getParam(entry);
    case 0x12: return getSwitch(entry);
    case 0x13: return getAddr(entry);
    case 0x14: return getCoreAttr(entry);
    default: ok = false; return 0;
    }
}

void procBatch(uint32_t batch, uint32_t rets, uint32_t num, int voiceMask) {
    for (uint32_t i = 0; i < num; ++i) {
        const uint32_t at = batch + 8 * i;
        const auto func = static_cast<uint16_t>(mem::read16(at));
        const auto entry = static_cast<uint16_t>(mem::read16(at + 2));
        const uint32_t value = mem::read32(at + 4);
        // ProcBatchEx: the batch is applied to every voice in the mask.
        if (voiceMask >= 0 && func <= 0x04) {
            for (int v = 0; v < 24; ++v)
                if (voiceMask & (1 << v)) {
                    bool ok;
                    batchOne(func, static_cast<uint16_t>((entry & ~0x3E) | (v << 1)), value, rets, i, ok);
                }
            if (rets) mem::write32(rets + 4 * i, 0);
            continue;
        }
        bool ok;
        const uint32_t r = batchOne(func, entry, value, rets, i, ok);
        if (!ok) return ret(-1 - static_cast<int32_t>(i));
        if (rets) mem::write32(rets + 4 * i, r);
    }
    ret(static_cast<int32_t>(num));
}
void sdProcBatch() { procBatch(arg(0), arg(1), arg(2), -1); }
void sdProcBatchEx() { procBatch(arg(0), arg(1), arg(2), static_cast<int>(arg(3) & 0xFFFFFF)); }

void sdVoiceTrans() {
    const int core = sarg(0) & 1;
    const auto mode = static_cast<uint16_t>(arg(1));
    const uint32_t iopAddr = arg(2), spuAddr = arg(3) & (spu2::kRamBytes - 1), size = arg(4);
    Core& c = g_core[core];
    if (c.voiceBusy) return ret(-1);
    const uint32_t n = std::min(size, spu2::kRamBytes - spuAddr);
    std::vector<uint8_t> data(n);
    wr(coreBase(core) + 0x1A8, static_cast<uint16_t>(spuAddr >> 17));
    wr(coreBase(core) + 0x1AA, static_cast<uint16_t>(spuAddr >> 1));
    switch (mode & 3) {
    case kTransWrite:
        mem::readBlock(iopAddr, data.data(), n);
        spu2::writeMemory(spuAddr, data.data(), n);
        break;
    case kTransRead:
        spu2::readMemory(spuAddr, data.data(), n);
        mem::writeBlock(iopAddr, data.data(), n);
        break;
    default:
        return ret(-1);
    }
    c.voiceIo = (mode & kTransIo) != 0;
    if (!c.voiceIo) {   // DMA: done a little later, with the core's DMA interrupt
        c.voiceBusy = true;
        setAttrDma(core, (mode & 3) == kTransWrite ? kAttrDmaWrite : kAttrDmaRead);
        c.voiceEvent = scheduleEvent(nowCycles() + (1000 + size / 4) * kEeCyclesPerIopCycle, &voiceDone, static_cast<uint64_t>(core));
    }
    ret(static_cast<int32_t>(size));
}

void sdVoiceTransStatus() {
    Core& c = g_core[sarg(0) & 1];
    if (c.voiceIo || !c.voiceBusy) return ret(1);
    if ((arg(1) & 1) && kernel::currentThread() >= 0 && !kernel::inInterrupt()) {   // wait for the end
        c.voiceWaiters.push_back(kernel::currentThread());
        return kernel::blockCurrent(kernel::TSW_SERVICE, 0);
    }
    ret(0);
}

void sdBlockTrans() {
    const int core = sarg(0) & 1;
    const auto mode = static_cast<uint16_t>(arg(1));
    Core& c = g_core[core];
    if ((mode & 3) == kTransStop) return retU(stopBlock(core));
    if (c.blockActive) return ret(-1);
    const bool loop = (mode & kTransLoop) != 0;
    const uint32_t addr = arg(2), size = loop ? arg(3) / 2 : arg(3);
    if (size == 0) return ret(-1);
    c.blockLoop = loop;
    c.blockAddr = addr;
    c.blockSize = size;
    c.blockHalf = 0;
    c.blockActive = true;
    c.blockPending = false;
    c.blockRead = (mode & 3) == kTransRead;
    if (c.blockRead) {
        PS2_UNIMPL("sound", "block transfer read", "sceSdBlockTrans: reading the SPU2's output back is not modelled; the driver gets silence");
        c.blockEvent = scheduleEvent(nowCycles() + pcmCycles(size), &readTick, static_cast<uint64_t>(core));
        setAttrDma(core, kAttrDmaRead);
        return ret(0);
    }
    // Write-from: the transfer starts part way into the buffer, on a 1 KB block.
    uint32_t start = addr, length = size;
    if ((mode & 3) == kTransWriteFrom && arg(4)) {
        uint32_t offset = arg(4) - addr;
        if (offset >= size) {
            if (!loop || offset >= 2 * size) {
                c.blockActive = false;
                return ret(-1);
            }
            offset -= size;
            c.blockHalf = 1;
        }
        offset = std::min((offset + 1023) & ~1023u, size);
        start = addr + c.blockHalf * size + offset;
        length = size - offset;
    }
    wr(coreBase(core) + 0x1A8, 0);
    wr(coreBase(core) + 0x1AA, 0);
    wr(coreBase(core) + 0x1B0, static_cast<uint16_t>(1 << core));
    if (length) spu2::startAutoDma(core, start, length);
    else autoDmaDone(core);
    ret(0);
}

void sdBlockTransStatus() {
    const int core = sarg(0) & 1;
    const Core& c = g_core[core];
    uint32_t where = 0;
    if (c.blockActive) where = c.blockRead ? c.blockAddr + c.blockHalf * c.blockSize : spu2::autoDmaAddress(core);
    retU((c.blockHalf << 24) | (where & 0xFFFFFF));
}

Handler handlerFrom(uint32_t fn, uint32_t a) { return Handler{fn, a, g_cpu.gpr[R_GP]}; }

void sdSetTransCallback() {
    Core& c = g_core[sarg(0) & 1];
    retU(c.callback.fn);
    c.callback = handlerFrom(arg(1), 0);
}
void sdSetIrqCallback() {
    retU(g_irqCallback.fn);
    g_irqCallback = handlerFrom(arg(0), 0);
}
void sdSetTransIntrHandler() {
    Core& c = g_core[sarg(0) & 1];
    retU(c.intr.fn);
    c.intr = handlerFrom(arg(1), arg(2));
}
void sdSetSpu2IntrHandler() {
    retU(g_spu2Intr.fn);
    g_spu2Intr = handlerFrom(arg(0), arg(1));
}
void sdGetTransIntrHandlerArgument() { retU(g_core[sarg(0) & 1].intr.arg); }
void sdGetSpu2IntrHandlerArgument() { retU(g_spu2Intr.arg); }
void sdStopTrans() { retU(stopBlock(sarg(0) & 1)); }

// Zeroes a mode's work area, which ends at EEA.
void clearEffectArea(int core, int mode) {
    if (mode <= 0 || mode > 9) return;
    const uint32_t size = static_cast<uint32_t>(kPresets[mode].size) << 4;
    const std::vector<uint8_t> zeros(size, 0);
    spu2::writeMemory(effectStart(core, mode), zeros.data(), size);
}

// sceSdEffectAttr: core, mode (ints), depth_L, depth_R (shorts), delay, feedback (ints).
void sdSetEffectAttr() {
    const int core = sarg(0) & 1;
    const uint32_t a = arg(1);
    EffectAttr e;
    e.core = core;
    e.mode = static_cast<int32_t>(mem::read32(a + 4));
    e.depthL = static_cast<int16_t>(mem::read16(a + 8));
    e.depthR = static_cast<int16_t>(mem::read16(a + 10));
    e.delay = static_cast<int32_t>(mem::read32(a + 12));
    e.feedback = static_cast<int32_t>(mem::read32(a + 16));
    const bool clear = e.mode & 0x100;
    const int mode = e.mode & ~0x100;
    if (mode < 0 || mode > 9) return ret(-1);
    const int lastMode = g_core[core].effect.mode & 0xFF;
    uint16_t regs[32];
    std::copy(std::begin(kPresets[mode].regs), std::end(kPresets[mode].regs), regs);
    const int32_t delay = e.delay, feedback = e.feedback;
    e.delay = mode == kModeEcho || mode == kModeDelay ? 0x80 : 0;
    e.feedback = mode == kModeEcho ? 0x80 : 0;
    if (mode == kModeEcho || mode == kModeDelay) {   // their delay and feedback come from the attributes
        const auto d = static_cast<uint16_t>((delay + 1) << 5);
        regs[7] = static_cast<uint16_t>(((feedback << 7) + feedback) << 1);
        regs[10] = static_cast<uint16_t>((d << 1) - regs[0]);
        regs[11] = static_cast<uint16_t>(d - regs[1]);
        regs[16] = static_cast<uint16_t>(regs[17] + d);
        regs[12] = static_cast<uint16_t>(regs[13] + d);
        regs[27] = static_cast<uint16_t>(regs[29] + d);
        regs[26] = static_cast<uint16_t>(regs[28] + d);
    }
    g_core[core].effect = e;
    const uint32_t attr = coreBase(core) + 0x19A;
    const bool wasOn = rd(attr) & kAttrEffects;
    if (wasOn) wr(attr, static_cast<uint16_t>(rd(attr) & ~kAttrEffects));
    if (wasOn && clear) clearEffectArea(core, lastMode);
    wr(0x764 + core * 40, static_cast<uint16_t>(e.depthL));
    wr(0x766 + core * 40, static_cast<uint16_t>(e.depthR));
    auto offsetReg = [&](uint32_t r, uint16_t v) {   // 8-byte units to a word address pair
        wr(r, static_cast<uint16_t>(v >> 14));
        wr(r + 2, static_cast<uint16_t>(v << 2));
    };
    offsetReg(coreBase(core) + 0x2E4, regs[0]);
    offsetReg(coreBase(core) + 0x2E8, regs[1]);
    for (uint32_t i = 0; i < 8; ++i) wr(0x774 + core * 40 + i * 2, regs[2 + i]);
    for (uint32_t i = 0; i < 20; ++i) offsetReg(coreBase(core) + 0x2EC + i * 4, regs[10 + i]);
    wr(0x784 + core * 40, regs[30]);
    wr(0x786 + core * 40, regs[31]);
    setEsa(core, effectStart(core, mode));
    if (clear) clearEffectArea(core, mode);
    if (wasOn) wr(attr, static_cast<uint16_t>(rd(attr) | kAttrEffects));
    ret(0);
}
void sdGetEffectAttr() {
    const EffectAttr& e = g_core[sarg(0) & 1].effect;
    const uint32_t a = arg(1);
    mem::write32(a, static_cast<uint32_t>(e.core));
    mem::write32(a + 4, static_cast<uint32_t>(e.mode));
    mem::write16(a + 8, static_cast<uint16_t>(e.depthL));
    mem::write16(a + 10, static_cast<uint16_t>(e.depthR));
    mem::write32(a + 12, static_cast<uint32_t>(e.delay));
    mem::write32(a + 16, static_cast<uint32_t>(e.feedback));
}
void sdClearEffectWorkArea() {
    if (sarg(2) < 0 || sarg(2) > 9) return ret(-1);
    clearEffectArea(sarg(0) & 1, sarg(2));
    ret(0);
}

} // namespace

void init() {
    for (Core& c : g_core) c = Core{};
    g_spu2Intr = g_irqCallback = Handler{};
    g_spdifMode = 0;
    g_transTrap = addTrap(&transInterrupt, "libsd::TransInterrupt");
    g_spu2Trap = addTrap(&spu2Interrupt, "libsd::Spu2Interrupt");
    spu2::setAutoDmaDone(&autoDmaDone);

    defineLibrary("libsd", 0x0105, {
        {2, "sceSdQuit", &sdQuit},
        {4, "sceSdInit", &sdInit},
        {5, "sceSdSetParam", &sdSetParam},
        {6, "sceSdGetParam", &sdGetParam},
        {7, "sceSdSetSwitch", &sdSetSwitch},
        {8, "sceSdGetSwitch", &sdGetSwitch},
        {9, "sceSdSetAddr", &sdSetAddr},
        {10, "sceSdGetAddr", &sdGetAddr},
        {11, "sceSdSetCoreAttr", &sdSetCoreAttr},
        {12, "sceSdGetCoreAttr", &sdGetCoreAttr},
        {13, "sceSdNote2Pitch", &sdNote2Pitch},
        {14, "sceSdPitch2Note", &sdPitch2Note},
        {15, "sceSdProcBatch", &sdProcBatch},
        {16, "sceSdProcBatchEx", &sdProcBatchEx},
        {17, "sceSdVoiceTrans", &sdVoiceTrans},
        {18, "sceSdBlockTrans", &sdBlockTrans},
        {19, "sceSdVoiceTransStatus", &sdVoiceTransStatus},
        {20, "sceSdBlockTransStatus", &sdBlockTransStatus},
        {21, "sceSdSetTransCallback", &sdSetTransCallback},
        {22, "sceSdSetIRQCallback", &sdSetIrqCallback},
        {23, "sceSdSetEffectAttr", &sdSetEffectAttr},
        {24, "sceSdGetEffectAttr", &sdGetEffectAttr},
        {25, "sceSdClearEffectWorkArea", &sdClearEffectWorkArea},
        {26, "sceSdSetTransIntrHandler", &sdSetTransIntrHandler},
        {27, "sceSdSetSpu2IntrHandler", &sdSetSpu2IntrHandler},
        {28, "sceSdGetTransIntrHandlerArgument", &sdGetTransIntrHandlerArgument},
        {29, "sceSdGetSpu2IntrHandlerArgument", &sdGetSpu2IntrHandlerArgument},
        {30, "sceSdStopTrans", &sdStopTrans},
        {31, "sceSdCleanEffectWorkArea", &sdClearEffectWorkArea},
        {32, "sceSdSetEffectMode", &sdSetEffectAttr},
        {33, "sceSdSetEffectModeParams", &sdSetEffectAttr},
    });
}

} // namespace ps2::iop::sound
