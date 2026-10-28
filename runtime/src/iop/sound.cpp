// libsd without an SPU2: registers keep what is written to them, voice
// transfers move data to and from a 2 MB sound memory and then raise their
// completion interrupt, looping block transfers (streamed PCM) raise one per
// half buffer at the 48 kHz playback rate, and every voice reports that it has
// finished playing. Drivers that wait for transfers or poll voices keep
// running; no samples are mixed.
#include "iop/sound.h"

#include "clock.h"
#include "iop/cpu.h"
#include "iop/hle.h"
#include "iop/kernel.h"
#include "log.h"
#include "ps2/memory.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace ps2::iop::sound {

using namespace hle;

namespace {

constexpr uint32_t kSpuRamSize = 2u << 20;
constexpr uint64_t kPcmBytesPerSecond = 48000 * 4;   // 16-bit stereo at 48 kHz
// Entries (the u16 libsd uses to name a register): parameter in bits 8-15,
// voice in bits 1-5, core in bit 0.
constexpr uint16_t kEnvx = 0x05 << 8, kVolxl = 0x06 << 8, kVolxr = 0x07 << 8, kMvolxl = (0x11 << 8) | 0x80,
                   kMvolxr = (0x12 << 8) | 0x80, kEndx = 0x17 << 8;
constexpr uint16_t kTransWrite = 0, kTransRead = 1, kTransStop = 2, kTransWriteFrom = 3, kTransLoop = 0x10,
                   kTransIo = 8;

struct Handler {
    uint32_t fn = 0, arg = 0, gp = 0;
};

struct Core {
    Handler intr;       // sceSdSetTransIntrHandler: fn(core, arg)
    Handler callback;   // sceSdSetTransCallback: fn(0), used when there is no intr handler
    // Voice transfer (DMA mode).
    bool voiceBusy = false, voiceIo = false;
    std::vector<int> voiceWaiters;   // threads in sceSdVoiceTransStatus(.., wait)
    uint64_t voiceEvent = 0;
    // Block transfer (AutoDMA): one buffer, or two halves in loop mode.
    bool blockActive = false, blockLoop = false, blockRead = false;
    uint32_t blockAddr = 0, blockSize = 0;   // the whole buffer, or one half in loop mode
    uint32_t blockHalf = 0;                  // the half playing
    uint64_t blockEvent = 0;
};

std::vector<uint8_t> g_spuRam;
Core g_core[2];
std::map<uint16_t, uint32_t> g_params, g_switches, g_addrs, g_coreAttrs;
std::map<int, std::vector<int16_t>> g_effect;   // sceSdSetEffectAttr, by core
Handler g_spu2Intr, g_irqCallback;

uint32_t valueOr(const std::map<uint16_t, uint32_t>& m, uint16_t key, uint32_t fallback) {
    auto it = m.find(key);
    return it == m.end() ? fallback : it->second;
}

void raiseTransfer(int core) {
    Core& c = g_core[core];
    if (c.intr.fn) kernel::callHandler(c.intr.fn, static_cast<uint32_t>(core), c.intr.gp, c.intr.arg);
    else if (c.callback.fn) kernel::callHandler(c.callback.fn, 0, c.callback.gp);
}

void voiceDone(uint64_t core) {
    Core& c = g_core[core];
    c.voiceEvent = 0;
    c.voiceBusy = false;
    for (int t : c.voiceWaiters) kernel::wakeThread(t, 1);
    c.voiceWaiters.clear();
    raiseTransfer(static_cast<int>(core));
}

uint64_t pcmCycles(uint32_t bytes) { return std::max<uint64_t>(1, bytes * kEeClockHz / kPcmBytesPerSecond); }

void blockTick(uint64_t core) {
    Core& c = g_core[core];
    c.blockEvent = 0;
    if (!c.blockActive) return;
    if (c.blockRead) {
        // What the SPU2 would have output: silence.
        mem::fill(c.blockAddr + c.blockHalf * c.blockSize, 0, c.blockSize);
    }
    if (c.blockLoop) {
        c.blockHalf ^= 1;
        c.blockEvent = scheduleEvent(nowCycles() + pcmCycles(c.blockSize), &blockTick, core);
    } else {
        c.blockActive = false;
    }
    raiseTransfer(static_cast<int>(core));
}

void stopBlock(Core& c) {
    if (c.blockEvent) cancelEvent(c.blockEvent);
    c.blockEvent = 0;
    c.blockActive = false;
}

// ---- exports ---------------------------------------------------------------------------------
void sdInit() {
    for (Core& c : g_core) {
        stopBlock(c);
        if (c.voiceEvent) cancelEvent(c.voiceEvent);
        const Handler intr = c.intr, cb = c.callback;
        c = Core{};
        c.intr = intr;   // handlers survive a re-initialisation
        c.callback = cb;
    }
    g_params.clear();
    g_switches.clear();
    g_addrs.clear();
    g_coreAttrs.clear();
    ret(0);
}

void sdQuit() {
    for (Core& c : g_core) stopBlock(c);
    ret(0);
}

void sdSetParam() { g_params[static_cast<uint16_t>(arg(0))] = arg(1) & 0xFFFF; }
void sdGetParam() {
    const auto entry = static_cast<uint16_t>(arg(0));
    const uint16_t param = entry & 0xFF80;
    // Envelopes and current volumes: every voice has finished, so they are zero.
    if ((entry & 0xFF00) == kEnvx || (entry & 0xFF00) == kVolxl || (entry & 0xFF00) == kVolxr || param == kMvolxl ||
        param == kMvolxr)
        return ret(0);
    retU(valueOr(g_params, entry, 0));
}
void sdSetSwitch() { g_switches[static_cast<uint16_t>(arg(0))] = arg(1) & 0xFFFFFF; }
void sdGetSwitch() {
    const auto entry = static_cast<uint16_t>(arg(0));
    if ((entry & 0xFF00) == kEndx) return retU(0xFFFFFF);   // all voices reached their end
    retU(valueOr(g_switches, entry, 0));
}
void sdSetAddr() { g_addrs[static_cast<uint16_t>(arg(0))] = arg(1); }
void sdGetAddr() { retU(valueOr(g_addrs, static_cast<uint16_t>(arg(0)), 0)); }
void sdSetCoreAttr() { g_coreAttrs[static_cast<uint16_t>(arg(0))] = arg(1) & 0xFFFF; }
void sdGetCoreAttr() { retU(valueOr(g_coreAttrs, static_cast<uint16_t>(arg(0)), 0)); }

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
    case 0x01: g_params[entry] = value & 0xFFFF; return 0;
    case 0x02: g_switches[entry] = value & 0xFFFFFF; return 0;
    case 0x03: g_addrs[entry] = value; return 0;
    case 0x04: g_coreAttrs[entry] = value & 0xFFFF; return 0;
    case 0x05: mem::write32(value, entry); return 0;                  // WRITEIOP
    case 0x06: write32(value, entry); return 0;                       // WRITEEE
    case 0x07:                                                        // EERETURN: the results so far to EE memory
        for (uint32_t i = 0; i < entry / 4u && i < index && rets; ++i) write32(value + 4 * i, mem::read32(rets + 4 * i));
        return 0;
    case 0x11: return (entry & 0xFF00) == kEnvx ? 0 : valueOr(g_params, entry, 0);
    case 0x12: return (entry & 0xFF00) == kEndx ? 0xFFFFFF : valueOr(g_switches, entry, 0);
    case 0x13: return valueOr(g_addrs, entry, 0);
    case 0x14: return valueOr(g_coreAttrs, entry, 0);
    default: ok = false; return 0;
    }
}

