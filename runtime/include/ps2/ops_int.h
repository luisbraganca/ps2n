// R5900 integer, shift, multiply/divide and load/store semantics.
//
// One inline function per instruction, named after the mnemonic in upper
// case and taking register numbers. Generated code passes constants (the
// compiler folds everything down to plain arithmetic); the fallback
// interpreter passes decoded fields. Control-flow instructions are not here:
// the recompiler emits them as C++ control flow.
//
// Semantics follow the EE Core Instruction Set Manual; corner cases (division
// by zero, LWL/LWR merging, upper GPR halves) were checked against PCSX2's
// interpreter.
#pragma once

#include "ps2/cpu.h"
#include "ps2/memory.h"

#include <bit>
#include <cstdint>

namespace ps2 {

// Raised by ADD/ADDI/SUB/DADD/DADDI/DSUB on signed overflow. Real hardware
// takes an exception and leaves rd unchanged; we log and skip the write.
PS2_COLD void integerOverflow(uint32_t value);

namespace ops {

inline uint64_t sext32(uint32_t v) { return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v))); }

// ---- arithmetic -------------------------------------------------------------
inline void ADDU(uint32_t rd, uint32_t rs, uint32_t rt) { setU32(rd, ru32(rs) + ru32(rt)); }
inline void SUBU(uint32_t rd, uint32_t rs, uint32_t rt) { setU32(rd, ru32(rs) - ru32(rt)); }
inline void ADDIU(uint32_t rt, uint32_t rs, int32_t imm) { setU32(rt, ru32(rs) + static_cast<uint32_t>(imm)); }
inline void DADDU(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, ru64(rs) + ru64(rt)); }
inline void DSUBU(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, ru64(rs) - ru64(rt)); }
inline void DADDIU(uint32_t rt, uint32_t rs, int32_t imm) { setU64(rt, ru64(rs) + static_cast<uint64_t>(static_cast<int64_t>(imm))); }

inline void ADD(uint32_t rd, uint32_t rs, uint32_t rt) {
    const int64_t r = static_cast<int64_t>(rs32(rs)) + rs32(rt);
    if (r != static_cast<int32_t>(r)) return integerOverflow(static_cast<uint32_t>(r));
    setS32(rd, static_cast<int32_t>(r));
}
inline void ADDI(uint32_t rt, uint32_t rs, int32_t imm) {
    const int64_t r = static_cast<int64_t>(rs32(rs)) + imm;
    if (r != static_cast<int32_t>(r)) return integerOverflow(static_cast<uint32_t>(r));
    setS32(rt, static_cast<int32_t>(r));
}
inline void SUB(uint32_t rd, uint32_t rs, uint32_t rt) {
    const int64_t r = static_cast<int64_t>(rs32(rs)) - rs32(rt);
    if (r != static_cast<int32_t>(r)) return integerOverflow(static_cast<uint32_t>(r));
    setS32(rd, static_cast<int32_t>(r));
}
inline void DADD(uint32_t rd, uint32_t rs, uint32_t rt) {
    const uint64_t a = ru64(rs), b = ru64(rt), r = a + b;
    if (static_cast<int64_t>((a ^ r) & (b ^ r)) < 0) return integerOverflow(static_cast<uint32_t>(r));
    setU64(rd, r);
}
inline void DADDI(uint32_t rt, uint32_t rs, int32_t imm) {
    const uint64_t a = ru64(rs), b = static_cast<uint64_t>(static_cast<int64_t>(imm)), r = a + b;
    if (static_cast<int64_t>((a ^ r) & (b ^ r)) < 0) return integerOverflow(static_cast<uint32_t>(r));
    setU64(rt, r);
}
inline void DSUB(uint32_t rd, uint32_t rs, uint32_t rt) {
    const uint64_t a = ru64(rs), b = ru64(rt), r = a - b;
    if (static_cast<int64_t>((a ^ b) & (a ^ r)) < 0) return integerOverflow(static_cast<uint32_t>(r));
    setU64(rd, r);
}

