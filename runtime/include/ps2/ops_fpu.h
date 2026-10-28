// R5900 COP1: the single-precision FPU.
//
// The EE FPU is not IEEE 754: it has no infinities, NaNs or denormals.
// Operands with exponent 255 read as +/-FLT_MAX, denormals read as +/-0, and
// results that overflow clamp to +/-FLT_MAX (setting O/SO) while underflows
// flush to zero (U/SU). Registers keep raw bit patterns so MFC1/MTC1 and
// loads/stores round-trip exactly. Rounding is toward zero on hardware; the
// runtime sets the host rounding mode accordingly (see Options::fpuRoundToZero).
#pragma once

#include "ps2/cpu.h"
#include "ps2/memory.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace ps2::ops {

namespace fpu {
constexpr uint32_t kC = 0x00800000;
constexpr uint32_t kI = 0x00020000, kD = 0x00010000, kO = 0x00008000, kU = 0x00004000;
constexpr uint32_t kSI = 0x00000040, kSD = 0x00000020, kSO = 0x00000010, kSU = 0x00000008;
constexpr uint32_t kPosMax = 0x7F7FFFFF;

// Operand conversion: denormal -> 0, Inf/NaN -> +/-FLT_MAX.
inline float in(uint32_t f) {
    switch (f & 0x7F800000) {
    case 0: f &= 0x80000000; break;
    case 0x7F800000: f = (f & 0x80000000) | kPosMax; break;
    default: break;
    }
    return std::bit_cast<float>(f);
}
inline uint32_t bits(float f) { return std::bit_cast<uint32_t>(f); }

// Result conversion with overflow/underflow handling. oFlags/uFlags are the
// FCR31 bits to set; passing kO/kU in them also clears the non-sticky flag
// when the condition does not occur.
inline uint32_t out(float r, uint32_t oFlags, uint32_t uFlags) {
    uint32_t x = bits(r);
    if ((x & 0x7FFFFFFF) == 0x7F800000) {
        cpu.fcr31 |= oFlags;
        return (x & 0x80000000) | kPosMax;
    }
    if (oFlags & kO) cpu.fcr31 &= ~kO;
    if ((x & 0x7F800000) == 0 && (x & 0x007FFFFF) != 0) {
        cpu.fcr31 |= uFlags;
        return x & 0x80000000;
    }
    if (uFlags & kU) cpu.fcr31 &= ~kU;
    return x;
}
constexpr uint32_t kOF = kO | kSO, kUF = kU | kSU;

inline uint32_t maxBits(uint32_t a, uint32_t b) {
    const auto sa = static_cast<int32_t>(a), sb = static_cast<int32_t>(b);
    return static_cast<uint32_t>((sa < 0 && sb < 0) ? (sa < sb ? sa : sb) : (sa > sb ? sa : sb));
}
inline uint32_t minBits(uint32_t a, uint32_t b) {
    const auto sa = static_cast<int32_t>(a), sb = static_cast<int32_t>(b);
    return static_cast<uint32_t>((sa < 0 && sb < 0) ? (sa > sb ? sa : sb) : (sa < sb ? sa : sb));
}
} // namespace fpu

inline float FPR(uint32_t f) { return fpu::in(cpu.fpr[f]); }

inline void MFC1(uint32_t rt, uint32_t fs) { setU32(rt, cpu.fpr[fs]); }
inline void MTC1(uint32_t rt, uint32_t fs) { cpu.fpr[fs] = ru32(rt); }
inline void CFC1(uint32_t rt, uint32_t fs) {
    setS32(rt, static_cast<int32_t>(fs == 31 ? cpu.fcr31 : fs == 0 ? 0x2E00u : 0u));
}
inline void CTC1(uint32_t rt, uint32_t fs) {
    if (fs == 31) cpu.fcr31 = ru32(rt);
}
inline void LWC1(uint32_t ft, uint32_t base, int32_t off) { cpu.fpr[ft] = read32(ru32(base) + static_cast<uint32_t>(off)); }
inline void SWC1(uint32_t ft, uint32_t base, int32_t off) { write32(ru32(base) + static_cast<uint32_t>(off), cpu.fpr[ft]); }

inline void ADD_S(uint32_t fd, uint32_t fs, uint32_t ft) { cpu.fpr[fd] = fpu::out(FPR(fs) + FPR(ft), fpu::kOF, fpu::kUF); }
inline void SUB_S(uint32_t fd, uint32_t fs, uint32_t ft) { cpu.fpr[fd] = fpu::out(FPR(fs) - FPR(ft), fpu::kOF, fpu::kUF); }
inline void MUL_S(uint32_t fd, uint32_t fs, uint32_t ft) { cpu.fpr[fd] = fpu::out(FPR(fs) * FPR(ft), fpu::kOF, fpu::kUF); }
inline void ADDA_S(uint32_t fs, uint32_t ft) { cpu.facc = fpu::out(FPR(fs) + FPR(ft), fpu::kOF, fpu::kUF); }
inline void SUBA_S(uint32_t fs, uint32_t ft) { cpu.facc = fpu::out(FPR(fs) - FPR(ft), fpu::kOF, fpu::kUF); }
inline void MULA_S(uint32_t fs, uint32_t ft) { cpu.facc = fpu::out(FPR(fs) * FPR(ft), fpu::kOF, fpu::kUF); }
inline void MADD_S(uint32_t fd, uint32_t fs, uint32_t ft) {
    const float p = fpu::in(fpu::bits(FPR(fs) * FPR(ft)));
    cpu.fpr[fd] = fpu::out(fpu::in(cpu.facc) + p, fpu::kOF, fpu::kUF);
}
inline void MSUB_S(uint32_t fd, uint32_t fs, uint32_t ft) {
    const float p = fpu::in(fpu::bits(FPR(fs) * FPR(ft)));
    cpu.fpr[fd] = fpu::out(fpu::in(cpu.facc) - p, fpu::kOF, fpu::kUF);
}
inline void MADDA_S(uint32_t fs, uint32_t ft) {
    cpu.facc = fpu::out(fpu::in(cpu.facc) + FPR(fs) * FPR(ft), fpu::kOF, fpu::kUF);
}
inline void MSUBA_S(uint32_t fs, uint32_t ft) {
    cpu.facc = fpu::out(fpu::in(cpu.facc) - FPR(fs) * FPR(ft), fpu::kOF, fpu::kUF);
}

