// SPDX-License-Identifier: GPL-3.0-or-later
//
// R5900 floating-point (COP1) semantics.
//
// The EE's FPU is not IEEE 754: it has no infinities, no NaNs and no
// denormals, and it rounds differently. PCSX2 approximates it on the host's
// SSE unit, and games are tested against that approximation, so this file
// reproduces it instruction for instruction: the same SSE operations, in the
// same order, under the same control register, as the code PCSX2's EE
// recompiler emits (pcsx2/x86/iFPU.cpp, PCSX2 v2.9.170, Copyright 2002-2026
// PCSX2 Dev Team, GPL-3.0+). The host's -selftest compares every function
// here with that recompiler on random and boundary operands.
//
// A register's value is held as that recompiler holds it: its 32 bits in the
// lowest lane of a host SSE register (val::Fpr). Compiled code keeps the FPU's
// registers so in its locals, and the instructions below take and give them
// so, which keeps a chain of them (a load, a multiplication, an addition, a
// store) in SSE registers throughout. The other three lanes are of no
// account: what they hold is never carried into the lowest lane of a result,
// and the self-test checks that with every function below.
//
// The clamp mode (PCSX2's "EE clamping": None, Normal, Extra + Preserve Sign)
// is read from the core's configuration, as the recompiler reads it when it
// compiles. The fourth mode, Full, is a different recompiler (iFPUd.cpp) and
// is not implemented: the provider does not use these functions then.
//
// Where this file does NOT follow the recompiler:
//
//  - ABS.S, NEG.S, MAX.S and MIN.S operate on the bits, as the console does
//    (ps2autotests cpu/ee_fpu/arithmetic) and as PCSX2's interpreter does. The
//    recompiler clamps them, and what MAX.S and MIN.S give it for +0 and -0
//    depends on which host register it happened to allocate.
//  - The recompiler sometimes clamps an operand in the host register that
//    holds the guest register, so that a later instruction of the same block
//    sees the clamped value. That is an accident of its register allocation;
//    here an instruction changes its destination and nothing else.
//  - In the clamp mode None, NaNs are not clamped away, and when two of them
//    meet in an operation the host returns the first operand. Which operand
//    the recompiler puts first again depends on its register allocation. Here
//    the order is fixed: fs before ft, and as noted at madd() below.
//
// As in the recompiler, the overflow and underflow flags of FCR31 are never
// set or cleared; only DIV.S, SQRT.S and RSQRT.S touch flags (I, D, SI, SD).
//
// This is x86-64 code, for SSE4.1. ADR 0001 makes ARM64 optional; a port
// needs these operations reproduced there, bit for bit.
#pragma once

#include "ps2n/ee_memory.h"
#include "ps2n/ee_state.h"
#include "ps2n/ops_int.h"

#include "Config.h"

#include <array>
#include <cstdint>
#include <emmintrin.h>
#include <smmintrin.h>
#include <xmmintrin.h>

// The instructions compiled code carries out most, inlined wherever they are
// called, in code that profile-guided optimization takes for cold as well:
// what is left of each is a few host instructions (game log, "G10").
#define PS2N_FPU_INLINE [[gnu::always_inline]] inline

namespace ps2n::ee::val
{
	// An FPU register's value: its 32 bits in the lowest lane of a host SSE
	// register. The other lanes are of no account (above).
	using Fpr = __m128;
} // namespace ps2n::ee::val

namespace ps2n::ee::ops
{
	namespace fpu
	{
		inline constexpr uint32_t kSign = 0x80000000u;
		inline constexpr uint32_t kMax = 0x7F7FFFFFu; // the largest float
		inline constexpr uint32_t kMin = 0xFF7FFFFFu; // the most negative

		// FCR31
		inline constexpr uint32_t kFlagC = 0x00800000u; // condition
		inline constexpr uint32_t kFlagI = 0x00020000u; // invalid
		inline constexpr uint32_t kFlagD = 0x00010000u; // division by zero
		inline constexpr uint32_t kFlagSI = 0x00000040u; // sticky invalid
		inline constexpr uint32_t kFlagSD = 0x00000020u; // sticky division by zero

