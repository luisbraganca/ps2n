#include "hw/dmac.h"

#include "hw/gs.h"
#include "hw/hw.h"
#include "hw/ipu.h"
#include "hw/vif.h"
#include "kernel/kernel.h"
#include "log.h"
#include "ps2/memory.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace ps2::dmac {

namespace {

constexpr uint32_t kBase[10] = {0x10008000u, 0x10009000u, 0x1000A000u, 0x1000B000u, 0x1000B400u,
                                0x1000C000u, 0x1000C400u, 0x1000C800u, 0x1000D000u, 0x1000D400u};
constexpr const char* kName[10] = {"VIF0", "VIF1", "GIF", "IPU_FROM", "IPU_TO", "SIF0", "SIF1", "SIF2", "SPR_FROM", "SPR_TO"};

struct Channel {
    uint32_t chcr = 0, madr = 0, qwc = 0, tadr = 0, asr0 = 0, asr1 = 0, sadr = 0;
    bool running = false;   // an IPU channel's transfer in progress
    bool lastTag = false;   // toIPU chain: the current tag's data ends the transfer
};

Channel g_ch[10];
uint32_t g_ctrl = 0, g_stat = 0, g_pcr = 0, g_sqwc = 0, g_rbsr = 0, g_rbor = 0, g_stadr = 0;
uint32_t g_enable = 0x1201;

bool held() { return (g_enable & 0x10000u) != 0; }   // D_ENABLE.CPND: every channel suspended

// A DMA address: bit 31 selects the scratchpad.
uint32_t memAddress(uint32_t addr) { return (addr & 0x80000000u) ? (kSprBase | (addr & 0x3FF0u)) : (addr & 0x01FFFFF0u); }

int channelOf(uint32_t addr) {
    for (int i = 9; i >= 0; --i)
        if (addr >= kBase[i]) return (addr - kBase[i]) < 0x100 ? i : -1;
    return -1;
}

// Reads n quadwords starting at a guest address (bit 31 of DMA addresses
// selects the scratchpad).
std::vector<uint64_t> fetch(uint32_t addr, uint32_t qwc) {
    std::vector<uint64_t> q(static_cast<size_t>(qwc) * 2);
    const uint32_t base = (addr & 0x80000000u) ? (kSprBase | (addr & 0x3FF0u)) : (addr & 0x01FFFFF0u);
    for (uint32_t i = 0; i < qwc; ++i) {
        const uint32_t a = (addr & 0x80000000u) ? (kSprBase | ((base + i * 16) & 0x3FF0u)) : base + i * 16;
        read128(a, &q[static_cast<size_t>(i) * 2]);
    }
    return q;
}

void deliver(int ch, const uint64_t* data, size_t qwc) {
    if (!qwc) return;
    switch (ch) {
    case hw::DMA_VIF0: vif::transfer(0, data, qwc); break;
    case hw::DMA_VIF1: vif::transfer(1, data, qwc); break;
    case hw::DMA_GIF: gs::gifTransfer(data, qwc, 3); break;
    case hw::DMA_SPR_TO: {   // chain mode: into the scratchpad from SADR on
        Channel& c = g_ch[ch];
        for (size_t i = 0; i < qwc; ++i) write128(kSprBase | ((c.sadr + static_cast<uint32_t>(i) * 16) & 0x3FF0u), data + i * 2);
        c.sadr = (c.sadr + static_cast<uint32_t>(qwc) * 16) & 0x3FF0u;
        break;
    }
    default: break;
    }
}

void runNormal(int ch) {
    Channel& c = g_ch[ch];
    const auto data = fetch(c.madr, c.qwc);
    deliver(ch, data.data(), c.qwc);
    c.madr += c.qwc * 16;
    c.qwc = 0;
}

// Source chain mode: follow DMAtags until an end condition.
void runChain(int ch) {
    Channel& c = g_ch[ch];
    const bool tte = (c.chcr >> 6) & 1;
    const bool tie = (c.chcr >> 7) & 1;
    int asp = static_cast<int>((c.chcr >> 4) & 3);
    if (c.qwc) runNormal(ch);   // residual data from a previous tag
    for (int guard = 0; guard < 1 << 20; ++guard) {
        uint64_t tag[2];
        const uint32_t ta = c.tadr;
        read128((ta & 0x80000000u) ? (kSprBase | (ta & 0x3FF0u)) : (ta & 0x01FFFFF0u), tag);
        const uint32_t qwc = static_cast<uint32_t>(tag[0] & 0xFFFF);
        const uint32_t id = static_cast<uint32_t>((tag[0] >> 28) & 7);
        const bool irq = (tag[0] >> 31) & 1;
        const uint32_t addr = static_cast<uint32_t>((tag[0] >> 32) & 0xFFFFFFF0u);
        c.chcr = (c.chcr & 0xFFFFu) | (static_cast<uint32_t>(tag[0]) & 0xFFFF0000u);
        if (tte && (ch == hw::DMA_VIF0 || ch == hw::DMA_VIF1)) {
            // The upper 64 bits of the tag are VIF codes, executed before the data.
            const uint64_t q[2] = {0, tag[1]};
            vif::transferTagWords(ch == hw::DMA_VIF0 ? 0 : 1, q);
        } else if (tte && ch == hw::DMA_SPR_TO) {
            deliver(ch, tag, 1);   // the whole tag goes to the scratchpad first
        }
        bool done = false;
        uint32_t dataAddr = ta + 16;
        switch (id) {
        case 0: dataAddr = addr; done = true; break;                       // refe
        case 1: c.tadr = ta + 16 + qwc * 16; break;                        // cnt
        case 2: c.tadr = addr; break;                                      // next
        case 3: case 4: dataAddr = addr; c.tadr = ta + 16; break;          // ref / refs
        case 5:                                                            // call
            if (asp == 0) c.asr0 = ta + 16 + qwc * 16;
            else c.asr1 = ta + 16 + qwc * 16;
            ++asp;
            c.tadr = addr;
            if (asp > 2) done = true;
            break;
        case 6:                                                            // ret
            if (asp > 0) {
                --asp;
                c.tadr = asp == 0 ? c.asr0 : c.asr1;
            } else {
                done = true;
            }
            break;
        case 7: done = true; break;                                        // end
        }
        const auto data = fetch(dataAddr, qwc);
        deliver(ch, data.data(), qwc);
        c.madr = dataAddr + qwc * 16;
        if (done || (irq && tie)) break;
    }
    c.chcr = (c.chcr & ~0x30u) | (static_cast<uint32_t>(asp & 3) << 4);
}

void runSpr(int ch) {
    Channel& c = g_ch[ch];
    const uint32_t mode = (c.chcr >> 2) & 3;
    if (mode != 0 && ch == hw::DMA_SPR_FROM) {
        PS2_UNIMPL("dmac", "SPR_FROM chain mode", "scratchpad DMA (from SPR) in chain mode is not implemented");
        return;
    }
    if (mode == 1) {   // SPR_TO source chain: the data goes to SADR sequentially
        runChain(ch);
        return;
    }
    for (uint32_t i = 0; i < c.qwc; ++i) {
        uint64_t q[2];
        const uint32_t sprAddr = kSprBase | ((c.sadr + i * 16) & 0x3FF0u);
        const uint32_t mem = (c.madr & 0x80000000u) ? (kSprBase | ((c.madr + i * 16) & 0x3FF0u)) : (c.madr + i * 16);
        if (ch == hw::DMA_SPR_FROM) {
            read128(sprAddr, q);
            write128(mem, q);
        } else {
            read128(mem, q);
            write128(sprAddr, q);
        }
    }
    c.madr += c.qwc * 16;
    c.sadr += c.qwc * 16;
    c.qwc = 0;
}

void finish(int ch) {
    g_ch[ch].chcr &= ~0x100u;   // STR off: done
    g_ch[ch].running = false;
    raise(ch);
}

// Source chain tags that end the transfer once their data has gone: refe,
// end, or an IRQ tag with TIE set (the tag sits in CHCR's upper half).
bool tagEnds(uint32_t chcr) {
    const uint32_t id = (chcr >> 28) & 7;
    return id == 0 || id == 7 || ((chcr >> 31) && (chcr & 0x80u));
}

// toIPU in chain mode: reads the next tag. False when the transfer is over.
bool nextToIpuTag(Channel& c) {
    if (((c.chcr >> 2) & 3) != 1 || c.lastTag) return false;
    uint64_t tag[2];
    read128(memAddress(c.tadr), tag);
    const uint32_t qwc = static_cast<uint32_t>(tag[0] & 0xFFFF);
    const uint32_t id = static_cast<uint32_t>((tag[0] >> 28) & 7);
    const uint32_t addr = static_cast<uint32_t>((tag[0] >> 32) & 0xFFFFFFF0u);
    int asp = static_cast<int>((c.chcr >> 4) & 3);
    bool end = false;
    c.chcr = (c.chcr & 0xFFFFu) | (static_cast<uint32_t>(tag[0]) & 0xFFFF0000u);
    c.qwc = qwc;
    switch (id) {
    case 0: c.madr = addr; c.tadr += 16; end = true; break;                    // refe
    case 1: c.madr = c.tadr + 16; c.tadr = c.madr + qwc * 16; break;           // cnt
    case 2: c.madr = c.tadr + 16; c.tadr = addr; break;                        // next
    case 3: case 4: c.madr = addr; c.tadr += 16; break;                        // ref / refs
    case 5:                                                                     // call
        c.madr = c.tadr + 16;
        (asp == 0 ? c.asr0 : c.asr1) = c.madr + qwc * 16;
        c.tadr = addr;
        end = ++asp > 2;
        break;
    case 6:                                                                     // ret
        c.madr = c.tadr + 16;
        if (asp > 0) c.tadr = --asp == 0 ? c.asr0 : c.asr1;
        else end = true;
        break;
    default: c.madr = c.tadr + 16; end = true; break;                          // end
    }
    c.chcr = (c.chcr & ~0x30u) | (static_cast<uint32_t>(asp & 3) << 4);
    c.lastTag = end || tagEnds(c.chcr);
    return true;
}

// toIPU's current data is used up: reads tags until there is more, or ends
// the transfer, as the DMAC does as soon as it reaches the end.
void settleToIpu() {
    Channel& c = g_ch[hw::DMA_IPU_TO];
    for (int guard = 0; c.running && c.qwc == 0 && guard < 4096; ++guard)
        if (!nextToIpuTag(c)) finish(hw::DMA_IPU_TO);
    if (c.running && c.qwc == 0) {   // a loop of empty tags: the console would spin here forever
        PS2_UNIMPL("dmac", "toIPU chain without data", "toIPU DMA chain at 0x%08x has no data in 4096 tags; ending it", c.tadr);
        finish(hw::DMA_IPU_TO);
    }
}

void start(int ch) {
    Channel& c = g_ch[ch];
    const uint32_t mode = (c.chcr >> 2) & 3;
    PS2_TRACE("dmac", "%s start chcr=0x%08x madr=0x%08x qwc=%u tadr=0x%08x", kName[ch], c.chcr, c.madr, c.qwc, c.tadr);
    if (ch == hw::DMA_IPU_FROM || ch == hw::DMA_IPU_TO) {
        // The IPU pulls and pushes the data; the transfer ends when it has all gone.
        c.running = true;
        c.lastTag = mode == 1 && c.qwc > 0 && tagEnds(c.chcr);   // restarted mid-tag
        if (ch == hw::DMA_IPU_TO) settleToIpu();
        else if (c.qwc == 0) finish(ch);
        ipu::run();
        return;
    }
    switch (ch) {
    case hw::DMA_VIF0:
    case hw::DMA_VIF1:
    case hw::DMA_GIF:
        if ((c.chcr & 1) == 0 && ch == hw::DMA_VIF1) {
            PS2_UNIMPL("dmac", "VIF1 to-memory transfer", "VIF1 DMA in the GS->EE direction (image readback) is not implemented");
            break;
        }
        if (mode == 1) runChain(ch);
        else runNormal(ch);
        break;
    case hw::DMA_SPR_FROM:
    case hw::DMA_SPR_TO:
        runSpr(ch);
        break;
    default: {
        char key[64];
        std::snprintf(key, sizeof key, "DMA channel %s", kName[ch]);
        PS2_UNIMPL("dmac", key, "DMA on channel %s is not modelled; completing it immediately", kName[ch]);
        c.qwc = 0;
        break;
    }
    }
    c.chcr &= ~0x100u;   // STR off: done
    raise(ch);
}

} // namespace