// ---- logical / compare ------------------------------------------------------
inline void AND(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, ru64(rs) & ru64(rt)); }
inline void OR(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, ru64(rs) | ru64(rt)); }
inline void XOR(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, ru64(rs) ^ ru64(rt)); }
inline void NOR(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, ~(ru64(rs) | ru64(rt))); }
inline void ANDI(uint32_t rt, uint32_t rs, uint32_t imm) { setU64(rt, ru64(rs) & imm); }
inline void ORI(uint32_t rt, uint32_t rs, uint32_t imm) { setU64(rt, ru64(rs) | imm); }
inline void XORI(uint32_t rt, uint32_t rs, uint32_t imm) { setU64(rt, ru64(rs) ^ imm); }
inline void LUI(uint32_t rt, uint32_t imm) { setU32(rt, imm << 16); }
inline void SLT(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, rs64(rs) < rs64(rt) ? 1 : 0); }
inline void SLTU(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, ru64(rs) < ru64(rt) ? 1 : 0); }
inline void SLTI(uint32_t rt, uint32_t rs, int32_t imm) { setU64(rt, rs64(rs) < imm ? 1 : 0); }
inline void SLTIU(uint32_t rt, uint32_t rs, int32_t imm) {
    setU64(rt, ru64(rs) < static_cast<uint64_t>(static_cast<int64_t>(imm)) ? 1 : 0);
}
inline void MOVZ(uint32_t rd, uint32_t rs, uint32_t rt) {
    if (ru64(rt) == 0) setU64(rd, ru64(rs));
}
inline void MOVN(uint32_t rd, uint32_t rs, uint32_t rt) {
    if (ru64(rt) != 0) setU64(rd, ru64(rs));
}

// ---- shifts -----------------------------------------------------------------
inline void SLL(uint32_t rd, uint32_t rt, uint32_t sa) { setU32(rd, ru32(rt) << (sa & 31)); }
inline void SRL(uint32_t rd, uint32_t rt, uint32_t sa) { setU32(rd, ru32(rt) >> (sa & 31)); }
inline void SRA(uint32_t rd, uint32_t rt, uint32_t sa) { setS32(rd, rs32(rt) >> (sa & 31)); }
inline void SLLV(uint32_t rd, uint32_t rt, uint32_t rs) { setU32(rd, ru32(rt) << (ru32(rs) & 31)); }
inline void SRLV(uint32_t rd, uint32_t rt, uint32_t rs) { setU32(rd, ru32(rt) >> (ru32(rs) & 31)); }
inline void SRAV(uint32_t rd, uint32_t rt, uint32_t rs) { setS32(rd, rs32(rt) >> (ru32(rs) & 31)); }
inline void DSLL(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, ru64(rt) << (sa & 31)); }
inline void DSRL(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, ru64(rt) >> (sa & 31)); }
inline void DSRA(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, static_cast<uint64_t>(rs64(rt) >> (sa & 31))); }
inline void DSLL32(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, ru64(rt) << ((sa & 31) + 32)); }
inline void DSRL32(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, ru64(rt) >> ((sa & 31) + 32)); }
inline void DSRA32(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, static_cast<uint64_t>(rs64(rt) >> ((sa & 31) + 32))); }
inline void DSLLV(uint32_t rd, uint32_t rt, uint32_t rs) { setU64(rd, ru64(rt) << (ru32(rs) & 63)); }
inline void DSRLV(uint32_t rd, uint32_t rt, uint32_t rs) { setU64(rd, ru64(rt) >> (ru32(rs) & 63)); }
inline void DSRAV(uint32_t rd, uint32_t rt, uint32_t rs) { setU64(rd, static_cast<uint64_t>(rs64(rt) >> (ru32(rs) & 63))); }