		// How often ABS.S or NEG.S ([0]), MAX.S or MIN.S ([1]) was given operands
		// for which stock PCSX2's recompiler may leave something else in the
		// destination than the console does, and this file (the first item
		// above): bits it clamps, or for the last two a zero of each sign. For
		// the report of a run: when a run is compared with a stock run and an
		// FPU register differs, this is the first place to look.
		extern uint64_t console_followed[2];
		inline bool clampedByPcsx2(uint32_t bits) { return (bits & 0x7F800000u) == 0x7F800000u; }

		// PCSX2's clamp settings: CHECK_FPU_OVERFLOW and CHECK_FPU_EXTRA_OVERFLOW.
		inline bool clampResults() { return EmuConfig.Cpu.Recompiler.fpuOverflow; }
		inline bool clampOperands() { return EmuConfig.Cpu.Recompiler.fpuExtraOverflow; }

		// A register's value from its bits and back: MOVD. (Written as an
		// intrinsic, the second becomes EXTRACTPS, twice the micro-operations.)
		PS2N_FPU_INLINE val::Fpr fromBits(uint32_t bits) { return _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<int>(bits))); }
		PS2N_FPU_INLINE uint32_t toBits(val::Fpr v)
		{
#if defined(__clang__) || defined(__GNUC__)
			uint32_t bits;
			__asm__("movd %1, %0" : "=r"(bits) : "x"(v));
			return bits;
#else
			return static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(v)));
