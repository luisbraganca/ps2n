// VIF command processing: register-setting codes, MPG (micro program
// upload), UNPACK (every format, with write masks, the offset/difference
// modes, skipping and filling writes), MSCAL/MSCNT (start the VU, with VIF1's
// TOP/TOPS double buffering) and DIRECT/DIRECTHL (GIF PATH2).
#include "hw/vif.h"

#include "hw/gs.h"
#include "log.h"
#include "ps2/ops_vu0.h"
#include "vu.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace ps2::vif {

namespace {

struct Unit {
    uint32_t stat = 0, fbrst = 0, err = 0, mark = 0, cycle = 0, mode = 0, num = 0, mask = 0, code = 0;
    uint32_t itops = 0, base = 0, ofst = 0, tops = 0, itop = 0, top = 0;
    uint32_t row[4] = {}, col[4] = {};
    // In-progress command
    uint32_t cmd = 0, imm = 0, cmdNum = 0;
    uint32_t wordsLeft = 0;       // payload words still expected
    std::vector<uint32_t> payload;
};

Unit g_vif[2];

uint8_t* vuMem(int unit) { return unit == 0 ? vu0Mem : vu1Mem; }
uint32_t vuMemSize(int unit) { return unit == 0 ? 4096u : 16384u; }
uint8_t* vuMicro(int unit) { return unit == 0 ? vu0Micro : vu1Micro; }
uint32_t vuMicroSize(int unit) { return unit == 0 ? 4096u : 16384u; }

// Bytes of source data per vector, by the low four bits of the UNPACK code.
constexpr uint32_t kVectorBytes[16] = {4, 2, 1, 0, 8, 4, 2, 0, 12, 6, 3, 0, 16, 8, 4, 2};

uint32_t cycleCl(const Unit& u) { return u.cycle & 0xFF; }
uint32_t cycleWl(const Unit& u) { return ((u.cycle >> 8) & 0xFF) ? (u.cycle >> 8) & 0xFF : 256; }

// Payload size in 32-bit words for UNPACK. A filling write (CL < WL) only
// consumes data for the first CL of every WL vectors.
uint32_t unpackWords(const Unit& u, uint32_t cmd, uint32_t num) {
    const uint32_t cl = cycleCl(u), wl = cycleWl(u);
    uint32_t vectors = num;
    if (cl < wl) vectors = cl * (num / wl) + std::min(num % wl, cl);
    return (vectors * kVectorBytes[cmd & 0xF] + 3) / 4;
}

// One component written as the mask and mode say (masks: 0 data, 1 ROW,
// 2 COL, 3 write-protected; modes: 1 adds ROW, 2 accumulates into ROW).
void writeComponent(Unit& u, uint32_t& dst, uint32_t comp, uint32_t cyclePos, uint32_t value, bool masked, uint32_t mode) {
    const uint32_t m = masked ? (u.mask >> (std::min(cyclePos, 3u) * 8 + comp * 2)) & 3 : 0;
    switch (m) {
    case 0:
        switch (mode) {
        case 1: dst = value + u.row[comp]; break;
        case 2: dst = u.row[comp] += value; break;
        case 3: dst = u.row[comp] = value; break;
        default: dst = value; break;
        }
        break;
    case 1: dst = u.row[comp]; break;
    case 2: dst = u.col[std::min(cyclePos, 3u)]; break;
    default: break;
    }
}

void doUnpack(int unit, Unit& u) {
    const uint32_t vl = u.cmd & 3, vn = (u.cmd >> 2) & 3;
    const bool usn = (u.imm >> 14) & 1;
    const bool masked = (u.cmd & 0x10) != 0;
    const bool v45 = vn == 3 && vl == 3;
    const uint32_t mode = v45 ? 0 : u.mode;
    uint32_t addr = u.imm & 0x3FF;
    if (((u.imm >> 15) & 1) && unit == 1) addr += u.tops;
    const uint32_t num = u.cmdNum ? u.cmdNum : 256;
    const uint32_t cl = cycleCl(u), wl = cycleWl(u);
    const bool fill = cl < wl;
    const uint32_t vsize = kVectorBytes[u.cmd & 0xF];
    const auto* src = reinterpret_cast<const uint8_t*>(u.payload.data());
    const size_t avail = u.payload.size() * 4;
    uint8_t* mem = vuMem(unit);
    const uint32_t memQw = vuMemSize(unit) / 16;

    // Element `i` of the vector at byte offset `pos` (zero past the end of the payload).
    const uint32_t ebytes = 4u >> vl;
    auto element = [&](size_t pos, uint32_t i) -> uint32_t {
        const size_t at = pos + i * ebytes;
        if (at + ebytes > avail) return 0;
        if (ebytes == 4) {
            uint32_t x;
            std::memcpy(&x, src + at, 4);
            return x;
        }
        if (ebytes == 2) {
            uint16_t h;
            std::memcpy(&h, src + at, 2);
            return usn ? h : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(h)));
        }
        return usn ? src[at] : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(src[at])));
    };

    size_t pos = 0;
    uint32_t cyclePos = 0;
    for (uint32_t i = 0; i < num; ++i) {
        uint32_t v[4];
        if (v45) {
            uint16_t h = 0;
            if (pos + 2 <= avail) std::memcpy(&h, src + pos, 2);
            v[0] = (h & 0x1F) << 3;
            v[1] = ((h >> 5) & 0x1F) << 3;
            v[2] = ((h >> 10) & 0x1F) << 3;
            v[3] = ((h >> 15) & 1) << 7;
        } else if (vn == 0) {   // S: one element to xyzw
            v[0] = v[1] = v[2] = v[3] = element(pos, 0);
        } else if (vn == 1) {   // V2 writes x y x y on hardware
            v[0] = v[2] = element(pos, 0);
            v[1] = v[3] = element(pos, 1);
        } else {                // V3 takes w from the next element; V4 is all four
            for (uint32_t c = 0; c < 4; ++c) v[c] = element(pos, c);
        }
        auto* dst = reinterpret_cast<uint32_t*>(mem + (addr % memQw) * 16);
        for (uint32_t c = 0; c < 4; ++c) writeComponent(u, dst[c], c, cyclePos, v[c], masked, mode);
        ++addr;
        ++cyclePos;
        if (fill) {
            if (cyclePos <= cl) pos += vsize;
            else if (cyclePos == wl) cyclePos = 0;
        } else {
            pos += vsize;
            if (cyclePos >= wl) {
                addr += cl - wl;
                cyclePos = 0;
            }
        }
    }
}

