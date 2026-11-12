// SPDX-License-Identifier: GPL-3.0-or-later
//
// R5900 integer, shift, multiply/divide, HI/LO and load/store semantics.
//
// One inline function per instruction, named after the mnemonic in upper case
// and taking register numbers. Statically compiled code passes constants, which
// the compiler folds down to plain arithmetic on the registers; the fallback
// interpreter passes decoded fields. Control-flow instructions are not here.
//
// What each function must agree with, in this order:
//  1. PCSX2's EE recompiler, which is what games are tested against;
//  2. where a console recording (ps2autotests) shows the recompiler wrong and
//     PCSX2's interpreter right, the console.
// tests/ and the host's -selftest hold these functions to that.
//
// Differences from PCSX2's interpreter, each following its recompiler:
//  - ADD, ADDI, SUB, DADD, DADDI and DSUB do not raise an exception on signed
//    overflow. They wrap, as the unsigned forms do.
//  - Loads and stores do not raise an address error for a misaligned address.
//  - The shift-amount register is four bits wide (MTSA).
//  - A load from an EE timer register does not force an event test.
// Differences from PCSX2's recompiler, following its interpreter:
//  - LWR at a word boundary sign-extends, and otherwise keeps bits 32-63 of
//    the register, as on the console (cpu/ee/lsu).
//  - DIV and DIV1 of two values the recompiler knows at compile time leave
//    the remainder in HI. The recompiler sign-extends the quotient over it
//    (recDIVconst), so that a negative quotient leaves -1 there.
#pragma once

#include "ps2n/ee_memory.h"
#include "ps2n/ee_state.h"

#include <cstdint>

// ---- on values ------------------------------------------------------------------
// The arithmetic of the instructions that do nothing but compute a general
// register from others: functions of the registers' values, bits 0-63, to
// the value of the destination. Generated code calls these on the local
// variables it keeps registers in (docs/execution-model.md, "Registers in
// local variables"); the forms that take register numbers, below, are made of
// them, so that each instruction is defined once.
namespace ps2n::ee::val
{
	// arithmetic
	inline uint64_t ADDU(uint64_t rs, uint64_t rt) { return sext32(static_cast<uint32_t>(rs) + static_cast<uint32_t>(rt)); }
	inline uint64_t SUBU(uint64_t rs, uint64_t rt) { return sext32(static_cast<uint32_t>(rs) - static_cast<uint32_t>(rt)); }
	inline uint64_t ADDIU(uint64_t rs, int32_t imm) { return sext32(static_cast<uint32_t>(rs) + static_cast<uint32_t>(imm)); }
	inline uint64_t DADDU(uint64_t rs, uint64_t rt) { return rs + rt; }
	inline uint64_t DSUBU(uint64_t rs, uint64_t rt) { return rs - rt; }
	inline uint64_t DADDIU(uint64_t rs, int32_t imm) { return rs + static_cast<uint64_t>(static_cast<int64_t>(imm)); }

	// The forms that trap on overflow on the console do not here, as in
	// PCSX2's recompiler: they wrap, as the unsigned forms do.
	inline uint64_t ADD(uint64_t rs, uint64_t rt) { return ADDU(rs, rt); }
	inline uint64_t SUB(uint64_t rs, uint64_t rt) { return SUBU(rs, rt); }
	inline uint64_t ADDI(uint64_t rs, int32_t imm) { return ADDIU(rs, imm); }
	inline uint64_t DADD(uint64_t rs, uint64_t rt) { return DADDU(rs, rt); }
	inline uint64_t DSUB(uint64_t rs, uint64_t rt) { return DSUBU(rs, rt); }
	inline uint64_t DADDI(uint64_t rs, int32_t imm) { return DADDIU(rs, imm); }