#endif
		}
		// A register's value stored as its 32 bits, from its SSE register.
		PS2N_FPU_INLINE void store(uint32_t* to, val::Fpr v) { _mm_store_ss(reinterpret_cast<float*>(to), v); }

		// Hides a value from the C++ compiler, and fixes the point at which it
		// is computed.
		//
		// The host's FP control register (rounding toward zero, denormals as
		// zero) is something the compiler knows nothing about. Given a
		// constant operand it would fold an operation at compile time, rounding
		// to nearest; and it is free to move an operation across a change of
		// the control register. Every operand of an FP operation below goes
		// through here, and so does a result computed under a temporary
		// control register, before it is restored.
		PS2N_FPU_INLINE __m128 opaque(__m128 v)
		{
#if defined(__clang__) || defined(__GNUC__)
			__asm__ volatile("" : "+x"(v));
			return v;
#else
			volatile uint32_t hidden = toBits(v);
			return fromBits(hidden);
#endif
		}

		// The host's FP control register set to `wanted` for the scope, if it
		// is not that already (LDMXCSR in the recompiler's code).
		struct ScopedControl
		{
			bool changed;
			explicit ScopedControl(uint32_t wanted)
				: changed(wanted != EmuConfig.Cpu.FPUFPCR.bitmask)
			{
				if (changed)
					_mm_setcsr(wanted);
			}
			~ScopedControl()
			{
				if (changed)
					_mm_setcsr(EmuConfig.Cpu.FPUFPCR.bitmask);
			}
			ScopedControl(const ScopedControl&) = delete;
			ScopedControl& operator=(const ScopedControl&) = delete;
		};

		// fpuFloat3: +NaN and +Inf become the largest float, -NaN and -Inf the
		// most negative. Integer minimums on the bits: PMINSD, PMINUD.
		PS2N_FPU_INLINE val::Fpr clampBySign(val::Fpr v)
		{
			__m128i bits = _mm_castps_si128(v);
			bits = _mm_min_epi32(bits, _mm_cvtsi32_si128(static_cast<int>(kMax)));
			bits = _mm_min_epu32(bits, _mm_cvtsi32_si128(static_cast<int>(kMin)));
			return _mm_castsi128_ps(bits);
		}

		// fpuFloat2: the clamp of an operand.
		PS2N_FPU_INLINE val::Fpr clampOperand(val::Fpr v) { return clampResults() ? clampBySign(v) : v; }

		// fpuFloat (ClampValues): the clamp of a result. Any NaN and +Inf
		// become the largest float, -Inf the most negative. MINSS then MAXSS,
		// in that order: a NaN comes out of MINSS as its second operand.
		PS2N_FPU_INLINE val::Fpr clampResult(val::Fpr v)
		{
			if (clampResults()) [[likely]]
			{
				v = _mm_min_ss(v, fromBits(kMax));
				v = _mm_max_ss(v, fromBits(kMin));
			}
			return v;
		}

		// Whether the lowest lane compares equal to zero (CMPEQSS against 0.0).
		// Under the EE's control register a denormal does.
		PS2N_FPU_INLINE bool isZero(val::Fpr v)
		{
			return (_mm_movemask_ps(_mm_cmpeq_ss(_mm_setzero_ps(), opaque(v))) & 1) != 0;
		}

		// FPU_ADD_SUB: the EE's adder keeps one guard bit where IEEE keeps
		// more. Of the operand with the smaller exponent, the bits that would
		// be shifted out past it are cleared before the host adds, and all but
		// its sign when the exponents are 25 or more apart.
		//
		// The masks by the difference of the exponents (d's less t's, -255 to
		// 255): the low word what d keeps, the high word what t keeps.
		// PCSX2's recompiler branches to the same masks. Looked up, they cost
		// no branch for the processor to mispredict, and come into SSE
		// registers without passing through general ones.
		alignas(64) inline constexpr std::array<uint64_t, 511> kGuardMasks = [] {
			std::array<uint64_t, 511> masks{};
			for (int diff = -255; diff <= 255; diff++)
			{
				uint32_t d = 0xFFFFFFFFu, t = 0xFFFFFFFFu;
				if (diff >= 25)
					t = kSign;
				else if (diff > 0)
					t = 0xFFFFFFFFu << (diff - 1);
				else if (diff <= -25)
					d = kSign;
				else if (diff < 0)
					d = 0xFFFFFFFFu << (-diff - 1);
				masks[static_cast<size_t>(diff + 255)] = d | (static_cast<uint64_t>(t) << 32);
			}
			return masks;
		}();

		PS2N_FPU_INLINE val::Fpr addSub(val::Fpr dv, val::Fpr tv, bool subtract)
		{
			const uint32_t d = toBits(dv), t = toBits(tv);
			const int32_t diff = static_cast<int32_t>((d >> 23) & 0xFF) - static_cast<int32_t>((t >> 23) & 0xFF);
			const __m128i masks = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&kGuardMasks[static_cast<size_t>(diff + 255)]));
			const __m128 a = opaque(_mm_and_ps(dv, _mm_castsi128_ps(masks)));
			const __m128 b = opaque(_mm_and_ps(tv, _mm_castsi128_ps(_mm_shuffle_epi32(masks, 1))));
			return subtract ? _mm_sub_ss(a, b) : _mm_add_ss(a, b);
		}

		// FPU_MUL, with the game fix PCSX2 applies in it when it is switched on.
		PS2N_FPU_INLINE val::Fpr multiply(val::Fpr s, val::Fpr t)
		{
			if (EmuConfig.Gamefixes.FpuMulHack) [[unlikely]]
			{
				if (toBits(s) == 0x3E800000u && toBits(t) == 0x40490FDBu)
					return fromBits(0x3F490FDAu);
			}
			return _mm_mul_ss(opaque(s), opaque(t));
		}

		PS2N_FPU_INLINE val::Fpr add(val::Fpr s, val::Fpr t)
		{
			if (clampOperands()) [[unlikely]]
			{
				s = clampOperand(s);
				t = clampOperand(t);
			}
			return clampResult(addSub(s, t, false));
		}

		PS2N_FPU_INLINE val::Fpr sub(val::Fpr s, val::Fpr t)
		{
			if (clampOperands()) [[unlikely]]
			{
				s = clampOperand(s);
				t = clampOperand(t);
			}
			return clampResult(addSub(s, t, true));
		}

		PS2N_FPU_INLINE val::Fpr mul(val::Fpr s, val::Fpr t)
		{
			if (clampOperands()) [[unlikely]]
			{
				s = clampOperand(s);
				t = clampOperand(t);
			}
			return clampResult(multiply(s, t));
		}

		// The product MADD and MSUB add to or subtract from the accumulator
		// (`acc`). A plain MULSS, without the game fix; in the Extra mode the
		// product and the accumulator are clamped as results are.
		struct ProductAndAcc
		{
			val::Fpr product, acc;
		};
		PS2N_FPU_INLINE ProductAndAcc productAndAcc(val::Fpr acc, val::Fpr s, val::Fpr t)
		{
			if (clampOperands()) [[unlikely]]
			{
				s = clampOperand(s);
				t = clampOperand(t);
			}
			__m128 product = _mm_mul_ss(opaque(s), opaque(t));
			if (clampOperands()) [[unlikely]]
			{
				acc = clampResult(acc);
				product = clampResult(product);
			}
			return {product, acc};
		}

		// The two orders are the recompiler's (FPU_ADD(product, ACC) for
		// MADD.S, FPU_ADD(ACC, product) for MADDA.S). They only differ in the
		// clamp mode None, when both are NaNs: the first one comes out.
		PS2N_FPU_INLINE val::Fpr madd(val::Fpr acc, val::Fpr s, val::Fpr t)
		{
			const ProductAndAcc p = productAndAcc(acc, s, t);
			return clampResult(addSub(p.product, p.acc, false));
		}

		PS2N_FPU_INLINE val::Fpr madda(val::Fpr acc, val::Fpr s, val::Fpr t)
		{
			const ProductAndAcc p = productAndAcc(acc, s, t);
			return clampResult(addSub(p.acc, p.product, false));
		}

		PS2N_FPU_INLINE val::Fpr msub(val::Fpr acc, val::Fpr s, val::Fpr t)
		{
			const ProductAndAcc p = productAndAcc(acc, s, t);
			return clampResult(addSub(p.acc, p.product, true));
		}

		// UCOMISS on the operands clamped by sign.
		inline void setCondition(bool value)
		{
			if (value)
				fpuRegs.fprc[31] |= kFlagC;
			else
				fpuRegs.fprc[31] &= ~kFlagC;
		}
		PS2N_FPU_INLINE __m128 comparand(val::Fpr v) { return opaque(clampBySign(v)); }
	} // namespace fpu

	namespace fpu
	{
		// MAX.S and MIN.S: operands PCSX2 clamps, or +0 and -0.
		inline void noteMaxMin(int32_t s, int32_t t)
		{
			const uint32_t us = static_cast<uint32_t>(s), ut = static_cast<uint32_t>(t);
			if ((((us | ut) & ~kSign) == 0 && us != ut) || clampedByPcsx2(us) || clampedByPcsx2(ut)) [[unlikely]]
				console_followed[1]++;
		}
	} // namespace fpu
} // namespace ps2n::ee::ops

