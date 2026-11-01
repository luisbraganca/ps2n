// Random VU micro programs and states for the vu_compiled test: the same
// generator writes the programs (generate.cpp) and the states they run from
// (check.cpp). Operations that would leave the unit are left out: XGKICK
// (the GS would draw the garbage), unknown words and, on VU0, the EFU's
// operations (they report themselves).
#pragma once

#include "ps2n/vu_micro.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace vufuzz {

class Random {
public:
    explicit Random(uint64_t seed) : rng_(seed) {}
    uint32_t below(uint32_t n) { return static_cast<uint32_t>(rng_() % n); }
    bool chance(uint32_t percent) { return below(100) < percent; }
    uint64_t bits() { return rng_(); }

    // Floats that reach the edges: zeros, denormals, exponent 255, the largest
    // values, small integers, values near 1, and plain ones.
    uint32_t floatBits() {
        const uint32_t sign = below(2) << 31;
        switch (below(14)) {
        case 0: return 0;
        case 1: return 0x80000000u;
        case 2: return sign | (below(0x800000) + 1);
        case 3: return sign | 0x7F800000u | below(0x800000);
        case 4: return sign | 0x7F7FFFFFu;
        case 5: {
            const float f = static_cast<float>(static_cast<int>(below(200)) - 100);
            uint32_t v;
            std::memcpy(&v, &f, 4);
            return v;
        }
        case 6: return below(0x10000);
        case 7: return 0x3F800000u + below(0x100000) - 0x80000u;
        case 8: return sign | ((below(10) + 120) << 23) | below(0x800000);
        case 9: return sign | ((below(3) + 252) << 23) | below(0x800000);
        case 10: return sign | ((below(3) + 1) << 23) | below(0x800000);
        case 11: return static_cast<uint32_t>(bits());
        default: return sign | ((below(60) + 97) << 23) | below(0x800000);
        }
    }

    // A pair, with the E bit `ePercent` of the time; branches are kept
    // `branchPercent` of the time (else drawn again), so programs run longer
    // straight stretches and loops.
    void pair(uint8_t* micro, uint32_t pc, uint32_t ePercent, uint32_t branchPercent, int unit = 1) {
        uint32_t upper = 0, lower = 0;
        for (;;) {
            upper = upperWord();
            const bool ibit = chance(8);
            lower = ibit ? floatBits() : lowerWord();
            if (ibit) upper |= 0x80000000u;
            const ps2n::vu::Pair p = ps2n::vu::decodePair(lower, upper, unit);
            if (p.upper.op == ps2n::vu::UpperOp::Unknown) continue;
            if (!ibit && (p.lower.op == ps2n::vu::LowerOp::Unknown || p.lower.op == ps2n::vu::LowerOp::Xgkick)) continue;
            using ps2n::vu::LowerOp;
            if (!ibit && unit == 0 && (p.lower.op == LowerOp::Esum || p.lower.op == LowerOp::Eatan2 || p.lower.op == LowerOp::Escalar))
                continue;   // VU0 has no EFU
            if (!ibit && p.lower.branch && !chance(branchPercent)) continue;
            break;
        }
        if (chance(ePercent)) upper |= 0x40000000u;
        std::memcpy(micro + pc, &lower, 4);
        std::memcpy(micro + pc + 4, &upper, 4);
    }

    void memory(uint8_t* m, size_t n) {
        for (size_t i = 0; i < n; i += 4) {
            const uint32_t v = floatBits();
            std::memcpy(m + i, &v, 4);
        }
    }