	// logical, compare
	inline uint64_t AND(uint64_t rs, uint64_t rt) { return rs & rt; }
	inline uint64_t OR(uint64_t rs, uint64_t rt) { return rs | rt; }
	inline uint64_t XOR(uint64_t rs, uint64_t rt) { return rs ^ rt; }
	inline uint64_t NOR(uint64_t rs, uint64_t rt) { return ~(rs | rt); }
	inline uint64_t ANDI(uint64_t rs, uint32_t imm) { return rs & imm; }
	inline uint64_t ORI(uint64_t rs, uint32_t imm) { return rs | imm; }
	inline uint64_t XORI(uint64_t rs, uint32_t imm) { return rs ^ imm; }
	inline uint64_t LUI(uint32_t imm) { return sext32(imm << 16); }
	inline uint64_t SLT(uint64_t rs, uint64_t rt) { return static_cast<int64_t>(rs) < static_cast<int64_t>(rt) ? 1 : 0; }
	inline uint64_t SLTU(uint64_t rs, uint64_t rt) { return rs < rt ? 1 : 0; }
	inline uint64_t SLTI(uint64_t rs, int32_t imm) { return static_cast<int64_t>(rs) < imm ? 1 : 0; }
	inline uint64_t SLTIU(uint64_t rs, int32_t imm) { return rs < static_cast<uint64_t>(static_cast<int64_t>(imm)) ? 1 : 0; }

	// The conditional moves: the destination keeps `rd` unless the condition holds.
	inline uint64_t MOVZ(uint64_t rd, uint64_t rs, uint64_t rt) { return rt == 0 ? rs : rd; }
	inline uint64_t MOVN(uint64_t rd, uint64_t rs, uint64_t rt) { return rt != 0 ? rs : rd; }

	// shifts
	inline uint64_t SLL(uint64_t rt, uint32_t sa) { return sext32(static_cast<uint32_t>(rt) << (sa & 31)); }
	inline uint64_t SRL(uint64_t rt, uint32_t sa) { return sext32(static_cast<uint32_t>(rt) >> (sa & 31)); }
	inline uint64_t SRA(uint64_t rt, uint32_t sa) { return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(rt) >> (sa & 31))); }
	inline uint64_t SLLV(uint64_t rt, uint64_t rs) { return SLL(rt, static_cast<uint32_t>(rs)); }
	inline uint64_t SRLV(uint64_t rt, uint64_t rs) { return SRL(rt, static_cast<uint32_t>(rs)); }
	inline uint64_t SRAV(uint64_t rt, uint64_t rs) { return SRA(rt, static_cast<uint32_t>(rs)); }
	inline uint64_t DSLL(uint64_t rt, uint32_t sa) { return rt << (sa & 31); }
	inline uint64_t DSRL(uint64_t rt, uint32_t sa) { return rt >> (sa & 31); }
	inline uint64_t DSRA(uint64_t rt, uint32_t sa) { return static_cast<uint64_t>(static_cast<int64_t>(rt) >> (sa & 31)); }
	inline uint64_t DSLL32(uint64_t rt, uint32_t sa) { return rt << ((sa & 31) + 32); }
	inline uint64_t DSRL32(uint64_t rt, uint32_t sa) { return rt >> ((sa & 31) + 32); }
	inline uint64_t DSRA32(uint64_t rt, uint32_t sa) { return static_cast<uint64_t>(static_cast<int64_t>(rt) >> ((sa & 31) + 32)); }
	inline uint64_t DSLLV(uint64_t rt, uint64_t rs) { return rt << (static_cast<uint32_t>(rs) & 63); }
	inline uint64_t DSRLV(uint64_t rt, uint64_t rs) { return rt >> (static_cast<uint32_t>(rs) & 63); }
	inline uint64_t DSRAV(uint64_t rt, uint64_t rs) { return static_cast<uint64_t>(static_cast<int64_t>(rt) >> (static_cast<uint32_t>(rs) & 63)); }
} // namespace ps2n::ee::val