// ---- HI/LO, multiply, divide ------------------------------------------------
// Pipeline 0 uses bits 0-63 of HI/LO (d[0]), pipeline 1 ("...1" ops) bits 64-127.
inline void mulResult(int pipe, uint32_t rd, uint64_t product) {
    cpu.lo.d[pipe] = sext32(static_cast<uint32_t>(product));
    cpu.hi.d[pipe] = sext32(static_cast<uint32_t>(product >> 32));
    setU64(rd, cpu.lo.d[pipe]);   // R5900 three-operand MULT also writes rd
}
inline uint64_t hiLo(int pipe) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cpu.hi.d[pipe])) << 32) | static_cast<uint32_t>(cpu.lo.d[pipe]);
}
inline void signedDiv(int pipe, int32_t a, int32_t b) {
    if (a == INT32_MIN && b == -1) {
        cpu.lo.d[pipe] = sext32(0x80000000u);
        cpu.hi.d[pipe] = 0;
    } else if (b != 0) {
        cpu.lo.d[pipe] = static_cast<uint64_t>(static_cast<int64_t>(a / b));
        cpu.hi.d[pipe] = static_cast<uint64_t>(static_cast<int64_t>(a % b));
    } else {   // R5900 divide-by-zero results
        cpu.lo.d[pipe] = static_cast<uint64_t>(static_cast<int64_t>(a < 0 ? 1 : -1));
        cpu.hi.d[pipe] = static_cast<uint64_t>(static_cast<int64_t>(a));
    }
}
inline void unsignedDiv(int pipe, uint32_t a, uint32_t b) {
    if (b != 0) {
        cpu.lo.d[pipe] = sext32(a / b);
        cpu.hi.d[pipe] = sext32(a % b);
    } else {
        cpu.lo.d[pipe] = ~0ull;
        cpu.hi.d[pipe] = sext32(a);
    }
}

inline void MULT(uint32_t rd, uint32_t rs, uint32_t rt) {
    mulResult(0, rd, static_cast<uint64_t>(static_cast<int64_t>(rs32(rs)) * rs32(rt)));
}
inline void MULTU(uint32_t rd, uint32_t rs, uint32_t rt) {
    mulResult(0, rd, static_cast<uint64_t>(ru32(rs)) * ru32(rt));
}
inline void MULT1(uint32_t rd, uint32_t rs, uint32_t rt) {
    mulResult(1, rd, static_cast<uint64_t>(static_cast<int64_t>(rs32(rs)) * rs32(rt)));
}
inline void MULTU1(uint32_t rd, uint32_t rs, uint32_t rt) {
    mulResult(1, rd, static_cast<uint64_t>(ru32(rs)) * ru32(rt));
}
inline void MADD(uint32_t rd, uint32_t rs, uint32_t rt) {
    mulResult(0, rd, hiLo(0) + static_cast<uint64_t>(static_cast<int64_t>(rs32(rs)) * rs32(rt)));
}
inline void MADDU(uint32_t rd, uint32_t rs, uint32_t rt) {
    mulResult(0, rd, hiLo(0) + static_cast<uint64_t>(ru32(rs)) * ru32(rt));
}
inline void MADD1(uint32_t rd, uint32_t rs, uint32_t rt) {
    mulResult(1, rd, hiLo(1) + static_cast<uint64_t>(static_cast<int64_t>(rs32(rs)) * rs32(rt)));
}
inline void MADDU1(uint32_t rd, uint32_t rs, uint32_t rt) {
    mulResult(1, rd, hiLo(1) + static_cast<uint64_t>(ru32(rs)) * ru32(rt));
}
inline void DIV(uint32_t rs, uint32_t rt) { signedDiv(0, rs32(rs), rs32(rt)); }
inline void DIVU(uint32_t rs, uint32_t rt) { unsignedDiv(0, ru32(rs), ru32(rt)); }
inline void DIV1(uint32_t rs, uint32_t rt) { signedDiv(1, rs32(rs), rs32(rt)); }
inline void DIVU1(uint32_t rs, uint32_t rt) { unsignedDiv(1, ru32(rs), ru32(rt)); }