// ---- the instructions, on values ----------------------------------------------------
// A register's value in (val::Fpr), the result's out. Compiled code computes
// with these while it holds the FPU's registers in local variables (recomp.h),
// and the functions further down, which work on the core's registers, are made
// of them: each instruction is defined once. Those that have flags write them
// to FCR31 in the core.
namespace ps2n::ee::val
{
	// ---- moves ----------------------------------------------------------------
	PS2N_FPU_INLINE uint64_t MFC1(Fpr fs) { return sext32(ops::fpu::toBits(fs)); }
	PS2N_FPU_INLINE Fpr MTC1(uint64_t rt) { return ops::fpu::fromBits(static_cast<uint32_t>(rt)); }

	// ---- on the bits, as the console ---------------------------------------------
	PS2N_FPU_INLINE Fpr ABS_S(Fpr s)
	{
		if (ops::fpu::clampedByPcsx2(ops::fpu::toBits(s))) [[unlikely]]
			ops::fpu::console_followed[0]++;
		return _mm_and_ps(s, ops::fpu::fromBits(~ops::fpu::kSign));
	}
	PS2N_FPU_INLINE Fpr NEG_S(Fpr s)
	{
		if (ops::fpu::clampedByPcsx2(ops::fpu::toBits(s))) [[unlikely]]
			ops::fpu::console_followed[0]++;
		return _mm_xor_ps(s, ops::fpu::fromBits(ops::fpu::kSign));
	}