void init() {
    for (auto& c : g_ch) c = Channel{};
    g_ctrl = g_stat = g_pcr = g_sqwc = g_rbsr = g_rbor = g_stadr = 0;
    g_enable = 0x1201;
}

void resetInterrupts() { g_stat = 0; }

uint32_t regRead(uint32_t addr) {
    switch (addr) {
    case 0x1000E000u: return g_ctrl;
    case 0x1000E010u: return g_stat;
    case 0x1000E020u: return g_pcr;
    case 0x1000E030u: return g_sqwc;
    case 0x1000E040u: return g_rbsr;
    case 0x1000E050u: return g_rbor;
    case 0x1000E060u: return g_stadr;
    default: break;
    }
    const int ch = channelOf(addr);
    if (ch < 0) return 0;
    const Channel& c = g_ch[ch];
    switch (addr - kBase[ch]) {
    case 0x00: return c.chcr;
    case 0x10: return c.madr;
    case 0x20: return c.qwc;
    case 0x30: return c.tadr;
    case 0x40: return c.asr0;
    case 0x50: return c.asr1;
    case 0x80: return c.sadr;
    default: return 0;
    }
}

void regWrite(uint32_t addr, uint32_t v) {
    switch (addr) {
    case 0x1000E000u: g_ctrl = v; return;
    case 0x1000E010u:
        g_stat &= ~(v & 0xFFFFu);              // CIS/SIS/MEIS/BEIS: write 1 to clear
        g_stat ^= v & 0xFFFF0000u;             // CIM/SIM/MEIM: write 1 to toggle
        if (pending()) kernel::kickInterrupts();
        return;
    case 0x1000E020u: g_pcr = v; return;
    case 0x1000E030u: g_sqwc = v; return;
    case 0x1000E040u: g_rbsr = v; return;
    case 0x1000E050u: g_rbor = v; return;
    case 0x1000E060u: g_stadr = v; return;
    default: break;
    }
    const int ch = channelOf(addr);
    if (ch < 0) return;
    Channel& c = g_ch[ch];
    switch (addr - kBase[ch]) {
    case 0x00:
        c.chcr = (c.chcr & 0xFFFF0000u) | (v & 0xFFFFu);
        if (!(v & 0x100u)) c.running = false;   // STR off stops an IPU transfer
        else if (!held() && !c.running) start(ch);
        return;
    case 0x10: c.madr = v; return;
    case 0x20: c.qwc = v & 0xFFFF; return;
    case 0x30: c.tadr = v; return;
    case 0x40: c.asr0 = v; return;
    case 0x50: c.asr1 = v; return;
    case 0x80: c.sadr = v & 0x3FF0u; return;
    default: return;
    }
}

