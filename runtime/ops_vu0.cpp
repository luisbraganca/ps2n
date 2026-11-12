// SPDX-License-Identifier: GPL-3.0-or-later
//
// VU0's instructions in EE code. ps2n/ops_vu0.h says what they agree with and
// where that is not the console; the comments here name the part of microVU
// (pcsx2/x86/microVU_*.inl, PCSX2 v2.9.170) that each piece reproduces.
//
// microVU computes a single-component instruction in the first lane of a host
// register, and the others in their own lanes. Every operation it uses is the
// same in every lane, so here each component is computed in its own lane, all
// four at once, and the components the instruction does not write are left
// out at the end. The exceptions, VOPMULA and VOPMSUB, mix the lanes, and say
// so where they do.

#include "ps2n/ops_vu0.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/MTVU.h"
#include "pcsx2/VUmicro.h"

#include <smmintrin.h>

#if !defined(__clang__) && !defined(__GNUC__)
#error "ops_vu0.cpp writes the host's SSE instructions as GNU inline assembly"
#endif

namespace
{
	using ps2n::ee::vu0::kMac;
	using ps2n::ee::vu0::kStatus;

	// ---- the host's floating-point instructions ------------------------------------
	// As microVU emits them: the first operand is the destination and the first
	// source, which decides which NaN comes out of two. They are written as
	// assembly so that the C++ compiler neither swaps the operands of an
	// addition or a multiplication nor computes anything itself: it would round
	// to nearest and keep denormals, where the host's control register (the
	// EE's, under which PCSX2's EE recompiler runs these too) says otherwise.