	// The EE's floats order like sign-and-magnitude integers.
	PS2N_FPU_INLINE Fpr MAX_S(Fpr fs, Fpr ft)
	{
		const int32_t s = static_cast<int32_t>(ops::fpu::toBits(fs)), t = static_cast<int32_t>(ops::fpu::toBits(ft));
		ops::fpu::noteMaxMin(s, t);
		return ops::fpu::fromBits(static_cast<uint32_t>((s < 0 && t < 0) ? (s < t ? s : t) : (s > t ? s : t)));
	}
	PS2N_FPU_INLINE Fpr MIN_S(Fpr fs, Fpr ft)
	{
		const int32_t s = static_cast<int32_t>(ops::fpu::toBits(fs)), t = static_cast<int32_t>(ops::fpu::toBits(ft));
		ops::fpu::noteMaxMin(s, t);
		return ops::fpu::fromBits(static_cast<uint32_t>((s < 0 && t < 0) ? (s > t ? s : t) : (s < t ? s : t)));
	}

	// ---- arithmetic -------------------------------------------------------------
	// ADDA.S, SUBA.S and MULA.S are these with the accumulator as destination.
	PS2N_FPU_INLINE Fpr ADD_S(Fpr s, Fpr t) { return ops::fpu::add(s, t); }
	PS2N_FPU_INLINE Fpr SUB_S(Fpr s, Fpr t) { return ops::fpu::sub(s, t); }
	PS2N_FPU_INLINE Fpr MUL_S(Fpr s, Fpr t) { return ops::fpu::mul(s, t); }

	// With the accumulator first. MADDA.S gives the accumulator's new value;
	// MSUBA.S is MSUB.S with the accumulator as destination.
	PS2N_FPU_INLINE Fpr MADD_S(Fpr acc, Fpr s, Fpr t) { return ops::fpu::madd(acc, s, t); }
	PS2N_FPU_INLINE Fpr MSUB_S(Fpr acc, Fpr s, Fpr t) { return ops::fpu::msub(acc, s, t); }
	PS2N_FPU_INLINE Fpr MADDA_S(Fpr acc, Fpr s, Fpr t) { return ops::fpu::madda(acc, s, t); }

	// Division rounds as the core's "FPU division" control register says
	// (to nearest by default, where everything else rounds toward zero). A
	// zero divisor gives the largest float with the sign of the quotient, and
	// the invalid flag for 0/0, the division-by-zero flag otherwise.
	inline Fpr DIV_S(Fpr s, Fpr t)
	{
		namespace fpu = ops::fpu;
		const fpu::ScopedControl control(EmuConfig.Cpu.FPUDivFPCR.bitmask);

		fpuRegs.fprc[31] &= ~(fpu::kFlagI | fpu::kFlagD);
		if (fpu::isZero(t))
		{
			fpuRegs.fprc[31] |= fpu::isZero(s) ? (fpu::kFlagI | fpu::kFlagSI) : (fpu::kFlagD | fpu::kFlagSD);
			return fpu::fromBits(((fpu::toBits(s) ^ fpu::toBits(t)) & fpu::kSign) | fpu::kMax);
		}

		if (fpu::clampOperands())
		{
			s = fpu::clampOperand(s);
			t = fpu::clampOperand(t);
		}
		const __m128 quotient = _mm_div_ss(fpu::opaque(s), fpu::opaque(t));
		return fpu::opaque(fpu::clampResult(quotient));
	}

	// The square root of the magnitude, rounded to nearest; a negative operand
	// (including -0) sets the invalid flag.
	inline Fpr SQRT_S(Fpr t)
	{
		namespace fpu = ops::fpu;
		FPControlRegister nearest = EmuConfig.Cpu.FPUFPCR;
		nearest.SetRoundMode(FPRoundMode::Nearest);
		const fpu::ScopedControl control(nearest.bitmask);

		fpuRegs.fprc[31] &= ~(fpu::kFlagI | fpu::kFlagD);
		uint32_t bits = fpu::toBits(t);
		if (bits & fpu::kSign)
		{
			fpuRegs.fprc[31] |= fpu::kFlagI | fpu::kFlagSI;
			bits &= ~fpu::kSign;
		}

		__m128 v = fpu::opaque(fpu::fromBits(bits));
		if (fpu::clampResults())
			v = _mm_min_ss(v, fpu::fromBits(fpu::kMax));
		v = _mm_sqrt_ss(fpu::opaque(v));
		if (fpu::clampOperands())
			v = fpu::clampResult(v);
		return fpu::opaque(v);
	}