namespace ps2n::ee::ops
{
	// ---- arithmetic -----------------------------------------------------------
	inline void ADDU(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::ADDU(ru64(rs), ru64(rt))); }
	inline void SUBU(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::SUBU(ru64(rs), ru64(rt))); }
	inline void ADDIU(uint32_t rt, uint32_t rs, int32_t imm) { setU64(rt, val::ADDIU(ru64(rs), imm)); }
	inline void DADDU(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::DADDU(ru64(rs), ru64(rt))); }
	inline void DSUBU(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::DSUBU(ru64(rs), ru64(rt))); }
	inline void DADDIU(uint32_t rt, uint32_t rs, int32_t imm) { setU64(rt, val::DADDIU(ru64(rs), imm)); }

	inline void ADD(uint32_t rd, uint32_t rs, uint32_t rt) { ADDU(rd, rs, rt); }
	inline void SUB(uint32_t rd, uint32_t rs, uint32_t rt) { SUBU(rd, rs, rt); }
	inline void ADDI(uint32_t rt, uint32_t rs, int32_t imm) { ADDIU(rt, rs, imm); }
	inline void DADD(uint32_t rd, uint32_t rs, uint32_t rt) { DADDU(rd, rs, rt); }
	inline void DSUB(uint32_t rd, uint32_t rs, uint32_t rt) { DSUBU(rd, rs, rt); }
	inline void DADDI(uint32_t rt, uint32_t rs, int32_t imm) { DADDIU(rt, rs, imm); }

	// ---- logical / compare ----------------------------------------------------
	inline void AND(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::AND(ru64(rs), ru64(rt))); }
	inline void OR(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::OR(ru64(rs), ru64(rt))); }
	inline void XOR(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::XOR(ru64(rs), ru64(rt))); }
	inline void NOR(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::NOR(ru64(rs), ru64(rt))); }
	inline void ANDI(uint32_t rt, uint32_t rs, uint32_t imm) { setU64(rt, val::ANDI(ru64(rs), imm)); }
	inline void ORI(uint32_t rt, uint32_t rs, uint32_t imm) { setU64(rt, val::ORI(ru64(rs), imm)); }
	inline void XORI(uint32_t rt, uint32_t rs, uint32_t imm) { setU64(rt, val::XORI(ru64(rs), imm)); }
	inline void LUI(uint32_t rt, uint32_t imm) { setU64(rt, val::LUI(imm)); }
	inline void SLT(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::SLT(ru64(rs), ru64(rt))); }
	inline void SLTU(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::SLTU(ru64(rs), ru64(rt))); }
	inline void SLTI(uint32_t rt, uint32_t rs, int32_t imm) { setU64(rt, val::SLTI(ru64(rs), imm)); }
	inline void SLTIU(uint32_t rt, uint32_t rs, int32_t imm) { setU64(rt, val::SLTIU(ru64(rs), imm)); }
	inline void MOVZ(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::MOVZ(ru64(rd), ru64(rs), ru64(rt))); }
	inline void MOVN(uint32_t rd, uint32_t rs, uint32_t rt) { setU64(rd, val::MOVN(ru64(rd), ru64(rs), ru64(rt))); }

	// ---- shifts ---------------------------------------------------------------
	inline void SLL(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, val::SLL(ru64(rt), sa)); }
	inline void SRL(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, val::SRL(ru64(rt), sa)); }
	inline void SRA(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, val::SRA(ru64(rt), sa)); }
	inline void SLLV(uint32_t rd, uint32_t rt, uint32_t rs) { setU64(rd, val::SLLV(ru64(rt), ru64(rs))); }
	inline void SRLV(uint32_t rd, uint32_t rt, uint32_t rs) { setU64(rd, val::SRLV(ru64(rt), ru64(rs))); }
	inline void SRAV(uint32_t rd, uint32_t rt, uint32_t rs) { setU64(rd, val::SRAV(ru64(rt), ru64(rs))); }
	inline void DSLL(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, val::DSLL(ru64(rt), sa)); }
	inline void DSRL(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, val::DSRL(ru64(rt), sa)); }
	inline void DSRA(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, val::DSRA(ru64(rt), sa)); }
	inline void DSLL32(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, val::DSLL32(ru64(rt), sa)); }
	inline void DSRL32(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, val::DSRL32(ru64(rt), sa)); }
	inline void DSRA32(uint32_t rd, uint32_t rt, uint32_t sa) { setU64(rd, val::DSRA32(ru64(rt), sa)); }
	inline void DSLLV(uint32_t rd, uint32_t rt, uint32_t rs) { setU64(rd, val::DSLLV(ru64(rt), ru64(rs))); }
	inline void DSRLV(uint32_t rd, uint32_t rt, uint32_t rs) { setU64(rd, val::DSRLV(ru64(rt), ru64(rs))); }
	inline void DSRAV(uint32_t rd, uint32_t rt, uint32_t rs) { setU64(rd, val::DSRAV(ru64(rt), ru64(rs))); }

	// ---- HI/LO, multiply, divide ----------------------------------------------
	// Pipeline 0 uses bits 0-63 of HI and LO, pipeline 1 (the "...1"
	// instructions) bits 64-127.
	inline void mulResult(int pipe, uint32_t rd, uint64_t product)
	{
		cpuRegs.LO.UD[pipe] = sext32(static_cast<uint32_t>(product));
		cpuRegs.HI.UD[pipe] = sext32(static_cast<uint32_t>(product >> 32));
		setU64(rd, cpuRegs.LO.UD[pipe]); // the R5900's three-operand form also writes rd
	}
	inline uint64_t hiLo(int pipe)
	{
		return (static_cast<uint64_t>(cpuRegs.HI.UL[pipe * 2]) << 32) | cpuRegs.LO.UL[pipe * 2];
	}
	inline void signedDiv(int pipe, int32_t a, int32_t b)
	{
		if (a == INT32_MIN && b == -1)
		{
			cpuRegs.LO.UD[pipe] = sext32(0x80000000u);
			cpuRegs.HI.UD[pipe] = 0;
		}
		else if (b != 0)
		{
			cpuRegs.LO.SD[pipe] = a / b;
			cpuRegs.HI.SD[pipe] = a % b;
		}
		else
		{
			// What the R5900 leaves in HI and LO after a division by zero.
			cpuRegs.LO.SD[pipe] = a < 0 ? 1 : -1;
			cpuRegs.HI.SD[pipe] = a;
		}
	}
	inline void unsignedDiv(int pipe, uint32_t a, uint32_t b)
	{
		if (b != 0)
		{
			cpuRegs.LO.UD[pipe] = sext32(a / b);
			cpuRegs.HI.UD[pipe] = sext32(a % b);
		}
		else
		{
			cpuRegs.LO.UD[pipe] = ~0ull;
			cpuRegs.HI.UD[pipe] = sext32(a);
		}
	}

	inline void MULT(uint32_t rd, uint32_t rs, uint32_t rt)
	{
		mulResult(0, rd, static_cast<uint64_t>(static_cast<int64_t>(rs32(rs)) * rs32(rt)));
	}
	inline void MULTU(uint32_t rd, uint32_t rs, uint32_t rt)
	{
		mulResult(0, rd, static_cast<uint64_t>(ru32(rs)) * ru32(rt));
	}
	inline void MULT1(uint32_t rd, uint32_t rs, uint32_t rt)
	{
		mulResult(1, rd, static_cast<uint64_t>(static_cast<int64_t>(rs32(rs)) * rs32(rt)));
	}
	inline void MULTU1(uint32_t rd, uint32_t rs, uint32_t rt)
	{
		mulResult(1, rd, static_cast<uint64_t>(ru32(rs)) * ru32(rt));
	}
	inline void MADD(uint32_t rd, uint32_t rs, uint32_t rt)
	{
		mulResult(0, rd, hiLo(0) + static_cast<uint64_t>(static_cast<int64_t>(rs32(rs)) * rs32(rt)));
	}
	inline void MADDU(uint32_t rd, uint32_t rs, uint32_t rt)
	{
		mulResult(0, rd, hiLo(0) + static_cast<uint64_t>(ru32(rs)) * ru32(rt));
	}
	inline void MADD1(uint32_t rd, uint32_t rs, uint32_t rt)
	{
		mulResult(1, rd, hiLo(1) + static_cast<uint64_t>(static_cast<int64_t>(rs32(rs)) * rs32(rt)));
	}
	inline void MADDU1(uint32_t rd, uint32_t rs, uint32_t rt)
	{
		mulResult(1, rd, hiLo(1) + static_cast<uint64_t>(ru32(rs)) * ru32(rt));
	}
	inline void DIV(uint32_t rs, uint32_t rt) { signedDiv(0, rs32(rs), rs32(rt)); }
	inline void DIVU(uint32_t rs, uint32_t rt) { unsignedDiv(0, ru32(rs), ru32(rt)); }
	inline void DIV1(uint32_t rs, uint32_t rt) { signedDiv(1, rs32(rs), rs32(rt)); }
	inline void DIVU1(uint32_t rs, uint32_t rt) { unsignedDiv(1, ru32(rs), ru32(rt)); }

	inline void MFHI(uint32_t rd) { setU64(rd, cpuRegs.HI.UD[0]); }
	inline void MFLO(uint32_t rd) { setU64(rd, cpuRegs.LO.UD[0]); }
	inline void MTHI(uint32_t rs) { cpuRegs.HI.UD[0] = ru64(rs); }
	inline void MTLO(uint32_t rs) { cpuRegs.LO.UD[0] = ru64(rs); }
	inline void MFHI1(uint32_t rd) { setU64(rd, cpuRegs.HI.UD[1]); }
	inline void MFLO1(uint32_t rd) { setU64(rd, cpuRegs.LO.UD[1]); }
	inline void MTHI1(uint32_t rs) { cpuRegs.HI.UD[1] = ru64(rs); }
	inline void MTLO1(uint32_t rs) { cpuRegs.LO.UD[1] = ru64(rs); }

	// ---- shift-amount register (for QFSRV) ------------------------------------
	// Four bits: a count of bytes.
	inline void MFSA(uint32_t rd) { setU64(rd, cpuRegs.sa); }
	inline void MTSA(uint32_t rs) { cpuRegs.sa = ru32(rs) & 0xF; }
	inline void MTSAB(uint32_t rs, int32_t imm) { cpuRegs.sa = (ru32(rs) ^ static_cast<uint32_t>(imm)) & 0xF; }
	inline void MTSAH(uint32_t rs, int32_t imm) { cpuRegs.sa = ((ru32(rs) ^ static_cast<uint32_t>(imm)) & 0x7) << 1; }

	// ---- loads ----------------------------------------------------------------
	inline uint32_t effAddr(uint32_t base, int32_t off) { return ru32(base) + static_cast<uint32_t>(off); }

	// The access happens also when the destination is $zero: a read of a
	// hardware register can have effects.
	inline void LB(uint32_t rt, uint32_t base, int32_t off)
	{
		const auto v = static_cast<int8_t>(read8(effAddr(base, off)));
		setU64(rt, static_cast<uint64_t>(static_cast<int64_t>(v)));
	}
	inline void LBU(uint32_t rt, uint32_t base, int32_t off) { setU64(rt, read8(effAddr(base, off))); }
	inline void LH(uint32_t rt, uint32_t base, int32_t off)
	{
		const auto v = static_cast<int16_t>(read16(effAddr(base, off)));
		setU64(rt, static_cast<uint64_t>(static_cast<int64_t>(v)));
	}
	inline void LHU(uint32_t rt, uint32_t base, int32_t off) { setU64(rt, read16(effAddr(base, off))); }
	inline void LW(uint32_t rt, uint32_t base, int32_t off) { setU32(rt, read32(effAddr(base, off))); }
	inline void LWU(uint32_t rt, uint32_t base, int32_t off) { setU64(rt, read32(effAddr(base, off))); }
	inline void LD(uint32_t rt, uint32_t base, int32_t off) { setU64(rt, read64(effAddr(base, off))); }
	inline void LQ(uint32_t rt, uint32_t base, int32_t off)
	{
		uint64_t v[2];
		read128(effAddr(base, off), v);
		set128(rt, v[0], v[1]);
	}

	// Unaligned loads merge the bytes on the "left" or "right" side of the
	// aligned word into rt (the EE is little-endian).
	inline void LWL(uint32_t rt, uint32_t base, int32_t off)
	{
		const uint32_t a = effAddr(base, off), shift = a & 3;
		const uint32_t mem = read32(a & ~3u);
		static constexpr uint32_t mask[4] = {0x00FFFFFFu, 0x0000FFFFu, 0x000000FFu, 0x00000000u};
		setU32(rt, (ru32(rt) & mask[shift]) | (mem << (24 - shift * 8)));
	}
	inline void LWR(uint32_t rt, uint32_t base, int32_t off)
	{
		const uint32_t a = effAddr(base, off), shift = a & 3;
		const uint32_t mem = read32(a & ~3u);
		static constexpr uint32_t mask[4] = {0x00000000u, 0xFF000000u, 0xFFFF0000u, 0xFFFFFF00u};
		const uint32_t v = (ru32(rt) & mask[shift]) | (mem >> (shift * 8));
		if (!rt)
			return;
		if (shift == 0)
			setU32(rt, v); // the whole word: sign-extended
		else
			cpuRegs.GPR.r[rt].UL[0] = v; // bits 32-63 are kept
	}
	inline void LDL(uint32_t rt, uint32_t base, int32_t off)
	{
		const uint32_t a = effAddr(base, off), shift = a & 7;
		const uint64_t mem = read64(a & ~7u);
		const uint64_t mask = shift == 7 ? 0 : (0x00FFFFFFFFFFFFFFull >> (shift * 8));
		setU64(rt, (ru64(rt) & mask) | (mem << (56 - shift * 8)));
	}
	inline void LDR(uint32_t rt, uint32_t base, int32_t off)
	{
		const uint32_t a = effAddr(base, off), shift = a & 7;
		const uint64_t mem = read64(a & ~7u);
		const uint64_t mask = shift == 0 ? 0 : (0xFFFFFFFFFFFFFF00ull << ((7 - shift) * 8));
		setU64(rt, (ru64(rt) & mask) | (mem >> (shift * 8)));
	}

	// ---- stores ---------------------------------------------------------------
	inline void SB(uint32_t rt, uint32_t base, int32_t off) { write8(effAddr(base, off), static_cast<uint8_t>(ru32(rt))); }
	inline void SH(uint32_t rt, uint32_t base, int32_t off) { write16(effAddr(base, off), static_cast<uint16_t>(ru32(rt))); }
	inline void SW(uint32_t rt, uint32_t base, int32_t off) { write32(effAddr(base, off), ru32(rt)); }
	inline void SD(uint32_t rt, uint32_t base, int32_t off) { write64(effAddr(base, off), ru64(rt)); }
	inline void SQ(uint32_t rt, uint32_t base, int32_t off) { write128(effAddr(base, off), cpuRegs.GPR.r[rt].UD); }

	inline void SWL(uint32_t rt, uint32_t base, int32_t off)
	{
		const uint32_t a = effAddr(base, off), shift = a & 3;
		const uint32_t mem = read32(a & ~3u);
		static constexpr uint32_t mask[4] = {0xFFFFFF00u, 0xFFFF0000u, 0xFF000000u, 0x00000000u};
		write32(a & ~3u, (ru32(rt) >> (24 - shift * 8)) | (mem & mask[shift]));
	}
	inline void SWR(uint32_t rt, uint32_t base, int32_t off)
	{
		const uint32_t a = effAddr(base, off), shift = a & 3;
		const uint32_t mem = read32(a & ~3u);
		static constexpr uint32_t mask[4] = {0x00000000u, 0x000000FFu, 0x0000FFFFu, 0x00FFFFFFu};
		write32(a & ~3u, (ru32(rt) << (shift * 8)) | (mem & mask[shift]));
	}
	inline void SDL(uint32_t rt, uint32_t base, int32_t off)
	{
		const uint32_t a = effAddr(base, off), shift = a & 7;
		const uint64_t mem = read64(a & ~7u);
		const uint64_t mask = shift == 7 ? 0 : (0xFFFFFFFFFFFFFF00ull << (shift * 8));
		write64(a & ~7u, (ru64(rt) >> (56 - shift * 8)) | (mem & mask));
	}
	inline void SDR(uint32_t rt, uint32_t base, int32_t off)
	{
		const uint32_t a = effAddr(base, off), shift = a & 7;
		const uint64_t mem = read64(a & ~7u);
		const uint64_t mask = shift == 0 ? 0 : (~0ull >> (64 - shift * 8));
		write64(a & ~7u, (ru64(rt) << (shift * 8)) | (mem & mask));
	}
} // namespace ps2n::ee::ops
