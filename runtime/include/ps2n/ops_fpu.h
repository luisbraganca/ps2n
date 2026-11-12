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
// This is x86-64 code. ADR 0001 makes ARM64 optional; a port needs these
// operations reproduced there, bit for bit.
#pragma once

#include "ps2n/ee_memory.h"
#include "ps2n/ee_state.h"
#include "ps2n/ops_int.h"

#include "Config.h"

#include <cstdint>
#include <emmintrin.h>
#include <xmmintrin.h>

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

		inline __m128 fromBits(uint32_t bits) { return _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<int>(bits))); }
		inline uint32_t toBits(__m128 v) { return static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(v))); }

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
		inline __m128 opaque(__m128 v)
		{
#if defined(__clang__) || defined(__GNUC__)
			__asm__ volatile("" : "+x"(v));
			return v;
#else
			volatile uint32_t hidden = toBits(v);
			return fromBits(hidden);
#endif
		}
		inline int32_t opaque(int32_t v)
		{
#if defined(__clang__) || defined(__GNUC__)
			__asm__ volatile("" : "+r"(v));
			return v;
#else
			volatile int32_t hidden = v;
			return hidden;
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
		// most negative. Integer minimums on the bits (PMINSD, PMINUD).
		inline uint32_t clampBySign(uint32_t x)
		{
			if (static_cast<int32_t>(x) > static_cast<int32_t>(kMax))
				x = kMax;
			if (x > kMin)
				x = kMin;
			return x;
		}

		// fpuFloat2: the clamp of an operand.
		inline uint32_t clampOperand(uint32_t x) { return clampResults() ? clampBySign(x) : x; }

		// fpuFloat (ClampValues): the clamp of a result. Any NaN and +Inf
		// become the largest float, -Inf the most negative. MINSS then MAXSS,
		// in that order: a NaN comes out of MINSS as its second operand.
		inline __m128 clampResult(__m128 v)
		{
			if (clampResults())
			{
				v = _mm_min_ss(v, fromBits(kMax));
				v = _mm_max_ss(v, fromBits(kMin));
			}
			return v;
		}

		// Whether x compares equal to zero (CMPEQSS against 0.0). Under the
		// EE's control register a denormal does.
		inline bool isZero(uint32_t x)
		{
			return (_mm_movemask_ps(_mm_cmpeq_ss(_mm_setzero_ps(), opaque(fromBits(x)))) & 1) != 0;
		}

		// FPU_ADD_SUB: the EE's adder keeps one guard bit where IEEE keeps
		// more. Of the operand with the smaller exponent, the bits that would
		// be shifted out past it are cleared before the host adds.
		inline __m128 addSub(uint32_t d, uint32_t t, bool subtract)
		{
			const int32_t diff = static_cast<int32_t>((d >> 23) & 0xFF) - static_cast<int32_t>((t >> 23) & 0xFF);
			if (diff >= 25)
				t &= kSign;
			else if (diff > 0)
				t &= 0xFFFFFFFFu << (diff - 1);
			else if (diff <= -25)
				d &= kSign;
			else if (diff < 0)
				d &= 0xFFFFFFFFu << (-diff - 1);

			const __m128 a = opaque(fromBits(d));
			const __m128 b = opaque(fromBits(t));
			return subtract ? _mm_sub_ss(a, b) : _mm_add_ss(a, b);
		}

		// FPU_MUL, with the game fix PCSX2 applies in it when it is switched on.
		inline __m128 multiply(uint32_t s, uint32_t t)
		{
			if (EmuConfig.Gamefixes.FpuMulHack && s == 0x3E800000u && t == 0x40490FDBu) [[unlikely]]
				return fromBits(0x3F490FDAu);
			return _mm_mul_ss(opaque(fromBits(s)), opaque(fromBits(t)));
		}

		inline uint32_t add(uint32_t s, uint32_t t)
		{
			if (clampOperands())
			{
				s = clampOperand(s);
				t = clampOperand(t);
			}
			return toBits(clampResult(addSub(s, t, false)));
		}

		inline uint32_t sub(uint32_t s, uint32_t t)
		{
			if (clampOperands())
			{
				s = clampOperand(s);
				t = clampOperand(t);
			}
			return toBits(clampResult(addSub(s, t, true)));
		}

		inline uint32_t mul(uint32_t s, uint32_t t)
		{
			if (clampOperands())
			{
				s = clampOperand(s);
				t = clampOperand(t);
			}
			return toBits(clampResult(multiply(s, t)));
		}

		// The product MADD and MSUB add to or subtract from the accumulator
		// (`accumulator`: its bits). A plain MULSS, without the game fix; in
		// the Extra mode the product and the accumulator are clamped as
		// results are.
		inline void productAndAcc(uint32_t accumulator, uint32_t s, uint32_t t, uint32_t* product, uint32_t* acc)
		{
			if (clampOperands())
			{
				s = clampOperand(s);
				t = clampOperand(t);
			}
			__m128 p = _mm_mul_ss(opaque(fromBits(s)), opaque(fromBits(t)));
			__m128 a = fromBits(accumulator);
			if (clampOperands())
			{
				a = clampResult(a);
				p = clampResult(p);
			}
			*product = toBits(p);
			*acc = toBits(a);
		}

		// The two orders are the recompiler's (FPU_ADD(product, ACC) for
		// MADD.S, FPU_ADD(ACC, product) for MADDA.S). They only differ in the
		// clamp mode None, when both are NaNs: the first one comes out.
		inline uint32_t madd(uint32_t accumulator, uint32_t s, uint32_t t)
		{
			uint32_t product, acc;
			productAndAcc(accumulator, s, t, &product, &acc);
			return toBits(clampResult(addSub(product, acc, false)));
		}

		inline uint32_t madda(uint32_t accumulator, uint32_t s, uint32_t t)
		{
			uint32_t product, acc;
			productAndAcc(accumulator, s, t, &product, &acc);
			return toBits(clampResult(addSub(acc, product, false)));
		}

		inline uint32_t msub(uint32_t accumulator, uint32_t s, uint32_t t)
		{
			uint32_t product, acc;
			productAndAcc(accumulator, s, t, &product, &acc);
			return toBits(clampResult(addSub(acc, product, true)));
		}

		// UCOMISS on the operands clamped by sign.
		inline void setCondition(bool value)
		{
			if (value)
				fpuRegs.fprc[31] |= kFlagC;
			else
				fpuRegs.fprc[31] &= ~kFlagC;
		}
		inline __m128 comparand(uint32_t x) { return opaque(fromBits(clampBySign(x))); }
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
// A register's 32 bits in, the result's out. Compiled code computes with these
// while it holds the FPU's registers in local variables (recomp.h), and the
// functions further down, which work on the core's registers, are made of
// them: each instruction is defined once. Those that have flags write them to
// FCR31 in the core.
namespace ps2n::ee::val
{
	// ---- moves ----------------------------------------------------------------
	inline uint64_t MFC1(uint32_t fs) { return sext32(fs); }
	inline uint32_t MTC1(uint64_t rt) { return static_cast<uint32_t>(rt); }

	// ---- on the bits, as the console ---------------------------------------------
	inline uint32_t ABS_S(uint32_t s)
	{
		if (ops::fpu::clampedByPcsx2(s)) [[unlikely]]
			ops::fpu::console_followed[0]++;
		return s & ~ops::fpu::kSign;
	}
	inline uint32_t NEG_S(uint32_t s)
	{
		if (ops::fpu::clampedByPcsx2(s)) [[unlikely]]
			ops::fpu::console_followed[0]++;
		return s ^ ops::fpu::kSign;
	}

	// The EE's floats order like sign-and-magnitude integers.
	inline uint32_t MAX_S(uint32_t fs, uint32_t ft)
	{
		const int32_t s = static_cast<int32_t>(fs), t = static_cast<int32_t>(ft);
		ops::fpu::noteMaxMin(s, t);
		return static_cast<uint32_t>((s < 0 && t < 0) ? (s < t ? s : t) : (s > t ? s : t));
	}
	inline uint32_t MIN_S(uint32_t fs, uint32_t ft)
	{
		const int32_t s = static_cast<int32_t>(fs), t = static_cast<int32_t>(ft);
		ops::fpu::noteMaxMin(s, t);
		return static_cast<uint32_t>((s < 0 && t < 0) ? (s > t ? s : t) : (s < t ? s : t));
	}

	// ---- arithmetic -------------------------------------------------------------
	// ADDA.S, SUBA.S and MULA.S are these with the accumulator as destination.
	inline uint32_t ADD_S(uint32_t s, uint32_t t) { return ops::fpu::add(s, t); }
	inline uint32_t SUB_S(uint32_t s, uint32_t t) { return ops::fpu::sub(s, t); }
	inline uint32_t MUL_S(uint32_t s, uint32_t t) { return ops::fpu::mul(s, t); }

	// With the accumulator's bits first. MADDA.S gives the accumulator's new
	// bits; MSUBA.S is MSUB.S with the accumulator as destination.
	inline uint32_t MADD_S(uint32_t acc, uint32_t s, uint32_t t) { return ops::fpu::madd(acc, s, t); }
	inline uint32_t MSUB_S(uint32_t acc, uint32_t s, uint32_t t) { return ops::fpu::msub(acc, s, t); }
	inline uint32_t MADDA_S(uint32_t acc, uint32_t s, uint32_t t) { return ops::fpu::madda(acc, s, t); }

	// Division rounds as the core's "FPU division" control register says
	// (to nearest by default, where everything else rounds toward zero). A
	// zero divisor gives the largest float with the sign of the quotient, and
	// the invalid flag for 0/0, the division-by-zero flag otherwise.
	inline uint32_t DIV_S(uint32_t s, uint32_t t)
	{
		namespace fpu = ops::fpu;
		const fpu::ScopedControl control(EmuConfig.Cpu.FPUDivFPCR.bitmask);

		fpuRegs.fprc[31] &= ~(fpu::kFlagI | fpu::kFlagD);
		if (fpu::isZero(t))
		{
			fpuRegs.fprc[31] |= fpu::isZero(s) ? (fpu::kFlagI | fpu::kFlagSI) : (fpu::kFlagD | fpu::kFlagSD);
			return ((s ^ t) & fpu::kSign) | fpu::kMax;
		}

		uint32_t a = s, b = t;
		if (fpu::clampOperands())
		{
			a = fpu::clampOperand(a);
			b = fpu::clampOperand(b);
		}
		const __m128 quotient = _mm_div_ss(fpu::opaque(fpu::fromBits(a)), fpu::opaque(fpu::fromBits(b)));
		return fpu::toBits(fpu::opaque(fpu::clampResult(quotient)));
	}

	// The square root of the magnitude, rounded to nearest; a negative operand
	// (including -0) sets the invalid flag.
	inline uint32_t SQRT_S(uint32_t t)
	{
		namespace fpu = ops::fpu;
		FPControlRegister nearest = EmuConfig.Cpu.FPUFPCR;
		nearest.SetRoundMode(FPRoundMode::Nearest);
		const fpu::ScopedControl control(nearest.bitmask);

		fpuRegs.fprc[31] &= ~(fpu::kFlagI | fpu::kFlagD);
		if (t & fpu::kSign)
		{
			fpuRegs.fprc[31] |= fpu::kFlagI | fpu::kFlagSI;
			t &= ~fpu::kSign;
		}

		__m128 v = fpu::opaque(fpu::fromBits(t));
		if (fpu::clampResults())
			v = _mm_min_ss(v, fpu::fromBits(fpu::kMax));
		v = _mm_sqrt_ss(fpu::opaque(v));
		if (fpu::clampOperands())
			v = fpu::clampResult(v);
		return fpu::toBits(fpu::opaque(v));
	}

	// d / sqrt(|t|), under the standing control register.
	inline uint32_t RSQRT_S(uint32_t d, uint32_t t)
	{
		namespace fpu = ops::fpu;

		fpuRegs.fprc[31] &= ~(fpu::kFlagI | fpu::kFlagD);
		if (t & fpu::kSign)
		{
			fpuRegs.fprc[31] |= fpu::kFlagI | fpu::kFlagSI;
			t &= ~fpu::kSign;
		}

		if (fpu::isZero(t))
		{
			fpuRegs.fprc[31] |= fpu::isZero(d) ? (fpu::kFlagI | fpu::kFlagSI) : (fpu::kFlagD | fpu::kFlagSD);
			return (d & fpu::kSign) | fpu::kMax;
		}

		__m128 root = fpu::opaque(fpu::fromBits(t));
		if (fpu::clampOperands())
		{
			root = _mm_min_ss(root, fpu::fromBits(fpu::kMax));
			d = fpu::clampOperand(d);
		}
		root = _mm_sqrt_ss(fpu::opaque(root));
		const __m128 quotient = _mm_div_ss(fpu::opaque(fpu::fromBits(d)), fpu::opaque(root));
		return fpu::toBits(fpu::clampResult(quotient));
	}

	// ---- conversions --------------------------------------------------------------
	// From a 32-bit integer, rounded as the control register says.
	inline uint32_t CVT_S_W(uint32_t s)
	{
		return ops::fpu::toBits(_mm_cvtsi32_ss(_mm_setzero_ps(), ops::fpu::opaque(static_cast<int32_t>(s))));
	}

	// To a 32-bit integer, truncating; what is too large saturates.
	inline uint32_t CVT_W_S(uint32_t s)
	{
		int32_t result = _mm_cvttss_si32(ops::fpu::opaque(ops::fpu::fromBits(s))); // 0x80000000 for what it cannot represent
		if (static_cast<int32_t>(s) >= 0x4F000000)
			result = 0x7FFFFFFF;
		return static_cast<uint32_t>(result);
	}

	// ---- comparisons: the condition, which the instruction puts in FCR31 ------------
	inline bool C_EQ_S(uint32_t s, uint32_t t) { return _mm_ucomieq_ss(ops::fpu::comparand(s), ops::fpu::comparand(t)) != 0; }
	inline bool C_LT_S(uint32_t s, uint32_t t) { return _mm_ucomilt_ss(ops::fpu::comparand(s), ops::fpu::comparand(t)) != 0; }
	inline bool C_LE_S(uint32_t s, uint32_t t) { return _mm_ucomile_ss(ops::fpu::comparand(s), ops::fpu::comparand(t)) != 0; }
} // namespace ps2n::ee::val

// ---- the instructions, on the core's registers ----------------------------------------
namespace ps2n::ee::ops
{
	// ---- moves ----------------------------------------------------------------
	inline void MFC1(uint32_t rt, uint32_t fs) { setU64(rt, val::MFC1(fpuRegs.fpr[fs].UL)); }
	inline void MTC1(uint32_t rt, uint32_t fs) { fpuRegs.fpr[fs].UL = val::MTC1(ru64(rt)); }

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
	inline void ABS_S(uint32_t fd, uint32_t fs) { fpuRegs.fpr[fd].UL = val::ABS_S(fpuRegs.fpr[fs].UL); }
	inline void NEG_S(uint32_t fd, uint32_t fs) { fpuRegs.fpr[fd].UL = val::NEG_S(fpuRegs.fpr[fs].UL); }
	inline void MAX_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpuRegs.fpr[fd].UL = val::MAX_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }
	inline void MIN_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpuRegs.fpr[fd].UL = val::MIN_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }

	// ---- arithmetic -------------------------------------------------------------
	inline void ADD_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpuRegs.fpr[fd].UL = val::ADD_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }
	inline void SUB_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpuRegs.fpr[fd].UL = val::SUB_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }
	inline void MUL_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpuRegs.fpr[fd].UL = val::MUL_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }
	inline void ADDA_S(uint32_t fs, uint32_t ft) { fpuRegs.ACC.UL = val::ADD_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }
	inline void SUBA_S(uint32_t fs, uint32_t ft) { fpuRegs.ACC.UL = val::SUB_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }
	inline void MULA_S(uint32_t fs, uint32_t ft) { fpuRegs.ACC.UL = val::MUL_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }
	inline void MADD_S(uint32_t fd, uint32_t fs, uint32_t ft)
	{
		fpuRegs.fpr[fd].UL = val::MADD_S(fpuRegs.ACC.UL, fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL);
	}
	inline void MSUB_S(uint32_t fd, uint32_t fs, uint32_t ft)
	{
		fpuRegs.fpr[fd].UL = val::MSUB_S(fpuRegs.ACC.UL, fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL);
	}
	inline void MADDA_S(uint32_t fs, uint32_t ft) { fpuRegs.ACC.UL = val::MADDA_S(fpuRegs.ACC.UL, fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }
	inline void MSUBA_S(uint32_t fs, uint32_t ft) { fpuRegs.ACC.UL = val::MSUB_S(fpuRegs.ACC.UL, fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }

	inline void DIV_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpuRegs.fpr[fd].UL = val::DIV_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }
	inline void SQRT_S(uint32_t fd, uint32_t ft) { fpuRegs.fpr[fd].UL = val::SQRT_S(fpuRegs.fpr[ft].UL); }
	inline void RSQRT_S(uint32_t fd, uint32_t fs, uint32_t ft) { fpuRegs.fpr[fd].UL = val::RSQRT_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL); }

	// ---- conversions --------------------------------------------------------------
	inline void CVT_S_W(uint32_t fd, uint32_t fs) { fpuRegs.fpr[fd].UL = val::CVT_S_W(fpuRegs.fpr[fs].UL); }
	inline void CVT_W_S(uint32_t fd, uint32_t fs) { fpuRegs.fpr[fd].UL = val::CVT_W_S(fpuRegs.fpr[fs].UL); }

	// ---- comparisons ----------------------------------------------------------------
	inline void C_F_S(uint32_t, uint32_t) { fpu::setCondition(false); }
	inline void C_EQ_S(uint32_t fs, uint32_t ft) { fpu::setCondition(val::C_EQ_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL)); }
	inline void C_LT_S(uint32_t fs, uint32_t ft) { fpu::setCondition(val::C_LT_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL)); }
	inline void C_LE_S(uint32_t fs, uint32_t ft) { fpu::setCondition(val::C_LE_S(fpuRegs.fpr[fs].UL, fpuRegs.fpr[ft].UL)); }

	// ---- load and store ---------------------------------------------------------------
	inline void LWC1(uint32_t ft, uint32_t base, int32_t off) { fpuRegs.fpr[ft].UL = read32(effAddr(base, off)); }
	inline void SWC1(uint32_t ft, uint32_t base, int32_t off) { write32(effAddr(base, off), fpuRegs.fpr[ft].UL); }
} // namespace ps2n::ee::ops