	// d / sqrt(|t|), under the standing control register.
	inline Fpr RSQRT_S(Fpr d, Fpr t)
	{
		namespace fpu = ops::fpu;

		fpuRegs.fprc[31] &= ~(fpu::kFlagI | fpu::kFlagD);
		uint32_t bits = fpu::toBits(t);
		if (bits & fpu::kSign)
		{
			fpuRegs.fprc[31] |= fpu::kFlagI | fpu::kFlagSI;
			bits &= ~fpu::kSign;
		}

		__m128 root = fpu::fromBits(bits);
		if (fpu::isZero(root))
		{
			fpuRegs.fprc[31] |= fpu::isZero(d) ? (fpu::kFlagI | fpu::kFlagSI) : (fpu::kFlagD | fpu::kFlagSD);
			return fpu::fromBits((fpu::toBits(d) & fpu::kSign) | fpu::kMax);
		}

		root = fpu::opaque(root);
		if (fpu::clampOperands())
		{
			root = _mm_min_ss(root, fpu::fromBits(fpu::kMax));
			d = fpu::clampOperand(d);
		}
		root = _mm_sqrt_ss(fpu::opaque(root));
		const __m128 quotient = _mm_div_ss(fpu::opaque(d), fpu::opaque(root));
		return fpu::clampResult(quotient);
	}

	// ---- conversions --------------------------------------------------------------
	// From a 32-bit integer, rounded as the control register says (CVTDQ2PS).
	PS2N_FPU_INLINE Fpr CVT_S_W(Fpr s) { return _mm_cvtepi32_ps(_mm_castps_si128(ops::fpu::opaque(s))); }

	// To a 32-bit integer, truncating; what is too large saturates.
	PS2N_FPU_INLINE Fpr CVT_W_S(Fpr s)
	{
		int32_t result = _mm_cvttss_si32(ops::fpu::opaque(s)); // 0x80000000 for what it cannot represent
		if (static_cast<int32_t>(ops::fpu::toBits(s)) >= 0x4F000000)
			result = 0x7FFFFFFF;
		return ops::fpu::fromBits(static_cast<uint32_t>(result));
	}

	// ---- comparisons: the condition, which the instruction puts in FCR31 ------------
	PS2N_FPU_INLINE bool C_EQ_S(Fpr s, Fpr t) { return _mm_ucomieq_ss(ops::fpu::comparand(s), ops::fpu::comparand(t)) != 0; }
	PS2N_FPU_INLINE bool C_LT_S(Fpr s, Fpr t) { return _mm_ucomilt_ss(ops::fpu::comparand(s), ops::fpu::comparand(t)) != 0; }
	PS2N_FPU_INLINE bool C_LE_S(Fpr s, Fpr t) { return _mm_ucomile_ss(ops::fpu::comparand(s), ops::fpu::comparand(t)) != 0; }
} // namespace ps2n::ee::val

// ---- the instructions, on the core's registers ----------------------------------------
namespace ps2n::ee::ops
{
	namespace fpu
	{
		// The core's FPU register `n` as a value, and a value into it.
		inline val::Fpr reg(uint32_t n) { return fromBits(fpuRegs.fpr[n].UL); }
		inline val::Fpr accumulator() { return fromBits(fpuRegs.ACC.UL); }
		inline void set(uint32_t n, val::Fpr v) { store(&fpuRegs.fpr[n].UL, v); }
		inline void setAccumulator(val::Fpr v) { store(&fpuRegs.ACC.UL, v); }
	} // namespace fpu

	// ---- moves ----------------------------------------------------------------
	inline void MFC1(uint32_t rt, uint32_t fs) { setU64(rt, val::MFC1(fpu::reg(fs))); }
	inline void MTC1(uint32_t rt, uint32_t fs) { fpu::set(fs, val::MTC1(ru64(rt))); }