    // VF1-31 and ACC, VI1-15 (mostly addresses in data memory), and the
    // control registers a program reads.
    void registers(uint32_t (*vf)[4], uint32_t* acc, uint32_t* vi) {
        for (int r = 1; r < 32; ++r)
            for (int c = 0; c < 4; ++c) vf[r][c] = floatBits();
        for (int c = 0; c < 4; ++c) acc[c] = floatBits();
        for (int r = 1; r < 16; ++r) vi[r] = chance(70) ? below(0x400) : below(0x10000);
        vi[16] = below(0x1000);                                                  // STATUS
        vi[17] = chance(90) ? below(0x10000) : static_cast<uint32_t>(bits());   // MAC
        vi[18] = below(0x1000000);                                               // CLIP
        vi[20] = 0x3F800000u | below(0x800000);                                  // R
        vi[21] = floatBits();                                                    // I
        vi[22] = floatBits();                                                    // Q
        vi[23] = floatBits();                                                    // P
    }

private:
    uint32_t reg() { return chance(80) ? below(8) : below(32); }
    uint32_t dest() {
        static const uint32_t common[] = {0xF, 0xE, 0x8, 0x4, 0x2, 0x1, 0xC, 0x3};
        return chance(70) ? common[below(8)] : below(16);
    }
    uint32_t upperWord() {
        uint32_t c;
        if (chance(55)) {
            c = (dest() << 21) | (reg() << 16) | (reg() << 11) | (reg() << 6) | below(0x30);
        } else {
            static const uint32_t ext[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
                                           0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
                                           0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                           0x27, 0x28, 0x29, 0x2A, 0x2C, 0x2D, 0x2E, 0x2F, 0x2F, 0x2F, 0x2F, 0x2F};
            const uint32_t e = ext[below(sizeof ext / 4)];
            c = (dest() << 21) | (reg() << 16) | (reg() << 11) | ((e >> 2) << 6) | 0x3C | (e & 3);
        }
        if (chance(3)) c |= below(8) << 27;   // M D T
        return c;
    }
    uint32_t lowerWord() {
        const uint32_t d = dest(), t = reg() & (chance(80) ? 15 : 31), s = reg() & (chance(80) ? 15 : 31);
        const uint32_t imm11 =
            chance(70) ? (static_cast<uint32_t>(static_cast<int>(below(13)) - (chance(70) ? 1 : 6)) & 0x7FF) : below(0x800);
        switch (below(30)) {
        case 0: case 1: case 2: return (0x00u << 25) | (d << 21) | (t << 16) | (s << 11) | below(64);   // LQ
        case 3: case 4: return (0x01u << 25) | (d << 21) | (t << 16) | (s << 11) | below(64);           // SQ
        case 5: return (0x04u << 25) | (d << 21) | (t << 16) | (s << 11) | below(64);                   // ILW
        case 6: return (0x05u << 25) | (d << 21) | (t << 16) | (s << 11) | below(64);                   // ISW
        case 7: case 8: return ((0x08u + below(2)) << 25) | (below(16) << 21) | (t << 16) | (s << 11) | below(0x800);   // IADDIU ISUBIU
        case 9: return ((0x10u + below(4)) << 25) | (chance(50) ? below(0x1000000) : (chance(50) ? 0xFFFFFFu : 0));      // FCxx
        case 10: return ((0x14u + below(4)) << 25) | (below(2) << 21) | (t << 16) | below(0x800);                         // FSxx
        case 11: {
            static const uint32_t o[] = {0x18, 0x1A, 0x1B, 0x1C};
            return (o[below(4)] << 25) | (t << 16) | (s << 11);
        }
        case 12: return ((0x20u + below(2)) << 25) | (t << 16) | imm11;   // B BAL
        case 13: return ((0x24u + below(2)) << 25) | (t << 16) | (s << 11);   // JR JALR
        case 14: case 15: case 16: {
            static const uint32_t o[] = {0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F};
            return (o[below(6)] << 25) | (t << 16) | (s << 11) | imm11;
        }
        case 17: case 18: {
            static const uint32_t o[] = {0x30, 0x31, 0x32, 0x34, 0x35};
            return (0x40u << 25) | (t << 16) | (s << 11) | (reg() << 6) | o[below(5)];
        }
        default: {
            static const uint32_t ext[] = {0x30, 0x30, 0x31, 0x34, 0x35, 0x36, 0x37, 0x38, 0x38, 0x39, 0x3A, 0x3B, 0x3B, 0x3C,
                                           0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x64, 0x68, 0x69, 0x70, 0x71, 0x72,
                                           0x73, 0x74, 0x75, 0x76, 0x78, 0x79, 0x7A, 0x7B, 0x7B, 0x7C, 0x7D, 0x7E};
            const uint32_t e = ext[below(sizeof ext / 4)];
            return (0x40u << 25) | (d << 21) | (t << 16) | (s << 11) | ((e >> 2) << 6) | 0x3C | (e & 3);
        }
        }
    }

    std::mt19937_64 rng_;
};

} // namespace vufuzz