void procBatch(uint32_t batch, uint32_t rets, uint32_t num, int voiceMask) {
    for (uint32_t i = 0; i < num; ++i) {
        const uint32_t at = batch + 8 * i;
        const auto func = static_cast<uint16_t>(mem::read16(at));
        auto entry = static_cast<uint16_t>(mem::read16(at + 2));
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
    const uint32_t iopAddr = arg(2), spuAddr = arg(3) & (kSpuRamSize - 1), size = arg(4);
    Core& c = g_core[core];
    if (c.voiceBusy) return ret(-1);
    const uint32_t n = std::min(size, kSpuRamSize - spuAddr);
    switch (mode & 3) {
    case kTransWrite:
        for (uint32_t i = 0; i < n; ++i) g_spuRam[spuAddr + i] = mem::read8(iopAddr + i);
        break;
    case kTransRead:
        for (uint32_t i = 0; i < n; ++i) mem::write8(iopAddr + i, g_spuRam[spuAddr + i]);
        break;
    default:
        return ret(-1);
    }
    c.voiceIo = (mode & kTransIo) != 0;
    if (!c.voiceIo) {   // DMA: completes a little later and raises the transfer interrupt
        c.voiceBusy = true;
        c.voiceEvent = scheduleEvent(nowCycles() + (1000 + size / 4) * kEeCyclesPerIopCycle, &voiceDone,
                                     static_cast<uint64_t>(core));
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
    if ((mode & 3) == kTransStop) {
        const uint32_t where = c.blockActive ? c.blockAddr + c.blockHalf * c.blockSize : 0;
        stopBlock(c);
        return retU(where);
    }
    if (c.blockActive) return ret(-1);
    c.blockLoop = (mode & kTransLoop) != 0;
    c.blockRead = (mode & 3) == kTransRead;
    c.blockAddr = arg(2);
    c.blockSize = c.blockLoop ? arg(3) / 2 : arg(3);
    c.blockHalf = 0;
    if ((mode & 3) == kTransWriteFrom && c.blockLoop && arg(4) >= c.blockAddr + c.blockSize) c.blockHalf = 1;
    c.blockActive = true;
    c.blockEvent = scheduleEvent(nowCycles() + pcmCycles(c.blockSize), &blockTick, static_cast<uint64_t>(core));
    ret(0);
}

void sdBlockTransStatus() {
    const Core& c = g_core[sarg(0) & 1];
    const uint32_t addr = c.blockActive ? c.blockAddr + c.blockHalf * c.blockSize : 0;
    retU((c.blockHalf << 24) | (addr & 0xFFFFFF));
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
void sdStopTrans() {   // (channel): stops the block transfer
    stopBlock(g_core[sarg(0) & 1]);
    ret(0);
}

// sceSdEffectAttr: core, mode (ints), depth_L, depth_R (shorts), delay, feedback (ints).
void sdSetEffectAttr() {
    const uint32_t a = arg(1);
    std::vector<int16_t> v;
    for (uint32_t i = 0; i < 20; i += 2) v.push_back(static_cast<int16_t>(mem::read16(a + i)));
    g_effect[sarg(0) & 1] = v;
    ret(0);
}
void sdGetEffectAttr() {
    const uint32_t a = arg(1);
    const auto it = g_effect.find(sarg(0) & 1);
    for (uint32_t i = 0; i < 20; i += 2)
        mem::write16(a + i, it == g_effect.end() ? 0 : static_cast<uint16_t>(it->second[i / 2]));
    mem::write32(a, static_cast<uint32_t>(sarg(0) & 1));
}
void sdZero() { ret(0); }

} // namespace

void init() {
    g_spuRam.assign(kSpuRamSize, 0);
    for (Core& c : g_core) c = Core{};
    g_params.clear();
    g_switches.clear();
    g_addrs.clear();
    g_coreAttrs.clear();
    g_effect.clear();
    g_spu2Intr = g_irqCallback = Handler{};

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
        {25, "sceSdClearEffectWorkArea", &sdZero},
        {26, "sceSdSetTransIntrHandler", &sdSetTransIntrHandler},
        {27, "sceSdSetSpu2IntrHandler", &sdSetSpu2IntrHandler},
        {28, "sceSdGetTransIntrHandlerArgument", &sdGetTransIntrHandlerArgument},
        {29, "sceSdGetSpu2IntrHandlerArgument", &sdGetSpu2IntrHandlerArgument},
        {30, "sceSdStopTrans", &sdStopTrans},
        {31, "sceSdCleanEffectWorkArea", &sdZero},
        {32, "sceSdSetEffectMode", &sdSetEffectAttr},
        {33, "sceSdSetEffectModeParams", &sdSetEffectAttr},
    });
}

} // namespace ps2::iop::sound