inline void DIV_S(uint32_t fd, uint32_t fs, uint32_t ft) {
    const uint32_t divisor = cpu.fpr[ft], dividend = cpu.fpr[fs];
    if ((divisor & 0x7F800000) == 0) {
        cpu.fcr31 |= (dividend & 0x7F800000) == 0 ? (fpu::kI | fpu::kSI) : (fpu::kD | fpu::kSD);
        cpu.fpr[fd] = ((divisor ^ dividend) & 0x80000000) | fpu::kPosMax;
        return;
    }
    cpu.fpr[fd] = fpu::out(fpu::in(dividend) / fpu::in(divisor), 0, 0);
}
inline void SQRT_S(uint32_t fd, uint32_t ft) {
    cpu.fcr31 &= ~(fpu::kI | fpu::kD);
    const uint32_t t = cpu.fpr[ft];
    if ((t & 0x7F800000) == 0) {
        cpu.fpr[fd] = t & 0x80000000;
    } else if (t & 0x80000000) {
        cpu.fcr31 |= fpu::kI | fpu::kSI;
        cpu.fpr[fd] = fpu::bits(std::sqrt(std::fabs(fpu::in(t))));
    } else {
        cpu.fpr[fd] = fpu::bits(std::sqrt(fpu::in(t)));
    }
}
inline void RSQRT_S(uint32_t fd, uint32_t fs, uint32_t ft) {
    cpu.fcr31 &= ~(fpu::kD | fpu::kI);
    const uint32_t t = cpu.fpr[ft];
    if ((t & 0x7F800000) == 0) {
        cpu.fcr31 |= fpu::kD | fpu::kSD;
        cpu.fpr[fd] = (t & 0x80000000) | fpu::kPosMax;
        return;
    }
    if (t & 0x80000000) cpu.fcr31 |= fpu::kI | fpu::kSI;
    const float root = fpu::in(fpu::bits(std::sqrt(std::fabs(fpu::in(t)))));
    cpu.fpr[fd] = fpu::out(FPR(fs) / root, 0, 0);
}

inline void ABS_S(uint32_t fd, uint32_t fs) {
    cpu.fpr[fd] = cpu.fpr[fs] & 0x7FFFFFFF;
    cpu.fcr31 &= ~(fpu::kO | fpu::kU);
}
inline void NEG_S(uint32_t fd, uint32_t fs) {
    cpu.fpr[fd] = cpu.fpr[fs] ^ 0x80000000;
    cpu.fcr31 &= ~(fpu::kO | fpu::kU);
}
inline void MOV_S(uint32_t fd, uint32_t fs) { cpu.fpr[fd] = cpu.fpr[fs]; }
inline void MAX_S(uint32_t fd, uint32_t fs, uint32_t ft) {
    cpu.fpr[fd] = fpu::maxBits(cpu.fpr[fs], cpu.fpr[ft]);
    cpu.fcr31 &= ~(fpu::kO | fpu::kU);
}
inline void MIN_S(uint32_t fd, uint32_t fs, uint32_t ft) {
    cpu.fpr[fd] = fpu::minBits(cpu.fpr[fs], cpu.fpr[ft]);
    cpu.fcr31 &= ~(fpu::kO | fpu::kU);
}

// Truncates toward zero; values outside int32 saturate by sign.
inline void CVT_W_S(uint32_t fd, uint32_t fs) {
    const uint32_t f = cpu.fpr[fs];
    if ((f & 0x7F800000) <= 0x4E800000) cpu.fpr[fd] = static_cast<uint32_t>(static_cast<int32_t>(std::bit_cast<float>(f)));
    else cpu.fpr[fd] = (f & 0x80000000) ? 0x80000000u : 0x7FFFFFFFu;
}
inline void CVT_S_W(uint32_t fd, uint32_t fs) {
    cpu.fpr[fd] = fpu::bits(static_cast<float>(static_cast<int32_t>(cpu.fpr[fs])));
}

inline void setCond(bool c) { cpu.fcr31 = c ? (cpu.fcr31 | fpu::kC) : (cpu.fcr31 & ~fpu::kC); }
inline void C_F_S(uint32_t, uint32_t) { cpu.fcr31 &= ~fpu::kC; }
inline void C_EQ_S(uint32_t fs, uint32_t ft) { setCond(FPR(fs) == FPR(ft)); }
inline void C_LT_S(uint32_t fs, uint32_t ft) { setCond(FPR(fs) < FPR(ft)); }
inline void C_LE_S(uint32_t fs, uint32_t ft) { setCond(FPR(fs) <= FPR(ft)); }

// Branch conditions (used by the generated BC1T/BC1F code).
inline bool fpuCond() { return (cpu.fcr31 & fpu::kC) != 0; }

} // namespace ps2::ops