	// FCR31 reads with its always-zero bits clear and its always-one bits set;
	// every control register below 16 reads as FCR0. Only FCR31 can be written,
	// and takes whatever is written.
	inline void CFC1(uint32_t rt, uint32_t fs)
	{
		if (fs >= 16)
			setU32(rt, (fpuRegs.fprc[31] & 0x0083C078u) | 0x01000001u);
		else
			setU32(rt, fpuRegs.fprc[0]);
	}
	inline void CTC1(uint32_t rt, uint32_t fs)
	{
		if (fs == 31)
			fpuRegs.fprc[31] = ru32(rt);
	}

	inline void MOV_S(uint32_t fd, uint32_t fs) { fpuRegs.fpr[fd].UL = fpuRegs.fpr[fs].UL; }

	// ---- on the bits, as the console ---------------------------------------------
	inline void ABS_S(uint32_t fd, uint32_t fs) { fpu::set(fd, val::ABS_S(fpu::reg(fs))); }
	inline void NEG_S(uint32_t fd, uint32_t fs) { fpu::set(fd, val::NEG_S(fpu::reg(fs))); }
	inline void MAX_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpu::set(fd, val::MAX_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void MIN_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpu::set(fd, val::MIN_S(fpu::reg(fs), fpu::reg(ft))); }

	// ---- arithmetic -------------------------------------------------------------
	inline void ADD_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpu::set(fd, val::ADD_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void SUB_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpu::set(fd, val::SUB_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void MUL_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpu::set(fd, val::MUL_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void ADDA_S(uint32_t fs, uint32_t ft) { fpu::setAccumulator(val::ADD_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void SUBA_S(uint32_t fs, uint32_t ft) { fpu::setAccumulator(val::SUB_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void MULA_S(uint32_t fs, uint32_t ft) { fpu::setAccumulator(val::MUL_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void MADD_S(uint32_t fd, uint32_t fs, uint32_t ft)
	{
		fpu::set(fd, val::MADD_S(fpu::accumulator(), fpu::reg(fs), fpu::reg(ft)));
	}
	inline void MSUB_S(uint32_t fd, uint32_t fs, uint32_t ft)
	{
		fpu::set(fd, val::MSUB_S(fpu::accumulator(), fpu::reg(fs), fpu::reg(ft)));
	}
	inline void MADDA_S(uint32_t fs, uint32_t ft) { fpu::setAccumulator(val::MADDA_S(fpu::accumulator(), fpu::reg(fs), fpu::reg(ft))); }
	inline void MSUBA_S(uint32_t fs, uint32_t ft) { fpu::setAccumulator(val::MSUB_S(fpu::accumulator(), fpu::reg(fs), fpu::reg(ft))); }

	inline void DIV_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpu::set(fd, val::DIV_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void SQRT_S(uint32_t fd, uint32_t ft) { fpu::set(fd, val::SQRT_S(fpu::reg(ft))); }
	inline void RSQRT_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpu::set(fd, val::RSQRT_S(fpu::reg(fs), fpu::reg(ft))); }

	// ---- conversions --------------------------------------------------------------
	inline void CVT_S_W(uint32_t fd, uint32_t fs) { fpu::set(fd, val::CVT_S_W(fpu::reg(fs))); }
	inline void CVT_W_S(uint32_t fd, uint32_t fs) { fpu::set(fd, val::CVT_W_S(fpu::reg(fs))); }

	// ---- comparisons ----------------------------------------------------------------
	inline void C_F_S(uint32_t, uint32_t) { fpu::setCondition(false); }
	inline void C_EQ_S(uint32_t fs, uint32_t ft) { fpu::setCondition(val::C_EQ_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void C_LT_S(uint32_t fs, uint32_t ft) { fpu::setCondition(val::C_LT_S(fpu::reg(fs), fpu::reg(ft))); }
	inline void C_LE_S(uint32_t fs, uint32_t ft) { fpu::setCondition(val::C_LE_S(fpu::reg(fs), fpu::reg(ft))); }

	// ---- load and store ---------------------------------------------------------------
	inline void LWC1(uint32_t ft, uint32_t base, int32_t off) { fpuRegs.fpr[ft].UL = read32(effAddr(base, off)); }
	inline void SWC1(uint32_t ft, uint32_t base, int32_t off) { write32(effAddr(base, off), fpuRegs.fpr[ft].UL); }
} // namespace ps2n::ee::ops