	inline __m128 ADDPS(__m128 a, __m128 b)
	{
		__asm__("addps %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128 ADDSS(__m128 a, __m128 b)
	{
		__asm__("addss %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128 SUBPS(__m128 a, __m128 b)
	{
		__asm__("subps %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128 MULPS(__m128 a, __m128 b)
	{
		__asm__("mulps %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128 DIVSS(__m128 a, __m128 b)
	{
		__asm__("divss %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128 MINPS(__m128 a, __m128 b)
	{
		__asm__("minps %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128 MAXPS(__m128 a, __m128 b)
	{
		__asm__("maxps %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128 MINSS(__m128 a, __m128 b)
	{
		__asm__("minss %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128 SQRTSS(__m128 a)
	{
		__asm__("sqrtss %0, %0" : "+x"(a));
		return a;
	}
	inline __m128 CMPEQPS(__m128 a, __m128 b)
	{
		__asm__("cmpeqps %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128 CMPNLTPS(__m128 a, __m128 b)
	{
		__asm__("cmpnltps %1, %0" : "+x"(a) : "x"(b));
		return a;
	}
	inline __m128i CVTTPS2DQ(__m128 a)
	{
		__m128i r;
		__asm__("cvttps2dq %1, %0" : "=x"(r) : "x"(a));
		return r;
	}
	inline __m128 CVTDQ2PS(__m128i a)
	{
		__m128 r;
		__asm__("cvtdq2ps %1, %0" : "=x"(r) : "x"(a));
		return r;
	}

	// ---- registers --------------------------------------------------------------------

	inline __m128 Float(__m128i v) { return _mm_castsi128_ps(v); }
	inline __m128i Int(__m128 v) { return _mm_castps_si128(v); }
	inline __m128 All(u32 bits) { return Float(_mm_set1_epi32(static_cast<int>(bits))); }
	inline __m128 Scalar(u32 bits) { return Float(_mm_cvtsi32_si128(static_cast<int>(bits))); }
	inline u32 Bits(__m128 v) { return static_cast<u32>(_mm_cvtsi128_si32(Int(v))); }

	inline __m128 VF(u32 r) { return Float(_mm_load_si128(reinterpret_cast<const __m128i*>(VU0.VF[r].UL))); }
	inline __m128 ACC() { return Float(_mm_load_si128(reinterpret_cast<const __m128i*>(VU0.ACC.UL))); }

	// The lanes a destination field (bit 3 for x to bit 0 for w) writes.
	alignas(16) constexpr u32 kDestLanes[16][4] = {
		{0, 0, 0, 0}, {0, 0, 0, ~0u}, {0, 0, ~0u, 0}, {0, 0, ~0u, ~0u},
		{0, ~0u, 0, 0}, {0, ~0u, 0, ~0u}, {0, ~0u, ~0u, 0}, {0, ~0u, ~0u, ~0u},
		{~0u, 0, 0, 0}, {~0u, 0, 0, ~0u}, {~0u, 0, ~0u, 0}, {~0u, 0, ~0u, ~0u},
		{~0u, ~0u, 0, 0}, {~0u, ~0u, 0, ~0u}, {~0u, ~0u, ~0u, 0}, {~0u, ~0u, ~0u, ~0u},
	};

	// The four bits reversed: a destination field to the lanes it writes (lane 0
	// is x), and lanes back to flag positions (x is the highest bit).
	constexpr u8 kFlip[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};

	inline __m128i Merge(__m128i old_value, __m128i value, u32 dest)
	{
		const __m128i lanes = _mm_load_si128(reinterpret_cast<const __m128i*>(kDestLanes[dest]));
		return _mm_blendv_epi8(old_value, value, lanes);
	}

	// A result written to a floating-point register: the components of `dest`.
	// VF00 is never written (writeBackReg and clearNeeded in microVU_IR.h).
	inline void WriteVF(u32 r, u32 dest, __m128 value)
	{
		if (r == 0)
			return;
		__m128i* reg = reinterpret_cast<__m128i*>(VU0.VF[r].UL);
		_mm_store_si128(reg, Merge(_mm_load_si128(reg), Int(value), dest));
	}
	inline void WriteACC(u32 dest, __m128 value)
	{
		__m128i* reg = reinterpret_cast<__m128i*>(VU0.ACC.UL);
		_mm_store_si128(reg, Merge(_mm_load_si128(reg), Int(value), dest));
	}

	// A component broadcast to every lane (mVUunpack_xyzw, mVUloadIreg, getQreg).
	inline __m128 Broadcast(u32 bits) { return All(bits); }

	// ---- clamping (microVU_Clamp.inl) --------------------------------------------------
	// The VU clamp mode, read as the recompiler reads it when it compiles:
	// CHECK_VU_OVERFLOW, clampE (CHECK_VU_EXTRA_OVERFLOW) and
	// CHECK_VU_SIGN_OVERFLOW for VU0. The four modes PCSX2 offers are None (all
	// off), Normal (overflow), Extra (and extra) and Extra + Preserve Sign (and
	// sign). Whether a register may be clamped at all is checkVFClamp
	// (microVU_IR.h): not when it was read from VF00, and that stays so for the
	// result computed into it.
	struct Clamp
	{
		bool overflow;
		bool extra;
		bool sign;
	};

	inline Clamp Settings()
	{
		const auto& r = EmuConfig.Cpu.Recompiler;
		return {r.vu0Overflow, r.vu0ExtraOverflow, r.vu0SignOverflow};
	}

	// mVUclamp1: NaN and +Inf to the largest float, -Inf to the most negative.
	// MINPS then MAXPS, against those two.
	inline __m128 Clamp1(__m128 v, const Clamp& c, bool clampable, bool clamp_e)
	{
		if (((!c.extra && c.overflow) || (c.extra && clamp_e)) && clampable)
		{
			v = MINPS(v, All(0x7F7FFFFFu));
			v = MAXPS(v, All(0xFF7FFFFFu));
		}
		return v;
	}

	// mVUclamp2: the clamp of an operand. With the sign preserved, on the bits
	// (PMINSD, PMINUD): a NaN keeps its sign.
	inline __m128 Clamp2(__m128 v, const Clamp& c, bool clampable, bool clamp_e)
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
	inline __m128 Clamp3(__m128 v, const Clamp& c, bool clampable)
	{
		return (c.extra && clampable) ? Clamp2(v, c, clampable, true) : v;
	}
	inline __m128 Clamp4(__m128 v, const Clamp& c, bool clampable)
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

	// clampOp (microVU_Misc.inl): `to` op `from`, the result in `to`'s register.
	inline __m128 Compute(Arith op, __m128 to, bool to_clampable, __m128 from, bool from_clampable, const Clamp& c)
	{
		to = Clamp3(to, c, to_clampable);
		from = Clamp3(from, c, from_clampable);
		switch (op)
		{
			case Arith::Add: to = ADDPS(to, from); break;
			case Arith::Sub: to = SUBPS(to, from); break;
			case Arith::Mul: to = MULPS(to, from); break;
			case Arith::Div: to = DIVSS(to, from); break;
		}
		return Clamp4(to, c, to_clampable);
	}

	// ---- flags (mVUupdateFlags in microVU_Upper.inl) -----------------------------------

	// microVU keeps the status flag register in a layout of its own while it
	// computes: per-component zero and sign flags, the rest moved up. These
	// are its conversions (mVUallocSFLAGd and mVUallocSFLAGc in
	// microVU_Alloc.inl); a status flag goes through both on its way.
	inline u32 Denormalize(u32 s)
	{
		return ((s >> 3) & 0x18u) | ((s << 11) & 0x1800u) | ((s << 14) & 0x3CF0000u);
	}
	inline u32 Normalize(u32 d)
	{
		u32 s = 0;
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

	// The zero and sign flags of the components of `dest` in `result`, to the
	// registers `flags` says; and with PCSX2's VU overflow game fix, the
	// overflow flags of those whose magnitude is the largest float or more.
	inline void UpdateFlags(__m128 result, u32 dest, u32 flags)
	{
		if (!(flags & (kStatus | kMac)))
			return;

		const u32 sign = kFlip[_mm_movemask_ps(result)] & dest;
		const u32 zero = kFlip[_mm_movemask_ps(CMPEQPS(_mm_setzero_ps(), result))] & dest;
		u32 mac = (sign << 4) | zero;

		u32 overflow = 0;
		if ((flags & kStatus) && EmuConfig.Gamefixes.VUOverflowHack) [[unlikely]]
		{
			const __m128 magnitude = _mm_and_ps(result, All(0x7FFFFFFFu));
			overflow = kFlip[_mm_movemask_ps(CMPNLTPS(magnitude, All(0x7F7FFFFFu)))] & dest;
			if (flags & kMac)
				mac |= overflow << 12;
		}

		if (flags & kMac)
			VU0.VI[REG_MAC_FLAG].UL = mac;
		if (flags & kStatus)
		{
			u32 d = Denormalize(VU0.VI[REG_STATUS_FLAG].UL) & 0xFFFC00FFu; // clears O, U, S and Z
			if (overflow)
				d |= 0x820000u; // O and OS
			d |= (mac & 0xFFu) | ((mac & 0xFFu) << 8);
			VU0.VI[REG_STATUS_FLAG].UL = Normalize(d);
		}
	}

	// ---- the FMAC instructions (microVU_Upper.inl) -------------------------------------

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

	inline Operand Fetch(Second k, u32 ft, u32 bc)
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
	enum : u32
	{
		cFt = 1,
		cFs = 2,
		cACC = 4,
	};

	inline bool Single(u32 dest) { return dest == 8 || dest == 4 || dest == 2 || dest == 1; }

	// MAX and MINI on the bits (MIN_MAX_PS, MIN_MAX_SS in microVU_Misc.inl,
	// which order the same way): negative floats have their magnitude bits
	// inverted, and the two are compared as signed integers. Of two equal
	// values, `from`.
	inline __m128 MinMax(__m128 to, __m128 from, bool min)
	{
		const __m128i a = Int(to), b = Int(from);
		const __m128i ka = _mm_xor_si128(_mm_srli_epi32(_mm_srai_epi32(a, 31), 1), a);
		const __m128i kb = _mm_xor_si128(_mm_srli_epi32(_mm_srai_epi32(b, 31), 1), b);
		const __m128i pick = min ? _mm_cmpgt_epi32(kb, ka) : _mm_cmpgt_epi32(ka, kb);
		return Float(_mm_or_si128(_mm_and_si128(a, pick), _mm_andnot_si128(pick, b)));
	}

	// ADD_SS_TriAceHack: VADDi of one component with PCSX2's VU add/sub game
	// fix. Of the two, the one whose exponent is smaller by 25 or more is
	// replaced by a zero of its sign, and nothing is clamped.
	inline __m128 TriAceAdd(__m128 to, __m128 from, u32 dest)
	{
		const int lane = dest == 8 ? 0 : (dest == 4 ? 1 : (dest == 2 ? 2 : 3));
		alignas(16) u32 a[4], b[4];
		_mm_store_si128(reinterpret_cast<__m128i*>(a), Int(to));
		_mm_store_si128(reinterpret_cast<__m128i*>(b), Int(from));
		u32 x = a[lane], y = b[lane];
		const int diff = static_cast<int>((y >> 23) & 0xFF) - static_cast<int>((x >> 23) & 0xFF);
		if (diff <= -25)
			y &= 0x80000000u;
		else if (diff >= 25)
			x &= 0x80000000u;
		return Broadcast(Bits(ADDSS(Scalar(x), Scalar(y))));
	}

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
	void FMACa(Kind kind, Second k, bool to_acc, u32 clamp, u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc)
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
	void FMACb(bool subtract, Second k, u32 clamp, u32 flags, u32 dest, u32 fs, u32 ft, u32 bc)
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
	void FMACc(Second k, u32 clamp, u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc)
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
	void FMACd(Second k, u32 clamp, u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc)
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

	// The clamps of the instructions that have them, by whether all four
	// components are written (_XYZW_PS) and, for MADDw and MSUB, in COP2.
	inline u32 WhenAll(u32 dest, u32 all, u32 otherwise) { return dest == 0xF ? all : otherwise; }

	// ---- the conversions -------------------------------------------------------------

	// mVU_FTOIx: CVTTPS2DQ gives 0x80000000 for what it cannot represent; a
	// positive value of that kind becomes 0x7FFFFFFF.
	void FTOI(u32 dest, u32 ft, u32 fs, u32 scale)
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
	void ITOF(u32 dest, u32 ft, u32 fs, u32 scale)
	{
		if (ft == 0)
			return;
		__m128 v = CVTDQ2PS(Int(VF(fs)));
		if (scale)
			v = MULPS(v, All(scale));
		WriteVF(ft, dest, v);
	}

	// ---- VU0's memory (mVUaddrFix in microVU_Misc.inl) ---------------------------------
	// A quadword address: VU0's 4 KB of data memory, or from 0x400 up VU1's
	// floating-point and integer registers, which wait for VU1's thread first.
	inline u8* Address(u32 a)
	{
		if (a & 0x400)
		{
			if (THREAD_VU1)
				vu1Thread.WaitVU();
			return reinterpret_cast<u8*>(VU1.VF) + (a & 0x3F) * 16;
		}
		return VU0.Mem + (a & 0xFF) * 16;
	}

	// The integer registers are 16 bits; VI00 reads as 0 whatever the
	// register holds, and a write to it goes nowhere. `r` is the instruction's
	// field, of which microVU uses the low four bits.
	inline u32 VI(u32 r)
	{
		r &= 15;
		return r ? VU0.VI[r].US[0] : 0;
	}
	inline void SetVI(u32 r, u32 value)
	{
		r &= 15;
		if (r)
			VU0.VI[r].US[0] = static_cast<u16>(value);
	}

	// mVUloadReg, mVUsaveReg: the components of `dest`.
	inline __m128 LoadQuad(const u8* p) { return Float(_mm_load_si128(reinterpret_cast<const __m128i*>(p))); }
	inline void StoreQuad(u8* p, u32 dest, __m128 value)
	{
		__m128i* q = reinterpret_cast<__m128i*>(p);
		_mm_store_si128(q, Merge(_mm_load_si128(q), Int(value), dest));
	}

	// VSQI and VSQD: fs stored. With no component in `dest`, mVUsaveReg's
	// default case stores all four, and the register allocator has read VF00
	// as a cleared register (it does so for a partial write without w).
	inline void StoreVF(u8* p, u32 dest, u32 fs)
	{
		if (dest == 0)
			StoreQuad(p, 0xF, fs ? VF(fs) : _mm_setzero_ps());
		else
			StoreQuad(p, dest, VF(fs));
	}

	// The scales of FTOI4/12/15 and ITOF4/12/15 (mVUglob).
	constexpr u32 kFloat16 = 0x41800000u; // 16.0
	constexpr u32 kFloat4096 = 0x45800000u; // 4096.0
	constexpr u32 kFloat32768 = 0x47000000u; // 32768.0
	constexpr u32 kFloat1_16 = 0x3D800000u; // 0.0625
	constexpr u32 kFloat1_4096 = 0x39800000u; // 0.000244140625
	constexpr u32 kFloat1_32768 = 0x38000000u; // 0.000030517578125

	// The flags of VDIV, VSQRT and VRSQRT in microVU's layout (divI, divD in
	// microVU_Misc.h): invalid and division by zero, each with its sticky one.
	constexpr u32 kDivI = 0x1040000u;
	constexpr u32 kDivD = 0x2080000u;

	inline void DivFlags(u32 flags, u32 div)
	{
		if (flags & kStatus)
			VU0.VI[REG_STATUS_FLAG].UL = Normalize((Denormalize(VU0.VI[REG_STATUS_FLAG].UL) & ~0xC0000u) | div);
	}

	// testZero: whether the first lane compares equal to zero, under the
	// control register (a denormal does when denormals are zero).
	inline bool IsZero(__m128 v) { return (_mm_movemask_ps(CMPEQPS(_mm_setzero_ps(), v)) & 1) != 0; }
} // namespace

namespace ps2n::ee::ops
{
	using ps2n::ee::vu0::kMac;
	using ps2n::ee::vu0::kStatus;

	// ---- FMAC: the vector forms ----
	void VADD(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { FMACa(Kind::Add, Second::Vector, false, 0, flags, dest, fd, fs, ft, 0); }
	void VSUB(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { FMACa(Kind::Sub, Second::Vector, false, WhenAll(dest, cFs | cFt, 0), flags, dest, fd, fs, ft, 0); }
	void VMUL(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { FMACa(Kind::Mul, Second::Vector, false, WhenAll(dest, cFs | cFt, cFs), flags, dest, fd, fs, ft, 0); }
	void VMADD(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { FMACc(Second::Vector, 0, flags, dest, fd, fs, ft, 0); }
	void VMSUB(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { FMACd(Second::Vector, cFs, flags, dest, fd, fs, ft, 0); }
	void VMAX(u32 dest, u32 fd, u32 fs, u32 ft) { FMACa(Kind::Max, Second::Vector, false, 0, 0, dest, fd, fs, ft, 0); }
	void VMINI(u32 dest, u32 fd, u32 fs, u32 ft) { FMACa(Kind::Min, Second::Vector, false, 0, 0, dest, fd, fs, ft, 0); }

	// ---- broadcast ----
	void VADDbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { FMACa(Kind::Add, Second::Bc, false, 0, flags, dest, fd, fs, ft, bc); }
	void VSUBbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { FMACa(Kind::Sub, Second::Bc, false, WhenAll(dest, cFs | cFt, 0), flags, dest, fd, fs, ft, bc); }
	void VMULbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { FMACa(Kind::Mul, Second::Bc, false, WhenAll(dest, cFs | cFt, cFs), flags, dest, fd, fs, ft, bc); }
	void VMADDbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { FMACc(Second::Bc, bc == 3 ? (cACC | cFt | cFs) : cFs, flags, dest, fd, fs, ft, bc); }
	void VMSUBbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { FMACd(Second::Bc, 0, flags, dest, fd, fs, ft, bc); }
	void VMAXbc(u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { FMACa(Kind::Max, Second::Bc, false, 0, 0, dest, fd, fs, ft, bc); }
	void VMINIbc(u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { FMACa(Kind::Min, Second::Bc, false, 0, 0, dest, fd, fs, ft, bc); }

	// ---- Q ----
	void VADDq(u32 flags, u32 dest, u32 fd, u32 fs) { FMACa(Kind::Add, Second::Q, false, 0, flags, dest, fd, fs, 0, 0); }
	void VSUBq(u32 flags, u32 dest, u32 fd, u32 fs) { FMACa(Kind::Sub, Second::Q, false, WhenAll(dest, cFs | cFt, 0), flags, dest, fd, fs, 0, 0); }
	void VMULq(u32 flags, u32 dest, u32 fd, u32 fs) { FMACa(Kind::Mul, Second::Q, false, WhenAll(dest, cFs | cFt, cFs), flags, dest, fd, fs, 0, 0); }
	void VMADDq(u32 flags, u32 dest, u32 fd, u32 fs) { FMACc(Second::Q, 0, flags, dest, fd, fs, 0, 0); }
	void VMSUBq(u32 flags, u32 dest, u32 fd, u32 fs) { FMACd(Second::Q, 0, flags, dest, fd, fs, 0, 0); }

	// ---- I ----
	void VADDi(u32 flags, u32 dest, u32 fd, u32 fs) { FMACa(Kind::AddI, Second::I, false, 0, flags, dest, fd, fs, 0, 0); }
	void VSUBi(u32 flags, u32 dest, u32 fd, u32 fs) { FMACa(Kind::Sub, Second::I, false, WhenAll(dest, cFs | cFt, 0), flags, dest, fd, fs, 0, 0); }
	void VMULi(u32 flags, u32 dest, u32 fd, u32 fs) { FMACa(Kind::Mul, Second::I, false, WhenAll(dest, cFs | cFt, cFs), flags, dest, fd, fs, 0, 0); }
	void VMADDi(u32 flags, u32 dest, u32 fd, u32 fs) { FMACc(Second::I, 0, flags, dest, fd, fs, 0, 0); }
	void VMSUBi(u32 flags, u32 dest, u32 fd, u32 fs) { FMACd(Second::I, 0, flags, dest, fd, fs, 0, 0); }
	void VMAXi(u32 dest, u32 fd, u32 fs) { FMACa(Kind::Max, Second::I, false, 0, 0, dest, fd, fs, 0, 0); }
	void VMINIi(u32 dest, u32 fd, u32 fs) { FMACa(Kind::Min, Second::I, false, 0, 0, dest, fd, fs, 0, 0); }

	// ---- into ACC ----
	void VADDA(u32 flags, u32 dest, u32 fs, u32 ft) { FMACa(Kind::Add, Second::Vector, true, 0, flags, dest, 0, fs, ft, 0); }
	void VSUBA(u32 flags, u32 dest, u32 fs, u32 ft) { FMACa(Kind::Sub, Second::Vector, true, 0, flags, dest, 0, fs, ft, 0); }
	void VMULA(u32 flags, u32 dest, u32 fs, u32 ft) { FMACa(Kind::Mul, Second::Vector, true, 0, flags, dest, 0, fs, ft, 0); }
	void VMADDA(u32 flags, u32 dest, u32 fs, u32 ft) { FMACb(false, Second::Vector, 0, flags, dest, fs, ft, 0); }
	void VMSUBA(u32 flags, u32 dest, u32 fs, u32 ft) { FMACb(true, Second::Vector, 0, flags, dest, fs, ft, 0); }

	void VADDAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { FMACa(Kind::Add, Second::Bc, true, 0, flags, dest, 0, fs, ft, bc); }
	void VSUBAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { FMACa(Kind::Sub, Second::Bc, true, 0, flags, dest, 0, fs, ft, bc); }
	void VMULAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { FMACa(Kind::Mul, Second::Bc, true, bc == 3 ? WhenAll(dest, cFs | cFt, cFs) : cFs, flags, dest, 0, fs, ft, bc); }
	void VMADDAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { FMACb(false, Second::Bc, cFs, flags, dest, fs, ft, bc); }
	void VMSUBAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { FMACb(true, Second::Bc, 0, flags, dest, fs, ft, bc); }

	void VADDAq(u32 flags, u32 dest, u32 fs) { FMACa(Kind::Add, Second::Q, true, 0, flags, dest, 0, fs, 0, 0); }
	void VSUBAq(u32 flags, u32 dest, u32 fs) { FMACa(Kind::Sub, Second::Q, true, 0, flags, dest, 0, fs, 0, 0); }
	void VMULAq(u32 flags, u32 dest, u32 fs) { FMACa(Kind::Mul, Second::Q, true, 0, flags, dest, 0, fs, 0, 0); }
	void VMADDAq(u32 flags, u32 dest, u32 fs) { FMACb(false, Second::Q, 0, flags, dest, fs, 0, 0); }
	void VMSUBAq(u32 flags, u32 dest, u32 fs) { FMACb(true, Second::Q, 0, flags, dest, fs, 0, 0); }

	void VADDAi(u32 flags, u32 dest, u32 fs) { FMACa(Kind::Add, Second::I, true, 0, flags, dest, 0, fs, 0, 0); }
	void VSUBAi(u32 flags, u32 dest, u32 fs) { FMACa(Kind::Sub, Second::I, true, 0, flags, dest, 0, fs, 0, 0); }
	void VMULAi(u32 flags, u32 dest, u32 fs) { FMACa(Kind::Mul, Second::I, true, 0, flags, dest, 0, fs, 0, 0); }
	void VMADDAi(u32 flags, u32 dest, u32 fs) { FMACb(false, Second::I, 0, flags, dest, fs, 0, 0); }
	void VMSUBAi(u32 flags, u32 dest, u32 fs) { FMACb(true, Second::I, 0, flags, dest, fs, 0, 0); }

	// ---- the outer product ----
	// mVU_OPMULA: ACC = (fs.y ft.z, fs.z ft.x, fs.x ft.y), lanes moved by
	// PSHUFD before the multiplication. With a single component, microVU loads
	// that component alone into the first lane of a cleared register when its
	// allocator holds neither register, and then multiplies the zeros of the
	// second and third lanes.
	void VOPMULA(u32 flags, u32 dest, u32 fs, u32 ft)
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
	void VOPMSUB(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft)
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
	void VABS(u32 dest, u32 ft, u32 fs)
	{
		if (ft == 0)
			return;
		WriteVF(ft, dest, _mm_and_ps(VF(fs), All(0x7FFFFFFFu)));
	}

	void VMOVE(u32 dest, u32 ft, u32 fs) { WriteVF(ft, dest, VF(fs)); }

	// mVU_MR32: the components rotated, x from y and w from x.
	void VMR32(u32 dest, u32 ft, u32 fs) { WriteVF(ft, dest, Float(_mm_shuffle_epi32(Int(VF(fs)), 0x39))); }

	void VFTOI0(u32 dest, u32 ft, u32 fs) { FTOI(dest, ft, fs, 0); }
	void VFTOI4(u32 dest, u32 ft, u32 fs) { FTOI(dest, ft, fs, kFloat16); }
	void VFTOI12(u32 dest, u32 ft, u32 fs) { FTOI(dest, ft, fs, kFloat4096); }
	void VFTOI15(u32 dest, u32 ft, u32 fs) { FTOI(dest, ft, fs, kFloat32768); }
	void VITOF0(u32 dest, u32 ft, u32 fs) { ITOF(dest, ft, fs, 0); }
	void VITOF4(u32 dest, u32 ft, u32 fs) { ITOF(dest, ft, fs, kFloat1_16); }
	void VITOF12(u32 dest, u32 ft, u32 fs) { ITOF(dest, ft, fs, kFloat1_4096); }
	void VITOF15(u32 dest, u32 ft, u32 fs) { ITOF(dest, ft, fs, kFloat1_32768); }

	// mVU_CLIP: x, y and z of fs against the magnitude of ft.w, on the bits;
	// a denormal component counts as +0. The clip flag register shifts up six
	// and keeps 24 bits. The destination field is not looked at.
	void VCLIPw(u32 dest, u32 fs, u32 ft)
	{
		(void)dest;
		const __m128i s = Int(VF(fs));
		const __m128i w = _mm_and_si128(_mm_set1_epi32(static_cast<int>(VU0.VF[ft].UL[3])), _mm_set1_epi32(0x7FFFFFFF));
		const __m128i denormal = _mm_cmpeq_epi32(_mm_and_si128(s, _mm_set1_epi32(0x7F800000)), _mm_setzero_si128());
		const __m128i v = _mm_andnot_si128(denormal, s);
		const __m128i negated = _mm_xor_si128(v, _mm_set1_epi32(static_cast<int>(0x80000000u)));
		const u32 above = static_cast<u32>(_mm_movemask_ps(Float(_mm_cmpgt_epi32(v, w)))); // +x > |w|, by lane
		const u32 below = static_cast<u32>(_mm_movemask_ps(Float(_mm_cmpgt_epi32(negated, w)))); // -x > |w|
		u32 clip = 0;
		for (u32 lane = 0; lane < 3; lane++)
			clip |= (((above >> lane) & 1) << (2 * lane)) | (((below >> lane) & 1) << (2 * lane + 1));
		VU0.VI[REG_CLIP_FLAG].UL = ((VU0.VI[REG_CLIP_FLAG].UL << 6) & 0xFFFFFFu) | clip;
	}

	// ---- division (microVU_Lower.inl) ----
	// mVU_DIV: Q = fs.fsf / ft.ftf.
	void VDIV(u32 flags, u32 fs, u32 fsf, u32 ft, u32 ftf)
	{
		const Clamp c = Settings();
		const u32 sv = VU0.VF[fs].UL[fsf], tv = VU0.VF[ft].UL[ftf];
		const __m128 s = Scalar(sv), t = Scalar(tv);
		u32 div;
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
	void VSQRT(u32 flags, u32 ft, u32 ftf)
	{
		u32 tv = VU0.VF[ft].UL[ftf];
		u32 div = 0;
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
	void VRSQRT(u32 flags, u32 fs, u32 fsf, u32 ft, u32 ftf)
	{
		const Clamp c = Settings();
		const u32 sv = VU0.VF[fs].UL[fsf];
		u32 tv = VU0.VF[ft].UL[ftf];
		u32 div = 0;
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
	void VWAITQ() {}
	void VNOP() {}

	// ---- the integer registers ----
	// An instruction whose destination is VI00 is left out entirely (isNOP in
	// microVU's analysis), the address registers of VLQI and VLQD excepted.
	void VIADD(u32 id, u32 is, u32 it) { SetVI(id, VI(is) + VI(it)); }
	void VISUB(u32 id, u32 is, u32 it) { SetVI(id, VI(is) - VI(it)); }
	void VIAND(u32 id, u32 is, u32 it) { SetVI(id, VI(is) & VI(it)); }
	void VIOR(u32 id, u32 is, u32 it) { SetVI(id, VI(is) | VI(it)); }

	// mVU_IADDI: with PCSX2's I-bit game fix the immediate comes from the
	// instruction word at the micro program counter, which in macro mode is 0:
	// the first word of VU0's micro memory.
	void VIADDI(u32 it, u32 is, int32_t imm5)
	{
		if (EmuConfig.Gamefixes.IbitHack) [[unlikely]]
		{
			u32 word;
			std::memcpy(&word, VU0.Micro, sizeof(word));
			imm5 = static_cast<int32_t>(word << 21) >> 27;
		}
		SetVI(it, VI(is) + static_cast<u32>(imm5));
	}

	// mVU_MTIR: the low 16 bits of fs.fsf.
	void VMTIR(u32 it, u32 fs, u32 fsf) { SetVI(it, VU0.VF[fs].UL[fsf]); }

	// mVU_MFIR: is, sign-extended, in every component of dest.
	void VMFIR(u32 dest, u32 ft, u32 is)
	{
		WriteVF(ft, dest, Broadcast(static_cast<u32>(static_cast<int32_t>(static_cast<int16_t>(VI(is))))));
	}

	// mVU_LQI: load from the address in is, sign-extended, then add one to is.
	void VLQI(u32 dest, u32 ft, u32 is)
	{
		is &= 15;
		u8* p = VU0.Mem;
		if (is)
		{
			const u32 a = VI(is);
			SetVI(is, a + 1);
			p = Address(static_cast<u32>(static_cast<int32_t>(static_cast<int16_t>(a))));
		}
		else if (ft == 0)
			return;
		WriteVF(ft, dest, LoadQuad(p));
	}

	// mVU_LQD: subtract one from is, then load from it. With VI00 the address
	// is VI00 minus one, which reaches VU1's registers.
	void VLQD(u32 dest, u32 ft, u32 is)
	{
		is &= 15;
		if (is == 0 && ft == 0)
			return;
		const u32 a = VI(is) - 1;
		SetVI(is, a);
		WriteVF(ft, dest, LoadQuad(Address(static_cast<u32>(static_cast<int32_t>(static_cast<int16_t>(a))))));
	}

	// mVU_SQI: store to the address in it, then add one to it.
	void VSQI(u32 dest, u32 fs, u32 it)
	{
		it &= 15;
		u8* p = VU0.Mem;
		if (it)
		{
			const u32 a = VI(it);
			SetVI(it, a + 1);
			p = Address(a);
		}
		StoreVF(p, dest, fs);
	}

	// mVU_SQD: subtract one from it, then store to it; with VI00, as VLQD.
	void VSQD(u32 dest, u32 fs, u32 it)
	{
		const u32 a = (VI(it) - 1) & 0xFFFFu;
		SetVI(it, a);
		StoreVF(Address(a), dest, fs);
	}

	// mVU_ILWR: the low 16 bits of the first component of dest at the address
	// in is.
	void VILWR(u32 dest, u32 it, u32 is)
	{
		if ((it & 15) == 0)
			return;
		const u8* p = (is & 15) ? Address(VI(is)) : VU0.Mem;
		const u32 offset = (dest & 8) ? 0 : ((dest & 4) ? 4 : ((dest & 2) ? 8 : 12));
		u16 value;
		std::memcpy(&value, p + offset, sizeof(value));
		SetVI(it, value);
	}

	// mVU_ISWR: it, zero-extended, to every component of dest at the address
	// in is.
	void VISWR(u32 dest, u32 it, u32 is)
	{
		u8* p = (is & 15) ? Address(VI(is)) : VU0.Mem;
		StoreQuad(p, dest, Broadcast(VI(it)));
	}

	// ---- R ----
	// mVU_RINIT: R takes fs.fsf's 23 low bits under the exponent of 1.0.
	void VRINIT(u32 fs, u32 fsf)
	{
		const u32 bits = (fs || fsf == 3) ? VU0.VF[fs].UL[fsf] : 0;
		VU0.VI[REG_R].UL = (bits & 0x7FFFFFu) | 0x3F800000u;
	}

	void VRXOR(u32 fs, u32 fsf)
	{
		if (fs || fsf == 3)
			VU0.VI[REG_R].UL ^= VU0.VF[fs].UL[fsf] & 0x7FFFFFu;
	}

	void VRGET(u32 dest, u32 ft) { WriteVF(ft, dest, Broadcast(VU0.VI[REG_R].UL)); }

	// mVU_RNEXT: R steps, then is copied as by VRGET.
	void VRNEXT(u32 dest, u32 ft)
	{
		u32 r = VU0.VI[REG_R].UL;
		const u32 feedback = ((r >> 4) & 1) ^ ((r >> 22) & 1);
		r = (((r << 1) ^ feedback) & 0x7FFFFFu) | 0x3F800000u;
		VU0.VI[REG_R].UL = r;
		WriteVF(ft, dest, Broadcast(r));
	}
} // namespace ps2n::ee::ops