inline void MFHI(uint32_t rd) { setU64(rd, cpu.hi.d[0]); }
inline void MFLO(uint32_t rd) { setU64(rd, cpu.lo.d[0]); }
inline void MTHI(uint32_t rs) { cpu.hi.d[0] = ru64(rs); }
inline void MTLO(uint32_t rs) { cpu.lo.d[0] = ru64(rs); }
inline void MFHI1(uint32_t rd) { setU64(rd, cpu.hi.d[1]); }
inline void MFLO1(uint32_t rd) { setU64(rd, cpu.lo.d[1]); }
inline void MTHI1(uint32_t rs) { cpu.hi.d[1] = ru64(rs); }
inline void MTLO1(uint32_t rs) { cpu.lo.d[1] = ru64(rs); }

// ---- shift-amount register (QFSRV) -----------------------------------------
inline void MFSA(uint32_t rd) { setU64(rd, cpu.sa); }
inline void MTSA(uint32_t rs) { cpu.sa = ru32(rs); }
inline void MTSAB(uint32_t rs, int32_t imm) { cpu.sa = (ru32(rs) ^ static_cast<uint32_t>(imm)) & 0xF; }
inline void MTSAH(uint32_t rs, int32_t imm) { cpu.sa = ((ru32(rs) ^ static_cast<uint32_t>(imm)) & 0x7) << 1; }

// ---- loads ------------------------------------------------------------------
inline uint32_t effAddr(uint32_t base, int32_t off) { return ru32(base) + static_cast<uint32_t>(off); }

inline void LB(uint32_t rt, uint32_t base, int32_t off) {
    const auto v = static_cast<int8_t>(read8(effAddr(base, off)));
    setU64(rt, static_cast<uint64_t>(static_cast<int64_t>(v)));
}
inline void LBU(uint32_t rt, uint32_t base, int32_t off) { setU64(rt, read8(effAddr(base, off))); }
inline void LH(uint32_t rt, uint32_t base, int32_t off) {
    const auto v = static_cast<int16_t>(read16(effAddr(base, off)));
    setU64(rt, static_cast<uint64_t>(static_cast<int64_t>(v)));
}
inline void LHU(uint32_t rt, uint32_t base, int32_t off) { setU64(rt, read16(effAddr(base, off))); }
inline void LW(uint32_t rt, uint32_t base, int32_t off) { setU32(rt, read32(effAddr(base, off))); }
inline void LWU(uint32_t rt, uint32_t base, int32_t off) { setU64(rt, read32(effAddr(base, off))); }
inline void LD(uint32_t rt, uint32_t base, int32_t off) { setU64(rt, read64(effAddr(base, off))); }
inline void LQ(uint32_t rt, uint32_t base, int32_t off) {
    uint64_t v[2];
    read128(effAddr(base, off), v);
    set128(rt, v[0], v[1]);
}