// MSCAL / MSCALF / MSCNT: latch ITOP (and on VIF1 TOP, flipping the double buffer), then run the VU.
void startMicro(int unit, Unit& u, uint32_t addrBytes) {
    u.itop = u.itops;
    if (unit == 1) {
        u.top = u.tops & 0x3FF;
        if (u.stat & 0x80) {   // DBF
            u.tops = u.base;
            u.stat &= ~0x80u;
        } else {
            u.tops = u.base + u.ofst;
            u.stat |= 0x80u;
        }
        vu::runVu1(addrBytes);
    } else {
        vu0CallMicro(addrBytes);
    }
}

void finishCommand(int unit, Unit& u) {
    switch (u.cmd) {
    case 0x20: u.mask = u.payload.at(0); break;                                  // STMASK
    case 0x30: std::memcpy(u.row, u.payload.data(), 16); break;                  // STROW
    case 0x31: std::memcpy(u.col, u.payload.data(), 16); break;                  // STCOL
    case 0x4A: {                                                                  // MPG
        const uint32_t start = (u.imm * 8) % vuMicroSize(unit);
        const size_t bytes = u.payload.size() * 4;
        uint8_t* micro = vuMicro(unit);
        for (size_t i = 0; i < bytes; ++i)
            micro[(start + i) % vuMicroSize(unit)] = reinterpret_cast<const uint8_t*>(u.payload.data())[i];
        break;
    }
    case 0x50:
    case 0x51: {                                                                  // DIRECT / DIRECTHL
        std::vector<uint64_t> q(u.payload.size() / 2);
        std::memcpy(q.data(), u.payload.data(), q.size() * 8);
        gs::gifTransfer(q.data(), q.size() / 2, 2);
        break;
    }
    default:
        if ((u.cmd & 0x60) == 0x60) doUnpack(unit, u);
        break;
    }
    u.payload.clear();
    u.cmd = 0;
}

