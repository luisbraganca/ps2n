// SPDX-License-Identifier: GPL-3.0-or-later
//
// COP2 (VU0 in macro mode): the VU0 instructions in EE code, as PS2Native
// carries them out. The transfers between VU0's registers and the EE's are in
// ops_cop2.h.
//
// What they agree with, by the rule the other families follow (ADR 0005):
// PCSX2's EE recompiler, whose code for these instructions is microVU's in
// macro mode: REC_COP2_mVU0, setupMacroOp and endMacroOp in
// pcsx2/x86/microVU_Macro.inl, the instructions in microVU_Upper.inl and
// microVU_Lower.inl, the clamps in microVU_Clamp.inl, the arithmetic in
// microVU_Misc.inl, the register allocator in microVU_IR.h (PCSX2 v2.9.170,
// Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+). The host's SSE instructions
// are those microVU emits, in the same order on the same operands, under the
// same control register (the EE's), so that a result is the recompiler's to
// the bit. The self-test holds every instruction here to the recompiler, in
// each of PCSX2's VU clamp modes.
//
// What that means where it is not the console:
//  - Arithmetic rounds and flushes denormals as the host does under the EE's
//    control register, not as VU0 does.
//  - Overflow and underflow are never flagged (PCSX2's VU overflow game fix
//    flags results of the largest magnitude and above as overflowing, as
//    there).
//  - Operands and results are clamped to the largest float, or not, as the VU
//    clamp mode says (None, Normal, Extra, Extra + Preserve Sign), and not
//    where microVU does not: an operand read from VF00, and a result
//    computed into the register such an operand was read into.
//  - VSUB and VSUBA of a register from itself give +0 whatever it holds.
//  - VMAX and VMINI order floats as signed magnitudes, so -0 is below +0 and a
//    NaN is above every number of its sign; of two equal values, the one from
//    ft.
//  - VDIV, VSQRT and VRSQRT: a zero divisor gives the largest float with the
//    sign of the quotient, and the square root is of the magnitude. The
//    invalid and division flags are as microVU sets them: VSQRT of a negative
//    number, -0 included, is invalid; VRSQRT by a zero of either sign is a
//    division by zero, or invalid when the dividend is zero too, and the
//    invalid flag of a negative divisor is not kept then.
//  - VCLIP reads a denormal component as +0.
//  - VOPMSUB with a single component in its mask computes that component
//    from x's product; VOPMULA with a single component gives +0 there. (Its
//    result in the recompiler depends on which registers its allocator holds
//    from the instructions before; this is what it gives when it holds none.)
//  - The integer registers are 16 bits wide. VLQD and VSQD with VI00 as the
//    address register reach VU1's registers, as with any address from 0x400
//    up.
//  - With PCSX2's I-bit game fix, VIADDI adds the immediate of the first word
//    of VU0's micro memory instead of its own.
//
// Where the flags of an instruction reach VU0's flag registers is not decided
// here: PCSX2's recompiler decides it for each instruction from its block
// (runtime/rec_blocks.h), and the caller passes the decision on. The status
// flag register is kept as the console has it throughout, where microVU may
// leave it in a layout of its own between two instructions of a block (the
// execution model, "What is not reproduced", says when that could show).
//
// The instructions are inline here, in ps2n::ee::vu0: compiled code calls them
// with the instruction's register numbers as constants (PS2N_VU0 in
// recomp.h), and gets each instruction's own few host instructions. Out of
// line, in runtime/ops_vu0.cpp: the flags, which a block lets reach the
// registers at only some of its instructions; the instructions that reach
// VU0's data memory or VU1's registers, which may have to wait for VU1's
// thread; and the functions of the runtime's table (ps2n::ee::ops), through
// which the interpreter calls the same code.
//
// VCALLMS and VCALLMSR are not here: PCSX2's recompiler calls PCSX2's own
// function for them, and so does PS2Native (ADR 0016).
#pragma once

#include "Config.h"
#include "VU.h"

#include <cstdint>
#include <cstring>
#include <smmintrin.h>

#if !defined(__clang__) && !defined(__GNUC__)
#error "ps2n/ops_vu0.h writes the host's SSE instructions as GNU inline assembly"
#endif

// Inlined wherever it is called: what is left of these once the register
// numbers are known is a few host instructions.
#define PS2N_VU0_INLINE [[gnu::always_inline]] inline

namespace ps2n::ee::vu0
{
	// Where the flags an instruction computes go.
	enum : uint32_t
	{
		kStatus = 1, // the status flag register takes them
		kMac = 2, // the MAC flag register takes them
	};

	namespace detail
	{
		// ---- the host's floating-point instructions --------------------------------
		// As microVU emits them: the first operand is the destination and the
		// first source, which decides which NaN comes out of two. They are written
		// as assembly so that the C++ compiler neither swaps the operands of an
		// addition or a multiplication nor computes anything itself: it would
		// round to nearest and keep denormals, where the host's control register
		// (the EE's, under which PCSX2's EE recompiler runs these too) says
		// otherwise.