uint32_t enableRegRead(uint32_t) { return g_enable; }
void enableRegWrite(uint32_t, uint32_t v) {
    const bool released = held() && !(v & 0x10000u);
    g_enable = v;
    if (!released) return;
    // Channels started while the DMAC was held begin now; IPU transfers resume.
    for (int ch = 0; ch < 10; ++ch)
        if ((g_ch[ch].chcr & 0x100u) && !g_ch[ch].running) start(ch);
    ipu::run();
}

uint32_t toIpu(uint64_t* dst, uint32_t max) {
    Channel& c = g_ch[hw::DMA_IPU_TO];
    uint32_t moved = 0;
    while (moved < max && c.running && !held()) {
        if (c.qwc == 0) {
            settleToIpu();
            continue;
        }
        const uint32_t n = std::min(max - moved, c.qwc);
        for (uint32_t i = 0; i < n; ++i) read128(memAddress(c.madr + i * 16), dst + size_t{moved + i} * 2);
        c.madr += n * 16;
        c.qwc -= n;
        moved += n;
        if (c.qwc == 0) settleToIpu();
    }
    return moved;
}

bool toIpuReady() { return g_ch[hw::DMA_IPU_TO].running && !held(); }

uint32_t fromIpu(const uint64_t* src, uint32_t count) {
    Channel& c = g_ch[hw::DMA_IPU_FROM];
    if (!c.running || held()) return 0;
    const uint32_t n = std::min(count, c.qwc);
    for (uint32_t i = 0; i < n; ++i) write128(memAddress(c.madr + i * 16), src + size_t{i} * 2);
    c.madr += n * 16;
    c.qwc -= n;
    if (c.qwc == 0) finish(hw::DMA_IPU_FROM);
    return n;
}

uint32_t pending() { return (g_stat & 0x3FF) & ((g_stat >> 16) & 0x3FF); }
void ack(int channel) { g_stat &= ~(1u << channel); }
void setMask(int channel, bool enabled) {
    if (channel < 0 || channel > 9) return;
    if (enabled) g_stat |= 1u << (16 + channel);
    else g_stat &= ~(1u << (16 + channel));
    if (pending()) kernel::kickInterrupts();
}
void raise(int channel) {
    g_stat |= 1u << channel;
    if (g_stat & (1u << (16 + channel))) kernel::kickInterrupts();
}
bool idle() {
    // CPCOND0 is true when every channel selected in D_PCR.CPC has finished.
    const uint32_t cpc = g_pcr & 0x3FF;
    return (~(g_stat & 0x3FF) & cpc) == 0;
}

} // namespace ps2::dmac