void beginCommand(int unit, Unit& u, uint32_t code) {
    u.code = code;
    u.imm = code & 0xFFFF;
    u.cmdNum = (code >> 16) & 0xFF;
    u.cmd = (code >> 24) & 0x7F;
    u.wordsLeft = 0;
    switch (u.cmd) {
    case 0x00: break;                                                            // NOP
    case 0x01: u.cycle = u.imm; break;                                           // STCYCL
    case 0x02:                                                                   // OFFSET
        if (unit) {
            u.ofst = u.imm & 0x3FF;
            u.stat &= ~0x80u;   // DBF
            u.tops = u.base;
        }
        break;
    case 0x03: if (unit) u.base = u.imm & 0x3FF; break;                          // BASE
    case 0x04: u.itops = u.imm & (unit ? 0x3FFu : 0xFFu); break;                 // ITOP
    case 0x05: u.mode = u.imm & 3; break;                                        // STMOD
    case 0x06: gs::setPath3Masked((u.imm >> 15) & 1); break;                     // MSKPATH3
    case 0x07: u.mark = u.imm; break;                                            // MARK
    case 0x10: case 0x11: case 0x13: break;                                      // FLUSHE / FLUSH / FLUSHA
    case 0x14: case 0x15: startMicro(unit, u, u.imm * 8); break;               // MSCAL / MSCALF
    case 0x17: startMicro(unit, u, 0xFFFFFFFFu); break;                         // MSCNT
    case 0x20: u.wordsLeft = 1; break;                                           // STMASK
    case 0x30: case 0x31: u.wordsLeft = 4; break;                                // STROW / STCOL
    case 0x4A: u.wordsLeft = (u.cmdNum ? u.cmdNum : 256) * 2; break;             // MPG
    case 0x50: case 0x51:                                                        // DIRECT / DIRECTHL
        if (unit == 0) {
            PS2_UNIMPL("vif", "DIRECT on VIF0", "DIRECT is only valid on VIF1");
            break;
        }
        u.wordsLeft = (u.imm ? u.imm : 65536) * 4;
        break;
    default:
        if ((u.cmd & 0x60) == 0x60) {
            u.wordsLeft = unpackWords(u, u.cmd, u.cmdNum ? u.cmdNum : 256);
        } else {
            PS2_UNIMPL("vif", "unknown VIF code", "unknown VIF code 0x%08x on VIF%d", code, unit);
        }
        break;
    }
    if (u.wordsLeft == 0) u.cmd = 0;
}

void feedWord(int unit, uint32_t w) {
    Unit& u = g_vif[unit];
    if (u.wordsLeft) {
        u.payload.push_back(w);
        if (--u.wordsLeft == 0) finishCommand(unit, u);
        return;
    }
    beginCommand(unit, u, w);
}

} // namespace

void init() {
    for (auto& u : g_vif) u = Unit{};
}

uint32_t regRead(uint32_t addr) {
    const int unit = addr >= 0x10003C00u ? 1 : 0;
    const Unit& u = g_vif[unit];
    switch (addr & 0x3F0) {
    case 0x000: return u.stat;
    case 0x010: return u.fbrst;
    case 0x020: return u.err;
    case 0x030: return u.mark;
    case 0x040: return u.cycle;
    case 0x050: return u.mode;
    case 0x060: return u.num;
    case 0x070: return u.mask;
    case 0x080: return u.code;
    case 0x090: return u.itops;
    case 0x0A0: return u.base;
    case 0x0B0: return u.ofst;
    case 0x0C0: return u.tops;
    case 0x0D0: return u.itop;
    case 0x0E0: return u.top;
    case 0x100: case 0x110: case 0x120: case 0x130: return u.row[((addr & 0x3F0) - 0x100) >> 4];
    case 0x140: case 0x150: case 0x160: case 0x170: return u.col[((addr & 0x3F0) - 0x140) >> 4];
    default: return 0;
    }
}

void regWrite(uint32_t addr, uint32_t v) {
    const int unit = addr >= 0x10003C00u ? 1 : 0;
    Unit& u = g_vif[unit];
    switch (addr & 0x3F0) {
    case 0x010:   // FBRST: reset clears the command state
        if (v & 1) u = Unit{};
        break;
    case 0x030: u.mark = v; break;
    case 0x020: u.err = v; break;
    default: break;
    }
}

void fifoWrite(int unit, const uint64_t q[2]) { transfer(unit, q, 1); }

uint32_t top(int unit) { return g_vif[unit & 1].top; }
uint32_t itop(int unit) { return g_vif[unit & 1].itop; }

void transfer(int unit, const uint64_t* qwords, size_t count) {
    const auto* w = reinterpret_cast<const uint32_t*>(qwords);
    for (size_t i = 0; i < count * 4; ++i) feedWord(unit, w[i]);
}

void transferTagWords(int unit, const uint64_t q[2]) {
    const auto* w = reinterpret_cast<const uint32_t*>(&q[1]);
    feedWord(unit, w[0]);
    feedWord(unit, w[1]);
}

} // namespace ps2::vif