		PS2N_VU0_INLINE __m128 ADDPS(__m128 a, __m128 b)
		{
			__asm__("addps %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128 ADDSS(__m128 a, __m128 b)
		{
			__asm__("addss %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128 SUBPS(__m128 a, __m128 b)
		{
			__asm__("subps %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128 MULPS(__m128 a, __m128 b)
		{
			__asm__("mulps %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128 DIVSS(__m128 a, __m128 b)
		{
			__asm__("divss %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128 MINPS(__m128 a, __m128 b)
		{
			__asm__("minps %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128 MAXPS(__m128 a, __m128 b)
		{
			__asm__("maxps %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128 MINSS(__m128 a, __m128 b)
		{
			__asm__("minss %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128 SQRTSS(__m128 a)
		{
			__asm__("sqrtss %0, %0" : "+x"(a));
			return a;
		}
		PS2N_VU0_INLINE __m128 CMPEQPS(__m128 a, __m128 b)
		{
			__asm__("cmpeqps %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128 CMPNLTPS(__m128 a, __m128 b)
		{
			__asm__("cmpnltps %1, %0" : "+x"(a) : "x"(b));
			return a;
		}
		PS2N_VU0_INLINE __m128i CVTTPS2DQ(__m128 a)
		{
			__m128i r;
			__asm__("cvttps2dq %1, %0" : "=x"(r) : "x"(a));
			return r;
		}
		PS2N_VU0_INLINE __m128 CVTDQ2PS(__m128i a)
		{
			__m128 r;
			__asm__("cvtdq2ps %1, %0" : "=x"(r) : "x"(a));
			return r;
		}

		// ---- registers ------------------------------------------------------------

		PS2N_VU0_INLINE __m128 Float(__m128i v) { return _mm_castsi128_ps(v); }
		PS2N_VU0_INLINE __m128i Int(__m128 v) { return _mm_castps_si128(v); }
		PS2N_VU0_INLINE __m128 All(uint32_t bits) { return Float(_mm_set1_epi32(static_cast<int>(bits))); }
		PS2N_VU0_INLINE __m128 Scalar(uint32_t bits) { return Float(_mm_cvtsi32_si128(static_cast<int>(bits))); }
		PS2N_VU0_INLINE uint32_t Bits(__m128 v) { return static_cast<uint32_t>(_mm_cvtsi128_si32(Int(v))); }

		PS2N_VU0_INLINE __m128 VF(uint32_t r) { return Float(_mm_load_si128(reinterpret_cast<const __m128i*>(VU0.VF[r].UL))); }
		PS2N_VU0_INLINE __m128 ACC() { return Float(_mm_load_si128(reinterpret_cast<const __m128i*>(VU0.ACC.UL))); }

		// The lanes a destination field (bit 3 for x to bit 0 for w) writes.
		alignas(16) inline constexpr uint32_t kDestLanes[16][4] = {
			{0, 0, 0, 0}, {0, 0, 0, ~0u}, {0, 0, ~0u, 0}, {0, 0, ~0u, ~0u},
			{0, ~0u, 0, 0}, {0, ~0u, 0, ~0u}, {0, ~0u, ~0u, 0}, {0, ~0u, ~0u, ~0u},
			{~0u, 0, 0, 0}, {~0u, 0, 0, ~0u}, {~0u, 0, ~0u, 0}, {~0u, 0, ~0u, ~0u},
			{~0u, ~0u, 0, 0}, {~0u, ~0u, 0, ~0u}, {~0u, ~0u, ~0u, 0}, {~0u, ~0u, ~0u, ~0u},
		};

		PS2N_VU0_INLINE __m128i Merge(__m128i old_value, __m128i value, uint32_t dest)
		{
			const __m128i lanes = _mm_load_si128(reinterpret_cast<const __m128i*>(kDestLanes[dest]));
			return _mm_blendv_epi8(old_value, value, lanes);
		}

		// A result written to a floating-point register: the components of
		// `dest`. VF00 is never written (writeBackReg and clearNeeded in
		// microVU_IR.h).
		PS2N_VU0_INLINE void WriteVF(uint32_t r, uint32_t dest, __m128 value)
		{
			if (r == 0)
				return;
			__m128i* reg = reinterpret_cast<__m128i*>(VU0.VF[r].UL);
			_mm_store_si128(reg, Merge(_mm_load_si128(reg), Int(value), dest));
		}
		PS2N_VU0_INLINE void WriteACC(uint32_t dest, __m128 value)
		{
			__m128i* reg = reinterpret_cast<__m128i*>(VU0.ACC.UL);
			_mm_store_si128(reg, Merge(_mm_load_si128(reg), Int(value), dest));
		}

		// A component broadcast to every lane (mVUunpack_xyzw, mVUloadIreg,
		// getQreg).
		PS2N_VU0_INLINE __m128 Broadcast(uint32_t bits) { return All(bits); }

		// ---- clamping (microVU_Clamp.inl) -----------------------------------------
		// The VU clamp mode, read as the recompiler reads it when it compiles:
		// CHECK_VU_OVERFLOW, clampE (CHECK_VU_EXTRA_OVERFLOW) and
		// CHECK_VU_SIGN_OVERFLOW for VU0. The four modes PCSX2 offers are None
		// (all off), Normal (overflow), Extra (and extra) and Extra + Preserve
		// Sign (and sign). Whether a register may be clamped at all is
		// checkVFClamp (microVU_IR.h): not when it was read from VF00, and that
		// stays so for the result computed into it.
		struct Clamp
		{
			bool overflow;
			bool extra;
			bool sign;
		};

		PS2N_VU0_INLINE Clamp Settings()
		{
			const auto& r = EmuConfig.Cpu.Recompiler;
			return {r.vu0Overflow, r.vu0ExtraOverflow, r.vu0SignOverflow};
		}

		// mVUclamp1: NaN and +Inf to the largest float, -Inf to the most
		// negative. MINPS then MAXPS, against those two.
		PS2N_VU0_INLINE __m128 Clamp1(__m128 v, const Clamp& c, bool clampable, bool clamp_e)
		{
			if (((!c.extra && c.overflow) || (c.extra && clamp_e)) && clampable)
			{
				v = MINPS(v, All(0x7F7FFFFFu));
				v = MAXPS(v, All(0xFF7FFFFFu));
			}
			return v;
		}

		// mVUclamp2: the clamp of an operand. With the sign preserved, on the
		// bits (PMINSD, PMINUD): a NaN keeps its sign.
		PS2N_VU0_INLINE __m128 Clamp2(__m128 v, const Clamp& c, bool clampable, bool clamp_e)
		{
			if (((!c.extra && c.sign) || (c.extra && clamp_e && c.sign)) && clampable)
			{
				__m128i i = Int(v);
				i = _mm_min_epi32(i, _mm_set1_epi32(0x7F7FFFFF));
				i = _mm_min_epu32(i, _mm_set1_epi32(static_cast<int>(0xFF7FFFFFu)));
				return Float(i);
			}
			return Clamp1(v, c, clampable, clamp_e);
		}

		// mVUclamp3 and mVUclamp4: around every addition, subtraction,
		// multiplication and division, in the Extra modes.
		PS2N_VU0_INLINE __m128 Clamp3(__m128 v, const Clamp& c, bool clampable)
		{
			return (c.extra && clampable) ? Clamp2(v, c, clampable, true) : v;
		}
		PS2N_VU0_INLINE __m128 Clamp4(__m128 v, const Clamp& c, bool clampable)
		{
			return (c.extra && !c.sign && clampable) ? Clamp1(v, c, clampable, true) : v;
		}

		enum class Arith
		{
			Add,
			Sub,
			Mul,
			Div, // of the first lanes only
		};

		// clampOp (microVU_Misc.inl): `to` op `from`, the result in `to`'s
		// register.
		// swapSources: the addition or multiplication with its two sources the
		// other way round, as PCSX2's emitter writes ADDPS and MULPS with AVX when
		// that shortens them; the result differs only where both are NaN (x86
		// gives the first source's). The register written, and so the result's
		// clamp, stay `to`'s.
		PS2N_VU0_INLINE __m128 Compute(Arith op, __m128 to, bool to_clampable, __m128 from, bool from_clampable, const Clamp& c,
			bool swapSources = false)
		{
			to = Clamp3(to, c, to_clampable);
			from = Clamp3(from, c, from_clampable);
			switch (op)
			{
				case Arith::Add: to = swapSources ? ADDPS(from, to) : ADDPS(to, from); break;
				case Arith::Sub: to = SUBPS(to, from); break;
				case Arith::Mul: to = swapSources ? MULPS(from, to) : MULPS(to, from); break;
				case Arith::Div: to = DIVSS(to, from); break;
			}
			return Clamp4(to, c, to_clampable);
		}

		// ---- flags (mVUupdateFlags in microVU_Upper.inl) --------------------------

		// The zero and sign flags of the components of `dest` in `result`, to the
		// registers `flags` says (runtime/ops_vu0.cpp).
		void Flags(__m128 result, uint32_t dest, uint32_t flags);

		PS2N_VU0_INLINE void UpdateFlags(__m128 result, uint32_t dest, uint32_t flags)
		{
			if (flags & (kStatus | kMac))
				Flags(result, dest, flags);
		}

		// The status flag register in microVU's layout and back (mVUallocSFLAGd and
		// mVUallocSFLAGc in microVU_Alloc.inl); a status flag goes through both on
		// its way.
		PS2N_VU0_INLINE uint32_t Denormalize(uint32_t s)
		{
			return ((s >> 3) & 0x18u) | ((s << 11) & 0x1800u) | ((s << 14) & 0x3CF0000u);
		}
		PS2N_VU0_INLINE uint32_t Normalize(uint32_t d)
		{
			uint32_t s = 0;
			if (d & 0x0F00u)
				s |= 0x01u; // Z
			if (d & 0xF000u)
				s |= 0x02u; // S
			if (d & 0x000Fu)
				s |= 0x40u; // ZS
			if (d & 0x00F0u)
				s |= 0x80u; // SS
			return s | ((d & 0xFFFF0000u) >> 14); // U O I D US OS IS DS
		}

		// ---- the FMAC instructions (microVU_Upper.inl) ----------------------------

		// The second operand: opCase1 to opCase4 of setupFtReg. Whether it may be
		// clamped: a register read from VF00 may not; a component broadcast, I and
		// Q are in registers of their own.
		enum class Second
		{
			Vector, // ft
			Bc, // a component of ft, broadcast
			I,
			Q,
		};

		struct Operand
		{
			__m128 v;
			bool clampable;
		};

		PS2N_VU0_INLINE Operand Fetch(Second k, uint32_t ft, uint32_t bc)
		{
			switch (k)
			{
				case Second::Vector: return {VF(ft), ft != 0};
				case Second::Bc: return {Broadcast(VU0.VF[ft].UL[bc]), true};
				case Second::I: return {Broadcast(VU0.VI[REG_I].UL), true};
				case Second::Q: break;
			}
			return {Broadcast(VU0.VI[REG_Q].UL), true};
		}

		// Which operands the instruction clamps first (clampModes).
		enum : uint32_t
		{
			cFt = 1,
			cFs = 2,
			cACC = 4,
		};

		PS2N_VU0_INLINE bool Single(uint32_t dest) { return dest == 8 || dest == 4 || dest == 2 || dest == 1; }

		// The clamps of the instructions that have them, by whether all four
		// components are written (_XYZW_PS).
		PS2N_VU0_INLINE uint32_t WhenAll(uint32_t dest, uint32_t all, uint32_t otherwise) { return dest == 0xF ? all : otherwise; }

		// MAX and MINI on the bits (MIN_MAX_PS, MIN_MAX_SS in microVU_Misc.inl,
		// which order the same way): negative floats have their magnitude bits
		// inverted, and the two are compared as signed integers. Of two equal
		// values, `from`.
		PS2N_VU0_INLINE __m128 MinMax(__m128 to, __m128 from, bool min)
		{
			const __m128i a = Int(to), b = Int(from);
			const __m128i ka = _mm_xor_si128(_mm_srli_epi32(_mm_srai_epi32(a, 31), 1), a);
			const __m128i kb = _mm_xor_si128(_mm_srli_epi32(_mm_srai_epi32(b, 31), 1), b);
			const __m128i pick = min ? _mm_cmpgt_epi32(kb, ka) : _mm_cmpgt_epi32(ka, kb);
			return Float(_mm_or_si128(_mm_and_si128(a, pick), _mm_andnot_si128(pick, b)));
		}

		// ADD_SS_TriAceHack: VADDi of one component with PCSX2's VU add/sub game
		// fix (runtime/ops_vu0.cpp).
		__m128 TriAceAdd(__m128 to, __m128 from, uint32_t dest);

		enum class Kind
		{
			Add,
			Sub,
			Mul,
			Max,
			Min,
			AddI, // ADDi, SSE_ADD2: an addition, or the game fix's
		};

		// mVU_FMACa: ADD, SUB, MUL, MAX, MINI, and the first three into ACC.
		PS2N_VU0_INLINE void FMACa(Kind kind, Second k, bool to_acc, uint32_t clamp, uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs,
			uint32_t ft, uint32_t bc)
		{
			// doSafeSub: a register minus itself is +0.
			if (kind == Kind::Sub && k == Second::Vector && fs == ft)
			{
				const __m128 zero = _mm_setzero_ps();
				if (to_acc)
					WriteACC(dest, zero);
				else
					WriteVF(fd, dest, zero);
				UpdateFlags(zero, dest, flags);
				return;
			}

			const Clamp c = Settings();
			Operand t = Fetch(k, ft, bc);
			__m128 s = VF(fs);
			const bool s_clampable = fs != 0;
			if (clamp & cFt)
				t.v = Clamp2(t.v, c, t.clampable, false);
			if (clamp & cFs)
				s = Clamp2(s, c, s_clampable, false);

			__m128 r;
			switch (kind)
			{
				case Kind::Add: r = Compute(Arith::Add, s, s_clampable, t.v, t.clampable, c); break;
				case Kind::Sub: r = Compute(Arith::Sub, s, s_clampable, t.v, t.clampable, c); break;
				case Kind::Mul: r = Compute(Arith::Mul, s, s_clampable, t.v, t.clampable, c); break;
				case Kind::Max: r = MinMax(s, t.v, false); break;
				case Kind::Min: r = MinMax(s, t.v, true); break;
				case Kind::AddI:
					if (Single(dest) && EmuConfig.Gamefixes.VuAddSubHack) [[unlikely]]
						r = TriAceAdd(s, t.v, dest);
					else
						r = Compute(Arith::Add, s, s_clampable, t.v, t.clampable, c);
					break;
			}

			if (to_acc)
				WriteACC(dest, r);
			else
				WriteVF(fd, dest, r);
			// MAX and MINI write no flags.
			if (kind != Kind::Max && kind != Kind::Min)
				UpdateFlags(r, dest, flags);
		}

		// mVU_FMACb: MADDA and MSUBA. ACC plus or minus the product.
		PS2N_VU0_INLINE void FMACb(bool subtract, Second k, uint32_t clamp, uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc)
		{
			const Clamp c = Settings();
			Operand t = Fetch(k, ft, bc);
			__m128 s = VF(fs);
			const bool s_clampable = fs != 0;
			if (clamp & cFt)
				t.v = Clamp2(t.v, c, t.clampable, false);
			if (clamp & cFs)
				s = Clamp2(s, c, s_clampable, false);

			const __m128 product = Compute(Arith::Mul, s, s_clampable, t.v, t.clampable, c);
			const __m128 r = Compute(subtract ? Arith::Sub : Arith::Add, ACC(), true, product, s_clampable, c);
			WriteACC(dest, r);
			UpdateFlags(r, dest, flags);
		}

		// mVU_FMACc: MADD. The product plus ACC.
		PS2N_VU0_INLINE void FMACc(Second k, uint32_t clamp, uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc)
		{
			const Clamp c = Settings();
			Operand t = Fetch(k, ft, bc);
			__m128 acc = ACC();
			__m128 s = VF(fs);
			const bool s_clampable = fs != 0;
			if (clamp & cFt)
				t.v = Clamp2(t.v, c, t.clampable, false);
			if (clamp & cFs)
				s = Clamp2(s, c, s_clampable, false);
			if (clamp & cACC)
				acc = Clamp2(acc, c, true, false);

			const __m128 product = Compute(Arith::Mul, s, s_clampable, t.v, t.clampable, c);
			const __m128 r = Compute(Arith::Add, product, s_clampable, acc, true, c);
			WriteVF(fd, dest, r);
			UpdateFlags(r, dest, flags);
		}

		// mVU_FMACd: MSUB. ACC minus the product.
		PS2N_VU0_INLINE void FMACd(Second k, uint32_t clamp, uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc)
		{
			const Clamp c = Settings();
			Operand t = Fetch(k, ft, bc);
			__m128 s = VF(fs);
			__m128 acc = ACC();
			const bool s_clampable = fs != 0;
			if (clamp & cFt)
				t.v = Clamp2(t.v, c, t.clampable, false);
			if (clamp & cFs)
				s = Clamp2(s, c, s_clampable, false);
			if (clamp & cACC)
				acc = Clamp2(acc, c, true, false);

			const __m128 product = Compute(Arith::Mul, s, s_clampable, t.v, t.clampable, c);
			const __m128 r = Compute(Arith::Sub, acc, true, product, s_clampable, c);
			WriteVF(fd, dest, r);
			UpdateFlags(r, dest, flags);
		}

		// ---- the conversions --------------------------------------------------------

		// mVU_FTOIx: CVTTPS2DQ gives 0x80000000 for what it cannot represent; a
		// positive value of that kind becomes 0x7FFFFFFF.
		PS2N_VU0_INLINE void FTOI(uint32_t dest, uint32_t ft, uint32_t fs, uint32_t scale)
		{
			if (ft == 0)
				return;
			__m128 v = VF(fs);
			if (scale)
				v = MULPS(v, All(scale));
			const __m128i positive_overflow = _mm_cmpgt_epi32(Int(v), _mm_set1_epi32(0x4EFFFFFF));
			WriteVF(ft, dest, Float(_mm_xor_si128(CVTTPS2DQ(v), positive_overflow)));
		}

		// mVU_ITOFx.
		PS2N_VU0_INLINE void ITOF(uint32_t dest, uint32_t ft, uint32_t fs, uint32_t scale)
		{
			if (ft == 0)
				return;
			__m128 v = CVTDQ2PS(Int(VF(fs)));
			if (scale)
				v = MULPS(v, All(scale));
			WriteVF(ft, dest, v);
		}

		// The scales of FTOI4/12/15 and ITOF4/12/15 (mVUglob).
		inline constexpr uint32_t kFloat16 = 0x41800000u; // 16.0
		inline constexpr uint32_t kFloat4096 = 0x45800000u; // 4096.0
		inline constexpr uint32_t kFloat32768 = 0x47000000u; // 32768.0
		inline constexpr uint32_t kFloat1_16 = 0x3D800000u; // 0.0625
		inline constexpr uint32_t kFloat1_4096 = 0x39800000u; // 0.000244140625
		inline constexpr uint32_t kFloat1_32768 = 0x38000000u; // 0.000030517578125

		// The flags of VDIV, VSQRT and VRSQRT in microVU's layout (divI, divD in
		// microVU_Misc.h): invalid and division by zero, each with its sticky one.
		inline constexpr uint32_t kDivI = 0x1040000u;
		inline constexpr uint32_t kDivD = 0x2080000u;

		PS2N_VU0_INLINE void DivFlags(uint32_t flags, uint32_t div)
		{
			if (flags & kStatus)
				VU0.VI[REG_STATUS_FLAG].UL = Normalize((Denormalize(VU0.VI[REG_STATUS_FLAG].UL) & ~0xC0000u) | div);
		}

		// testZero: whether the first lane compares equal to zero, under the
		// control register (a denormal does when denormals are zero).
		PS2N_VU0_INLINE bool IsZero(__m128 v) { return (_mm_movemask_ps(CMPEQPS(_mm_setzero_ps(), v)) & 1) != 0; }

		// The integer registers are 16 bits; VI00 reads as 0 whatever the
		// register holds, and a write to it goes nowhere. `r` is the
		// instruction's field, of which microVU uses the low four bits.
		PS2N_VU0_INLINE uint32_t VI(uint32_t r)
		{
			r &= 15;
			return r ? VU0.VI[r].US[0] : 0;
		}
		PS2N_VU0_INLINE void SetVI(uint32_t r, uint32_t value)
		{
			r &= 15;
			if (r)
				VU0.VI[r].US[0] = static_cast<uint16_t>(value);
		}
	} // namespace detail

	using namespace detail;

	// ---- FMAC: the vector forms ----
	PS2N_VU0_INLINE void VADD(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { FMACa(Kind::Add, Second::Vector, false, 0, flags, dest, fd, fs, ft, 0); }
	PS2N_VU0_INLINE void VSUB(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { FMACa(Kind::Sub, Second::Vector, false, WhenAll(dest, cFs | cFt, 0), flags, dest, fd, fs, ft, 0); }
	PS2N_VU0_INLINE void VMUL(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { FMACa(Kind::Mul, Second::Vector, false, WhenAll(dest, cFs | cFt, cFs), flags, dest, fd, fs, ft, 0); }
	PS2N_VU0_INLINE void VMADD(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { FMACc(Second::Vector, 0, flags, dest, fd, fs, ft, 0); }
	PS2N_VU0_INLINE void VMSUB(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { FMACd(Second::Vector, cFs, flags, dest, fd, fs, ft, 0); }
	PS2N_VU0_INLINE void VMAX(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { FMACa(Kind::Max, Second::Vector, false, 0, 0, dest, fd, fs, ft, 0); }
	PS2N_VU0_INLINE void VMINI(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) { FMACa(Kind::Min, Second::Vector, false, 0, 0, dest, fd, fs, ft, 0); }

	// ---- broadcast ----
	PS2N_VU0_INLINE void VADDbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) { FMACa(Kind::Add, Second::Bc, false, 0, flags, dest, fd, fs, ft, bc); }
	PS2N_VU0_INLINE void VSUBbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) { FMACa(Kind::Sub, Second::Bc, false, WhenAll(dest, cFs | cFt, 0), flags, dest, fd, fs, ft, bc); }
	PS2N_VU0_INLINE void VMULbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) { FMACa(Kind::Mul, Second::Bc, false, WhenAll(dest, cFs | cFt, cFs), flags, dest, fd, fs, ft, bc); }
	PS2N_VU0_INLINE void VMADDbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) { FMACc(Second::Bc, bc == 3 ? (cACC | cFt | cFs) : cFs, flags, dest, fd, fs, ft, bc); }
	PS2N_VU0_INLINE void VMSUBbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) { FMACd(Second::Bc, 0, flags, dest, fd, fs, ft, bc); }
	PS2N_VU0_INLINE void VMAXbc(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) { FMACa(Kind::Max, Second::Bc, false, 0, 0, dest, fd, fs, ft, bc); }
	PS2N_VU0_INLINE void VMINIbc(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) { FMACa(Kind::Min, Second::Bc, false, 0, 0, dest, fd, fs, ft, bc); }

	// ---- Q ----
	PS2N_VU0_INLINE void VADDq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACa(Kind::Add, Second::Q, false, 0, flags, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VSUBq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACa(Kind::Sub, Second::Q, false, WhenAll(dest, cFs | cFt, 0), flags, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VMULq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACa(Kind::Mul, Second::Q, false, WhenAll(dest, cFs | cFt, cFs), flags, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VMADDq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACc(Second::Q, 0, flags, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VMSUBq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACd(Second::Q, 0, flags, dest, fd, fs, 0, 0); }

	// ---- I ----
	PS2N_VU0_INLINE void VADDi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACa(Kind::AddI, Second::I, false, 0, flags, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VSUBi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACa(Kind::Sub, Second::I, false, WhenAll(dest, cFs | cFt, 0), flags, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VMULi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACa(Kind::Mul, Second::I, false, WhenAll(dest, cFs | cFt, cFs), flags, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VMADDi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACc(Second::I, 0, flags, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VMSUBi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs) { FMACd(Second::I, 0, flags, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VMAXi(uint32_t dest, uint32_t fd, uint32_t fs) { FMACa(Kind::Max, Second::I, false, 0, 0, dest, fd, fs, 0, 0); }
	PS2N_VU0_INLINE void VMINIi(uint32_t dest, uint32_t fd, uint32_t fs) { FMACa(Kind::Min, Second::I, false, 0, 0, dest, fd, fs, 0, 0); }

	// ---- into ACC ----
	PS2N_VU0_INLINE void VADDA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft) { FMACa(Kind::Add, Second::Vector, true, 0, flags, dest, 0, fs, ft, 0); }
	PS2N_VU0_INLINE void VSUBA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft) { FMACa(Kind::Sub, Second::Vector, true, 0, flags, dest, 0, fs, ft, 0); }
	PS2N_VU0_INLINE void VMULA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft) { FMACa(Kind::Mul, Second::Vector, true, 0, flags, dest, 0, fs, ft, 0); }
	PS2N_VU0_INLINE void VMADDA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft) { FMACb(false, Second::Vector, 0, flags, dest, fs, ft, 0); }
	PS2N_VU0_INLINE void VMSUBA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft) { FMACb(true, Second::Vector, 0, flags, dest, fs, ft, 0); }

	PS2N_VU0_INLINE void VADDAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc) { FMACa(Kind::Add, Second::Bc, true, 0, flags, dest, 0, fs, ft, bc); }
	PS2N_VU0_INLINE void VSUBAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc) { FMACa(Kind::Sub, Second::Bc, true, 0, flags, dest, 0, fs, ft, bc); }
	PS2N_VU0_INLINE void VMULAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc) { FMACa(Kind::Mul, Second::Bc, true, bc == 3 ? WhenAll(dest, cFs | cFt, cFs) : cFs, flags, dest, 0, fs, ft, bc); }
	PS2N_VU0_INLINE void VMADDAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc) { FMACb(false, Second::Bc, cFs, flags, dest, fs, ft, bc); }
	PS2N_VU0_INLINE void VMSUBAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc) { FMACb(true, Second::Bc, 0, flags, dest, fs, ft, bc); }

	PS2N_VU0_INLINE void VADDAq(uint32_t flags, uint32_t dest, uint32_t fs) { FMACa(Kind::Add, Second::Q, true, 0, flags, dest, 0, fs, 0, 0); }
	PS2N_VU0_INLINE void VSUBAq(uint32_t flags, uint32_t dest, uint32_t fs) { FMACa(Kind::Sub, Second::Q, true, 0, flags, dest, 0, fs, 0, 0); }
	PS2N_VU0_INLINE void VMULAq(uint32_t flags, uint32_t dest, uint32_t fs) { FMACa(Kind::Mul, Second::Q, true, 0, flags, dest, 0, fs, 0, 0); }
	PS2N_VU0_INLINE void VMADDAq(uint32_t flags, uint32_t dest, uint32_t fs) { FMACb(false, Second::Q, 0, flags, dest, fs, 0, 0); }
	PS2N_VU0_INLINE void VMSUBAq(uint32_t flags, uint32_t dest, uint32_t fs) { FMACb(true, Second::Q, 0, flags, dest, fs, 0, 0); }

	PS2N_VU0_INLINE void VADDAi(uint32_t flags, uint32_t dest, uint32_t fs) { FMACa(Kind::Add, Second::I, true, 0, flags, dest, 0, fs, 0, 0); }
	PS2N_VU0_INLINE void VSUBAi(uint32_t flags, uint32_t dest, uint32_t fs) { FMACa(Kind::Sub, Second::I, true, 0, flags, dest, 0, fs, 0, 0); }
	PS2N_VU0_INLINE void VMULAi(uint32_t flags, uint32_t dest, uint32_t fs) { FMACa(Kind::Mul, Second::I, true, 0, flags, dest, 0, fs, 0, 0); }
	PS2N_VU0_INLINE void VMADDAi(uint32_t flags, uint32_t dest, uint32_t fs) { FMACb(false, Second::I, 0, flags, dest, fs, 0, 0); }
	PS2N_VU0_INLINE void VMSUBAi(uint32_t flags, uint32_t dest, uint32_t fs) { FMACb(true, Second::I, 0, flags, dest, fs, 0, 0); }

	// ---- the outer product ----
	// mVU_OPMULA: ACC = (fs.y ft.z, fs.z ft.x, fs.x ft.y), lanes moved by
	// PSHUFD before the multiplication. With a single component, microVU loads
	// that component alone into the first lane of a cleared register when its
	// allocator holds neither register, and then multiplies the zeros of the
	// second and third lanes.
	PS2N_VU0_INLINE void VOPMULA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft)
	{
		const Clamp c = Settings();
		__m128 s, t;
		if (Single(dest))
		{
			s = _mm_setzero_ps();
			t = _mm_setzero_ps();
		}
		else
		{
			s = Float(_mm_shuffle_epi32(Int(VF(fs)), 0xC9)); // y z x w
			t = Float(_mm_shuffle_epi32(Int(VF(ft)), 0xD2)); // z x y w
		}
		const __m128 r = Compute(Arith::Mul, s, fs != 0, t, ft != 0, c);
		WriteACC(dest, r);
		UpdateFlags(r, dest, flags);
	}

	// mVU_OPMSUB: fd = ACC - (fs.y ft.z, fs.z ft.x, fs.x ft.y). Both registers
	// are read whole; with a single component, the first lane of the product
	// is what is subtracted.
	PS2N_VU0_INLINE void VOPMSUB(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft)
	{
		const Clamp c = Settings();
		const __m128 t = Float(_mm_shuffle_epi32(Int(VF(ft)), 0xD2)); // z x y w
		const __m128 s = Float(_mm_shuffle_epi32(Int(VF(fs)), 0xC9)); // y z x w
		__m128 product = Compute(Arith::Mul, s, fs != 0, t, ft != 0, c);
		if (Single(dest))
			product = Float(_mm_shuffle_epi32(Int(product), 0x00));
		const __m128 r = Compute(Arith::Sub, ACC(), true, product, fs != 0, c);
		WriteVF(fd, dest, r);
		UpdateFlags(r, dest, flags);
	}

	// ---- on the bits ----
	PS2N_VU0_INLINE void VABS(uint32_t dest, uint32_t ft, uint32_t fs)
	{
		if (ft == 0)
			return;
		WriteVF(ft, dest, _mm_and_ps(VF(fs), All(0x7FFFFFFFu)));
	}

	PS2N_VU0_INLINE void VMOVE(uint32_t dest, uint32_t ft, uint32_t fs) { WriteVF(ft, dest, VF(fs)); }

	// mVU_MR32: the components rotated, x from y and w from x.
	PS2N_VU0_INLINE void VMR32(uint32_t dest, uint32_t ft, uint32_t fs) { WriteVF(ft, dest, Float(_mm_shuffle_epi32(Int(VF(fs)), 0x39))); }

	PS2N_VU0_INLINE void VFTOI0(uint32_t dest, uint32_t ft, uint32_t fs) { FTOI(dest, ft, fs, 0); }
	PS2N_VU0_INLINE void VFTOI4(uint32_t dest, uint32_t ft, uint32_t fs) { FTOI(dest, ft, fs, kFloat16); }
	PS2N_VU0_INLINE void VFTOI12(uint32_t dest, uint32_t ft, uint32_t fs) { FTOI(dest, ft, fs, kFloat4096); }
	PS2N_VU0_INLINE void VFTOI15(uint32_t dest, uint32_t ft, uint32_t fs) { FTOI(dest, ft, fs, kFloat32768); }
	PS2N_VU0_INLINE void VITOF0(uint32_t dest, uint32_t ft, uint32_t fs) { ITOF(dest, ft, fs, 0); }
	PS2N_VU0_INLINE void VITOF4(uint32_t dest, uint32_t ft, uint32_t fs) { ITOF(dest, ft, fs, kFloat1_16); }
	PS2N_VU0_INLINE void VITOF12(uint32_t dest, uint32_t ft, uint32_t fs) { ITOF(dest, ft, fs, kFloat1_4096); }
	PS2N_VU0_INLINE void VITOF15(uint32_t dest, uint32_t ft, uint32_t fs) { ITOF(dest, ft, fs, kFloat1_32768); }

	// mVU_CLIP: x, y and z of fs against the magnitude of ft.w, on the bits;
	// a denormal component counts as +0. The clip flag register shifts up six
	// and keeps 24 bits. The destination field is not looked at.
	PS2N_VU0_INLINE void VCLIPw(uint32_t dest, uint32_t fs, uint32_t ft)
	{
		(void)dest;
		const __m128i s = Int(VF(fs));
		const __m128i w = _mm_and_si128(_mm_set1_epi32(static_cast<int>(VU0.VF[ft].UL[3])), _mm_set1_epi32(0x7FFFFFFF));
		const __m128i denormal = _mm_cmpeq_epi32(_mm_and_si128(s, _mm_set1_epi32(0x7F800000)), _mm_setzero_si128());
		const __m128i v = _mm_andnot_si128(denormal, s);
		const __m128i negated = _mm_xor_si128(v, _mm_set1_epi32(static_cast<int>(0x80000000u)));
		const uint32_t above = static_cast<uint32_t>(_mm_movemask_ps(Float(_mm_cmpgt_epi32(v, w)))); // +x > |w|, by lane
		const uint32_t below = static_cast<uint32_t>(_mm_movemask_ps(Float(_mm_cmpgt_epi32(negated, w)))); // -x > |w|
		uint32_t clip = 0;
		for (uint32_t lane = 0; lane < 3; lane++)
			clip |= (((above >> lane) & 1) << (2 * lane)) | (((below >> lane) & 1) << (2 * lane + 1));
		VU0.VI[REG_CLIP_FLAG].UL = ((VU0.VI[REG_CLIP_FLAG].UL << 6) & 0xFFFFFFu) | clip;
	}

	// ---- division (microVU_Lower.inl) ----
	// mVU_DIV: Q = fs.fsf / ft.ftf.
	PS2N_VU0_INLINE void VDIV(uint32_t flags, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf)
	{
		const Clamp c = Settings();
		const uint32_t sv = VU0.VF[fs].UL[fsf], tv = VU0.VF[ft].UL[ftf];
		const __m128 s = Scalar(sv), t = Scalar(tv);
		uint32_t div;
		__m128 q;
		if (IsZero(t))
		{
			div = IsZero(s) ? kDivI : kDivD;
			q = Scalar(((sv ^ tv) & 0x80000000u) | 0x7F7FFFFFu);
		}
		else
		{
			div = 0;
			q = Compute(Arith::Div, s, fs != 0, t, ft != 0, c);
			q = Clamp1(q, c, fs != 0, true);
		}
		VU0.VI[REG_Q].UL = Bits(q);
		DivFlags(flags, div);
	}

	// mVU_SQRT: Q = the square root of the magnitude of ft.ftf; a negative
	// operand, -0 included, is invalid.
	PS2N_VU0_INLINE void VSQRT(uint32_t flags, uint32_t ft, uint32_t ftf)
	{
		uint32_t tv = VU0.VF[ft].UL[ftf];
		uint32_t div = 0;
		if (tv & 0x80000000u)
		{
			div = kDivI;
			tv &= 0x7FFFFFFFu;
		}
		__m128 t = Scalar(tv);
		if (EmuConfig.Cpu.Recompiler.vu0Overflow)
			t = MINSS(t, Scalar(0x7F7FFFFFu));
		VU0.VI[REG_Q].UL = Bits(SQRTSS(t));
		DivFlags(flags, div);
	}

	// mVU_RSQRT: Q = fs.fsf / the square root of the magnitude of ft.ftf. A
	// zero root makes the division flag (the invalid one for 0 by 0) the only
	// one, a negative operand's invalid flag included.
	PS2N_VU0_INLINE void VRSQRT(uint32_t flags, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf)
	{
		const Clamp c = Settings();
		const uint32_t sv = VU0.VF[fs].UL[fsf];
		uint32_t tv = VU0.VF[ft].UL[ftf];
		uint32_t div = 0;
		if (tv & 0x80000000u)
		{
			div = kDivI;
			tv &= 0x7FFFFFFFu;
		}
		const __m128 root = SQRTSS(Scalar(tv));
		const __m128 s = Scalar(sv);
		__m128 q;
		if (IsZero(root))
		{
			div = IsZero(s) ? kDivI : kDivD;
			q = Scalar((sv & 0x80000000u) | 0x7F7FFFFFu);
		}
		else
		{
			q = Compute(Arith::Div, s, fs != 0, root, ft != 0, c);
			q = Clamp1(q, c, fs != 0, true);
		}
		VU0.VI[REG_Q].UL = Bits(q);
		DivFlags(flags, div);
	}

	// recVWAITQ and recVNOP emit nothing.
	PS2N_VU0_INLINE void VWAITQ() {}
	PS2N_VU0_INLINE void VNOP() {}

	// ---- the integer registers ----
	// An instruction whose destination is VI00 is left out entirely (isNOP in
	// microVU's analysis), the address registers of VLQI and VLQD excepted.
	PS2N_VU0_INLINE void VIADD(uint32_t id, uint32_t is, uint32_t it) { SetVI(id, VI(is) + VI(it)); }
	PS2N_VU0_INLINE void VISUB(uint32_t id, uint32_t is, uint32_t it) { SetVI(id, VI(is) - VI(it)); }
	PS2N_VU0_INLINE void VIAND(uint32_t id, uint32_t is, uint32_t it) { SetVI(id, VI(is) & VI(it)); }
	PS2N_VU0_INLINE void VIOR(uint32_t id, uint32_t is, uint32_t it) { SetVI(id, VI(is) | VI(it)); }

	// mVU_IADDI: with PCSX2's I-bit game fix the immediate comes from the
	// instruction word at the micro program counter, which in macro mode is 0:
	// the first word of VU0's micro memory.
	PS2N_VU0_INLINE void VIADDI(uint32_t it, uint32_t is, int32_t imm5)
	{
		if (EmuConfig.Gamefixes.IbitHack) [[unlikely]]
		{
			uint32_t word;
			std::memcpy(&word, VU0.Micro, sizeof(word));
			imm5 = static_cast<int32_t>(word << 21) >> 27;
		}
		SetVI(it, VI(is) + static_cast<uint32_t>(imm5));
	}

	// mVU_MTIR: the low 16 bits of fs.fsf.
	PS2N_VU0_INLINE void VMTIR(uint32_t it, uint32_t fs, uint32_t fsf) { SetVI(it, VU0.VF[fs].UL[fsf]); }

	// mVU_MFIR: is, sign-extended, in every component of dest.
	PS2N_VU0_INLINE void VMFIR(uint32_t dest, uint32_t ft, uint32_t is)
	{
		WriteVF(ft, dest, Broadcast(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(VI(is))))));
	}

	// The instructions that reach VU0's data memory, or from address 0x400 up
	// VU1's registers, waiting for VU1's thread first (runtime/ops_vu0.cpp).
	void VLQI(uint32_t dest, uint32_t ft, uint32_t is);
	void VLQD(uint32_t dest, uint32_t ft, uint32_t is);
	void VSQI(uint32_t dest, uint32_t fs, uint32_t it);
	void VSQD(uint32_t dest, uint32_t fs, uint32_t it);
	void VILWR(uint32_t dest, uint32_t it, uint32_t is);
	void VISWR(uint32_t dest, uint32_t it, uint32_t is);

	// ---- R ----
	// mVU_RINIT: R takes fs.fsf's 23 low bits under the exponent of 1.0.
	PS2N_VU0_INLINE void VRINIT(uint32_t fs, uint32_t fsf)
	{
		const uint32_t bits = (fs || fsf == 3) ? VU0.VF[fs].UL[fsf] : 0;
		VU0.VI[REG_R].UL = (bits & 0x7FFFFFu) | 0x3F800000u;
	}

	PS2N_VU0_INLINE void VRXOR(uint32_t fs, uint32_t fsf)
	{
		if (fs || fsf == 3)
			VU0.VI[REG_R].UL ^= VU0.VF[fs].UL[fsf] & 0x7FFFFFu;
	}

	PS2N_VU0_INLINE void VRGET(uint32_t dest, uint32_t ft) { WriteVF(ft, dest, Broadcast(VU0.VI[REG_R].UL)); }

	// mVU_RNEXT: R steps, then is copied as by VRGET.
	PS2N_VU0_INLINE void VRNEXT(uint32_t dest, uint32_t ft)
	{
		uint32_t r = VU0.VI[REG_R].UL;
		const uint32_t feedback = ((r >> 4) & 1) ^ ((r >> 22) & 1);
		r = (((r << 1) ^ feedback) & 0x7FFFFFu) | 0x3F800000u;
		VU0.VI[REG_R].UL = r;
		WriteVF(ft, dest, Broadcast(r));
	}
} // namespace ps2n::ee::vu0

// The functions of the semantic call table (ps2n/r5900_sem_cop2.inc), out of
// line in runtime/ops_vu0.cpp: the same code as above, for the runtime, which
// carries out an instruction it has decoded. Register numbers are as the
// instruction holds them: fd, fs and ft are VU0 floating-point registers, and
// for the integer instructions the integer registers id, is and it. `dest`:
// the component mask, bit 3 for x to bit 0 for w. `bc`: the broadcast
// component, 0 for x to 3 for w. `fsf`, `ftf`: a single component, likewise.
// `flags`: where the flags reach (vu0::kStatus, vu0::kMac).
namespace ps2n::ee::ops
{
	void VADD(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VSUB(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMUL(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMADD(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMSUB(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMAX(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMINI(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VOPMSUB(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);

	void VADDbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VSUBbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMULbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMADDbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMSUBbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMAXbc(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMINIbc(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);

	void VADDq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VSUBq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMULq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMADDq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMSUBq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);

	void VADDi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VSUBi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMULi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMADDi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMSUBi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMAXi(uint32_t dest, uint32_t fd, uint32_t fs);
	void VMINIi(uint32_t dest, uint32_t fd, uint32_t fs);

	void VADDA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VSUBA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VMULA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VMADDA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VMSUBA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VOPMULA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);

	void VADDAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);
	void VSUBAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMULAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMADDAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMSUBAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);

	void VADDAq(uint32_t flags, uint32_t dest, uint32_t fs);
	void VSUBAq(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMULAq(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMADDAq(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMSUBAq(uint32_t flags, uint32_t dest, uint32_t fs);

	void VADDAi(uint32_t flags, uint32_t dest, uint32_t fs);
	void VSUBAi(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMULAi(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMADDAi(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMSUBAi(uint32_t flags, uint32_t dest, uint32_t fs);

	void VABS(uint32_t dest, uint32_t ft, uint32_t fs);
	void VMOVE(uint32_t dest, uint32_t ft, uint32_t fs);
	void VMR32(uint32_t dest, uint32_t ft, uint32_t fs);
	void VFTOI0(uint32_t dest, uint32_t ft, uint32_t fs);
	void VFTOI4(uint32_t dest, uint32_t ft, uint32_t fs);
	void VFTOI12(uint32_t dest, uint32_t ft, uint32_t fs);
	void VFTOI15(uint32_t dest, uint32_t ft, uint32_t fs);
	void VITOF0(uint32_t dest, uint32_t ft, uint32_t fs);
	void VITOF4(uint32_t dest, uint32_t ft, uint32_t fs);
	void VITOF12(uint32_t dest, uint32_t ft, uint32_t fs);
	void VITOF15(uint32_t dest, uint32_t ft, uint32_t fs);
	void VCLIPw(uint32_t dest, uint32_t fs, uint32_t ft);

	void VDIV(uint32_t flags, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf);
	void VSQRT(uint32_t flags, uint32_t ft, uint32_t ftf);
	void VRSQRT(uint32_t flags, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf);
	void VWAITQ();
	void VNOP();

	void VIADD(uint32_t id, uint32_t is, uint32_t it);
	void VISUB(uint32_t id, uint32_t is, uint32_t it);
	void VIAND(uint32_t id, uint32_t is, uint32_t it);
	void VIOR(uint32_t id, uint32_t is, uint32_t it);
	void VIADDI(uint32_t it, uint32_t is, int32_t imm5);
	void VMTIR(uint32_t it, uint32_t fs, uint32_t fsf);
	void VMFIR(uint32_t dest, uint32_t ft, uint32_t is);
	void VLQI(uint32_t dest, uint32_t ft, uint32_t is);
	void VLQD(uint32_t dest, uint32_t ft, uint32_t is);
	void VSQI(uint32_t dest, uint32_t fs, uint32_t it);
	void VSQD(uint32_t dest, uint32_t fs, uint32_t it);
	void VILWR(uint32_t dest, uint32_t it, uint32_t is);
	void VISWR(uint32_t dest, uint32_t it, uint32_t is);
	void VRINIT(uint32_t fs, uint32_t fsf);
	void VRXOR(uint32_t fs, uint32_t fsf);
	void VRGET(uint32_t dest, uint32_t ft);
	void VRNEXT(uint32_t dest, uint32_t ft);
} // namespace ps2n::ee::ops