// Unaligned loads merge the bytes on the "left"/"right" side of the aligned
// word into rt (little-endian byte order).
inline void LWL(uint32_t rt, uint32_t base, int32_t off) {
    const uint32_t a = effAddr(base, off), shift = a & 3;
    const uint32_t mem = read32(a & ~3u);
    static constexpr uint32_t mask[4] = {0x00FFFFFFu, 0x0000FFFFu, 0x000000FFu, 0x00000000u};
    setU32(rt, (ru32(rt) & mask[shift]) | (mem << (24 - shift * 8)));
}
inline void LWR(uint32_t rt, uint32_t base, int32_t off) {
    const uint32_t a = effAddr(base, off), shift = a & 3;
    const uint32_t mem = read32(a & ~3u);
    static constexpr uint32_t mask[4] = {0x00000000u, 0xFF000000u, 0xFFFF0000u, 0xFFFFFF00u};
    const uint32_t v = (ru32(rt) & mask[shift]) | (mem >> (shift * 8));
    if (!rt) return;
    if (shift == 0) setU32(rt, v);   // whole word: sign-extended
    else cpu.gpr[rt].d[0] = (cpu.gpr[rt].d[0] & 0xFFFFFFFF00000000ull) | v;
}
inline void LDL(uint32_t rt, uint32_t base, int32_t off) {
    const uint32_t a = effAddr(base, off), shift = a & 7;
    const uint64_t mem = read64(a & ~7u);
    const uint64_t mask = shift == 7 ? 0 : (0x00FFFFFFFFFFFFFFull >> (shift * 8));
    setU64(rt, (ru64(rt) & mask) | (mem << (56 - shift * 8)));
}
inline void LDR(uint32_t rt, uint32_t base, int32_t off) {
    const uint32_t a = effAddr(base, off), shift = a & 7;
    const uint64_t mem = read64(a & ~7u);
    const uint64_t mask = shift == 0 ? 0 : (0xFFFFFFFFFFFFFF00ull << ((7 - shift) * 8));
    setU64(rt, (ru64(rt) & mask) | (mem >> (shift * 8)));
}

// ---- stores -----------------------------------------------------------------
inline void SB(uint32_t rt, uint32_t base, int32_t off) { write8(effAddr(base, off), static_cast<uint8_t>(ru32(rt))); }
inline void SH(uint32_t rt, uint32_t base, int32_t off) { write16(effAddr(base, off), static_cast<uint16_t>(ru32(rt))); }
inline void SW(uint32_t rt, uint32_t base, int32_t off) { write32(effAddr(base, off), ru32(rt)); }
inline void SD(uint32_t rt, uint32_t base, int32_t off) { write64(effAddr(base, off), ru64(rt)); }
inline void SQ(uint32_t rt, uint32_t base, int32_t off) { write128(effAddr(base, off), cpu.gpr[rt].d); }

inline void SWL(uint32_t rt, uint32_t base, int32_t off) {
    const uint32_t a = effAddr(base, off), shift = a & 3;
    const uint32_t mem = read32(a & ~3u);
    static constexpr uint32_t mask[4] = {0xFFFFFF00u, 0xFFFF0000u, 0xFF000000u, 0x00000000u};
    write32(a & ~3u, (ru32(rt) >> (24 - shift * 8)) | (mem & mask[shift]));
}
inline void SWR(uint32_t rt, uint32_t base, int32_t off) {
    const uint32_t a = effAddr(base, off), shift = a & 3;
    const uint32_t mem = read32(a & ~3u);
    static constexpr uint32_t mask[4] = {0x00000000u, 0x000000FFu, 0x0000FFFFu, 0x00FFFFFFu};
    write32(a & ~3u, (ru32(rt) << (shift * 8)) | (mem & mask[shift]));
}
inline void SDL(uint32_t rt, uint32_t base, int32_t off) {
    const uint32_t a = effAddr(base, off), shift = a & 7;
    const uint64_t mem = read64(a & ~7u);
    const uint64_t mask = shift == 7 ? 0 : (0xFFFFFFFFFFFFFF00ull << (shift * 8));
    write64(a & ~7u, (ru64(rt) >> (56 - shift * 8)) | (mem & mask));
}
inline void SDR(uint32_t rt, uint32_t base, int32_t off) {
    const uint32_t a = effAddr(base, off), shift = a & 7;
    const uint64_t mem = read64(a & ~7u);
    const uint64_t mask = shift == 0 ? 0 : (~0ull >> (64 - shift * 8));
    write64(a & ~7u, (ru64(rt) << (shift * 8)) | (mem & mask));
}

} // namespace ops
} // namespace ps2
