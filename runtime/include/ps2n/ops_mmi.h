// SPDX-License-Identifier: GPL-3.0-or-later
//
// R5900 multimedia (MMI) instructions: 128-bit parallel arithmetic on the
// general-purpose registers, and on HI and LO.
//
// Plain loops over lanes copied out with memcpy; compilers turn these into
// host SIMD, so no intrinsics are needed. Lane 0 is the least significant;
// "word i" of a register is bits 32*i to 32*i+31.
//
// What each function must agree with is stated in ops_int.h. The host's
// -selftest compares every one with PCSX2's interpreter and recompiler. They
// agree with both, except where those two disagree with each other:
//
//  - PMADDW and PMSUBW compute the plain 64-bit result, as PCSX2's recompiler
//    does. Its interpreter adds corrections to the high word that the console
//    recording contradicts (ps2autotests cpu/ee_simd/muldiv).
//  - PMADDUW with $zero as an operand adds a product of zero, as PCSX2's
//    interpreter does. Its recompiler multiplies two host registers it never
//    loaded.
#pragma once

#include "ps2n/ee_state.h"
#include "ps2n/ops_int.h"

#include <emmintrin.h>
#include <stdint.h>
#include <tmmintrin.h>

namespace ps2n::ee::ops
{
	template <typename T>
	struct Lanes {
		static constexpr int N = 16 / static_cast<int>(sizeof(T));
		T v[16 / sizeof(T)];
	};

	template <typename T>
	inline Lanes<T> gl(uint32_t r) {
		Lanes<T> q;
		__builtin_memcpy(q.v, cpuRegs.GPR.r[r].UD, 16);
		return q;
	}
	template <typename T>
	inline Lanes<T> hiLanes() {
		Lanes<T> q;
		__builtin_memcpy(q.v, cpuRegs.HI.UD, 16);
		return q;
	}
	template <typename T>
	inline Lanes<T> loLanes() {
		Lanes<T> q;
		__builtin_memcpy(q.v, cpuRegs.LO.UD, 16);
		return q;
	}
	template <typename T>
	inline void sl(uint32_t rd, const Lanes<T>& q) {
		if (rd) __builtin_memcpy(cpuRegs.GPR.r[rd].UD, q.v, 16);
	}

	// A register's 128 bits, and HI's and LO's, as one SSE value; and a result
	// written to rd whole. The instructions that only move lanes about are
	// written with these: compilers turned their loops into partial loads and
	// stores of the registers (2026-11-20).
	inline __m128i gq(uint32_t r) { return _mm_loadu_si128(reinterpret_cast<const __m128i*>(cpuRegs.GPR.r[r].UD)); }
	inline __m128i hiq() { return _mm_loadu_si128(reinterpret_cast<const __m128i*>(cpuRegs.HI.UD)); }
	inline __m128i loq() { return _mm_loadu_si128(reinterpret_cast<const __m128i*>(cpuRegs.LO.UD)); }
	inline void sq(uint32_t rd, __m128i v) {
		if (rd) _mm_storeu_si128(reinterpret_cast<__m128i*>(cpuRegs.GPR.r[rd].UD), v);
	}

	template <typename T, typename F>
	inline void lanewise(uint32_t rd, uint32_t rs, uint32_t rt, F f) {
		const auto a = gl<T>(rs), b = gl<T>(rt);
		Lanes<T> r;
		for (int i = 0; i < Lanes<T>::N; ++i) r.v[i] = static_cast<T>(f(a.v[i], b.v[i]));
		sl(rd, r);
	}
	template <typename T, typename F>
	inline void lanewise1(uint32_t rd, uint32_t rt, F f) {
		const auto a = gl<T>(rt);
		Lanes<T> r;
		for (int i = 0; i < Lanes<T>::N; ++i) r.v[i] = static_cast<T>(f(a.v[i]));
		sl(rd, r);
	}

	// The smallest and largest value of a lane's type.
	template <typename T>
	struct Limits;
	template <> struct Limits<int8_t> { static constexpr int8_t min = -128, max = 127; };
	template <> struct Limits<int16_t> { static constexpr int16_t min = -32768, max = 32767; };
	template <> struct Limits<int32_t> { static constexpr int32_t min = -2147483647 - 1, max = 2147483647; };
	template <> struct Limits<uint8_t> { static constexpr uint8_t min = 0, max = 0xFF; };
	template <> struct Limits<uint16_t> { static constexpr uint16_t min = 0, max = 0xFFFF; };
	template <> struct Limits<uint32_t> { static constexpr uint32_t min = 0, max = 0xFFFFFFFFu; };

	template <typename T>
	inline T saturate(int64_t v) {
		if (v > static_cast<int64_t>(Limits<T>::max)) return Limits<T>::max;
		if (v < static_cast<int64_t>(Limits<T>::min)) return Limits<T>::min;
		return static_cast<T>(v);
	}

	// ---- add / subtract ---------------------------------------------------------
	inline void PADDW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint32_t>(rd, rs, rt, [](uint32_t a, uint32_t b) { return a + b; }); }
	inline void PSUBW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint32_t>(rd, rs, rt, [](uint32_t a, uint32_t b) { return a - b; }); }
	inline void PADDH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint16_t>(rd, rs, rt, [](uint16_t a, uint16_t b) { return a + b; }); }
	inline void PSUBH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint16_t>(rd, rs, rt, [](uint16_t a, uint16_t b) { return a - b; }); }
	inline void PADDB(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint8_t>(rd, rs, rt, [](uint8_t a, uint8_t b) { return a + b; }); }
	inline void PSUBB(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint8_t>(rd, rs, rt, [](uint8_t a, uint8_t b) { return a - b; }); }

	inline void PADDSW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int32_t>(rd, rs, rt, [](int32_t a, int32_t b) { return saturate<int32_t>(int64_t(a) + b); }); }
	inline void PSUBSW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int32_t>(rd, rs, rt, [](int32_t a, int32_t b) { return saturate<int32_t>(int64_t(a) - b); }); }
	inline void PADDSH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int16_t>(rd, rs, rt, [](int16_t a, int16_t b) { return saturate<int16_t>(int64_t(a) + b); }); }
	inline void PSUBSH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int16_t>(rd, rs, rt, [](int16_t a, int16_t b) { return saturate<int16_t>(int64_t(a) - b); }); }
	inline void PADDSB(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int8_t>(rd, rs, rt, [](int8_t a, int8_t b) { return saturate<int8_t>(int64_t(a) + b); }); }
	inline void PSUBSB(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int8_t>(rd, rs, rt, [](int8_t a, int8_t b) { return saturate<int8_t>(int64_t(a) - b); }); }

	inline void PADDUW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint32_t>(rd, rs, rt, [](uint32_t a, uint32_t b) { return saturate<uint32_t>(int64_t(a) + b); }); }
	inline void PSUBUW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint32_t>(rd, rs, rt, [](uint32_t a, uint32_t b) { return saturate<uint32_t>(int64_t(a) - b); }); }
	inline void PADDUH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint16_t>(rd, rs, rt, [](uint16_t a, uint16_t b) { return saturate<uint16_t>(int64_t(a) + b); }); }
	inline void PSUBUH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint16_t>(rd, rs, rt, [](uint16_t a, uint16_t b) { return saturate<uint16_t>(int64_t(a) - b); }); }
	inline void PADDUB(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint8_t>(rd, rs, rt, [](uint8_t a, uint8_t b) { return saturate<uint8_t>(int64_t(a) + b); }); }
	inline void PSUBUB(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint8_t>(rd, rs, rt, [](uint8_t a, uint8_t b) { return saturate<uint8_t>(int64_t(a) - b); }); }

	// Halfwords 0-3 subtract, 4-7 add.
	inline void PADSBH(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto a = gl<uint16_t>(rs), b = gl<uint16_t>(rt);
		Lanes<uint16_t> r;
		for (int i = 0; i < 4; ++i) r.v[i] = static_cast<uint16_t>(a.v[i] - b.v[i]);
		for (int i = 4; i < 8; ++i) r.v[i] = static_cast<uint16_t>(a.v[i] + b.v[i]);
		sl(rd, r);
	}

	// ---- compare / min / max / abs ----------------------------------------------
	inline void PCGTW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int32_t>(rd, rs, rt, [](int32_t a, int32_t b) { return a > b ? -1 : 0; }); }
	inline void PCGTH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int16_t>(rd, rs, rt, [](int16_t a, int16_t b) { return a > b ? -1 : 0; }); }
	inline void PCGTB(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int8_t>(rd, rs, rt, [](int8_t a, int8_t b) { return a > b ? -1 : 0; }); }
	inline void PCEQW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint32_t>(rd, rs, rt, [](uint32_t a, uint32_t b) { return a == b ? 0xFFFFFFFFu : 0u; }); }
	inline void PCEQH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint16_t>(rd, rs, rt, [](uint16_t a, uint16_t b) { return a == b ? 0xFFFF : 0; }); }
	inline void PCEQB(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<uint8_t>(rd, rs, rt, [](uint8_t a, uint8_t b) { return a == b ? 0xFF : 0; }); }
	inline void PMAXW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int32_t>(rd, rs, rt, [](int32_t a, int32_t b) { return a > b ? a : b; }); }
	inline void PMINW(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int32_t>(rd, rs, rt, [](int32_t a, int32_t b) { return a < b ? a : b; }); }
	inline void PMAXH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int16_t>(rd, rs, rt, [](int16_t a, int16_t b) { return a > b ? a : b; }); }
	inline void PMINH(uint32_t rd, uint32_t rs, uint32_t rt) { lanewise<int16_t>(rd, rs, rt, [](int16_t a, int16_t b) { return a < b ? a : b; }); }
	inline void PABSW(uint32_t rd, uint32_t rt) {
		lanewise1<int32_t>(rd, rt, [](int32_t a) { return a == INT32_MIN ? INT32_MAX : (a < 0 ? -a : a); });
	}
	inline void PABSH(uint32_t rd, uint32_t rt) {
		lanewise1<int16_t>(rd, rt, [](int16_t a) { return a == INT16_MIN ? INT16_MAX : static_cast<int16_t>(a < 0 ? -a : a); });
	}

	// ---- logical ----------------------------------------------------------------
	// Whole: written as two halves, a register read whole right after (SQ, the
	// next of these) waited for the two stores to reach memory.
	inline void PAND(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_and_si128(gq(rs), gq(rt))); }
	inline void POR(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_or_si128(gq(rs), gq(rt))); }
	inline void PXOR(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_xor_si128(gq(rs), gq(rt))); }
	inline void PNOR(uint32_t rd, uint32_t rs, uint32_t rt) {
		sq(rd, _mm_xor_si128(_mm_or_si128(gq(rs), gq(rt)), _mm_set1_epi32(-1)));
	}

	// ---- shifts -----------------------------------------------------------------
	inline void PSLLH(uint32_t rd, uint32_t rt, uint32_t sa) { lanewise1<uint16_t>(rd, rt, [sa](uint16_t a) { return a << (sa & 15); }); }
	inline void PSRLH(uint32_t rd, uint32_t rt, uint32_t sa) { lanewise1<uint16_t>(rd, rt, [sa](uint16_t a) { return a >> (sa & 15); }); }
	inline void PSRAH(uint32_t rd, uint32_t rt, uint32_t sa) { lanewise1<int16_t>(rd, rt, [sa](int16_t a) { return a >> (sa & 15); }); }
	inline void PSLLW(uint32_t rd, uint32_t rt, uint32_t sa) { lanewise1<uint32_t>(rd, rt, [sa](uint32_t a) { return a << (sa & 31); }); }
	inline void PSRLW(uint32_t rd, uint32_t rt, uint32_t sa) { lanewise1<uint32_t>(rd, rt, [sa](uint32_t a) { return a >> (sa & 31); }); }
	inline void PSRAW(uint32_t rd, uint32_t rt, uint32_t sa) { lanewise1<int32_t>(rd, rt, [sa](int32_t a) { return a >> (sa & 31); }); }
	// Variable shifts work on words 0 and 2 and sign-extend into 64-bit lanes.
	inline void PSLLVW(uint32_t rd, uint32_t rt, uint32_t rs) {
		const auto t = gl<uint32_t>(rt), s = gl<uint32_t>(rs);
		set128(rd, sext32(t.v[0] << (s.v[0] & 31)), sext32(t.v[2] << (s.v[2] & 31)));
	}
	inline void PSRLVW(uint32_t rd, uint32_t rt, uint32_t rs) {
		const auto t = gl<uint32_t>(rt), s = gl<uint32_t>(rs);
		set128(rd, sext32(t.v[0] >> (s.v[0] & 31)), sext32(t.v[2] >> (s.v[2] & 31)));
	}
	inline void PSRAVW(uint32_t rd, uint32_t rt, uint32_t rs) {
		const auto t = gl<int32_t>(rt);
		const auto s = gl<uint32_t>(rs);
		set128(rd, static_cast<uint64_t>(static_cast<int64_t>(t.v[0] >> (s.v[0] & 31))),
			   static_cast<uint64_t>(static_cast<int64_t>(t.v[2] >> (s.v[2] & 31))));
	}
	// Funnel shift of rs:rt right by SA bytes (SA set by MTSAB/MTSAH).
	inline void QFSRV(uint32_t rd, uint32_t rs, uint32_t rt) {
		const uint32_t bytes = cpuRegs.sa & 15;
		uint8_t cat[32];
		__builtin_memcpy(cat, cpuRegs.GPR.r[rt].UD, 16);
		__builtin_memcpy(cat + 16, cpuRegs.GPR.r[rs].UD, 16);
		Lanes<uint8_t> r;
		__builtin_memcpy(r.v, cat + bytes, 16);
		sl(rd, r);
	}

	// ---- pack / extend / interleave ---------------------------------------------
	// rd's lanes alternate rt's and rs's, rt's first: the low or the high half
	// of each (PUNPCKL, PUNPCKH).
	inline void PEXTLW(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_unpacklo_epi32(gq(rt), gq(rs))); }
	inline void PEXTUW(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_unpackhi_epi32(gq(rt), gq(rs))); }
	inline void PEXTLH(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_unpacklo_epi16(gq(rt), gq(rs))); }
	inline void PEXTUH(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_unpackhi_epi16(gq(rt), gq(rs))); }
	inline void PEXTLB(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_unpacklo_epi8(gq(rt), gq(rs))); }
	inline void PEXTUB(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_unpackhi_epi8(gq(rt), gq(rs))); }
	inline void PPACW(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<uint32_t>(rs), t = gl<uint32_t>(rt);
		sl(rd, Lanes<uint32_t>{{t.v[0], t.v[2], s.v[0], s.v[2]}});
	}
	inline void PPACH(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<uint16_t>(rs), t = gl<uint16_t>(rt);
		Lanes<uint16_t> r;
		for (int i = 0; i < 4; ++i) {
			r.v[i] = t.v[2 * i];
			r.v[4 + i] = s.v[2 * i];
		}
		sl(rd, r);
	}
	inline void PPACB(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<uint8_t>(rs), t = gl<uint8_t>(rt);
		Lanes<uint8_t> r;
		for (int i = 0; i < 8; ++i) {
			r.v[i] = t.v[2 * i];
			r.v[8 + i] = s.v[2 * i];
		}
		sl(rd, r);
	}
	// 1:5:5:5 halfword pixels <-> 8:8:8:8 words.
	inline void PEXT5(uint32_t rd, uint32_t rt) {
		lanewise1<uint32_t>(rd, rt, [](uint32_t x) {
			return ((x & 0x001F) << 3) | ((x & 0x03E0) << 6) | ((x & 0x7C00) << 9) | ((x & 0x8000) << 16);
		});
	}
	inline void PPAC5(uint32_t rd, uint32_t rt) {
		lanewise1<uint32_t>(rd, rt, [](uint32_t x) {
			return ((x >> 3) & 0x001F) | ((x >> 6) & 0x03E0) | ((x >> 9) & 0x7C00) | ((x >> 16) & 0x8000);
		});
	}
	inline void PINTH(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<uint16_t>(rs), t = gl<uint16_t>(rt);
		sl(rd, Lanes<uint16_t>{{t.v[0], s.v[4], t.v[1], s.v[5], t.v[2], s.v[6], t.v[3], s.v[7]}});
	}
	inline void PINTEH(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<uint16_t>(rs), t = gl<uint16_t>(rt);
		sl(rd, Lanes<uint16_t>{{t.v[0], s.v[0], t.v[2], s.v[2], t.v[4], s.v[4], t.v[6], s.v[6]}});
	}
	inline void PCPYLD(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_unpacklo_epi64(gq(rt), gq(rs))); }
	inline void PCPYUD(uint32_t rd, uint32_t rs, uint32_t rt) { sq(rd, _mm_unpackhi_epi64(gq(rs), gq(rt))); }
	inline void PCPYH(uint32_t rd, uint32_t rt) {
		const auto t = gl<uint16_t>(rt);
		sl(rd, Lanes<uint16_t>{{t.v[0], t.v[0], t.v[0], t.v[0], t.v[4], t.v[4], t.v[4], t.v[4]}});
	}
	inline void PEXEH(uint32_t rd, uint32_t rt) {
		const auto t = gl<uint16_t>(rt);
		sl(rd, Lanes<uint16_t>{{t.v[2], t.v[1], t.v[0], t.v[3], t.v[6], t.v[5], t.v[4], t.v[7]}});
	}
	inline void PREVH(uint32_t rd, uint32_t rt) {
		const auto t = gl<uint16_t>(rt);
		sl(rd, Lanes<uint16_t>{{t.v[3], t.v[2], t.v[1], t.v[0], t.v[7], t.v[6], t.v[5], t.v[4]}});
	}
	inline void PEXCH(uint32_t rd, uint32_t rt) {
		const auto t = gl<uint16_t>(rt);
		sl(rd, Lanes<uint16_t>{{t.v[0], t.v[2], t.v[1], t.v[3], t.v[4], t.v[6], t.v[5], t.v[7]}});
	}
	inline void PEXEW(uint32_t rd, uint32_t rt) {
		const auto t = gl<uint32_t>(rt);
		sl(rd, Lanes<uint32_t>{{t.v[2], t.v[1], t.v[0], t.v[3]}});
	}
	inline void PROT3W(uint32_t rd, uint32_t rt) {
		const auto t = gl<uint32_t>(rt);
		sl(rd, Lanes<uint32_t>{{t.v[1], t.v[2], t.v[0], t.v[3]}});
	}
	inline void PEXCW(uint32_t rd, uint32_t rt) {
		const auto t = gl<uint32_t>(rt);
		sl(rd, Lanes<uint32_t>{{t.v[0], t.v[2], t.v[1], t.v[3]}});
	}

	// Number of leading bits equal to the sign bit, minus one, for words 0 and 1.
	inline void PLZCW(uint32_t rd, uint32_t rs) {
		if (!rd) return;
		auto count = [](uint32_t x) {
			const uint32_t y = x ^ static_cast<uint32_t>(static_cast<int32_t>(x) >> 31);
			return static_cast<uint32_t>(y == 0 ? 32 : __builtin_clz(y)) - 1; // std::countl_zero
		};
		const auto s = gl<uint32_t>(rs);
		cpuRegs.GPR.r[rd].UD[0] = static_cast<uint64_t>(count(s.v[0])) | (static_cast<uint64_t>(count(s.v[1])) << 32);
	}

	// ---- HI/LO transfers --------------------------------------------------------
	inline void PMFHI(uint32_t rd) { sq(rd, hiq()); }
	inline void PMFLO(uint32_t rd) { sq(rd, loq()); }
	inline void PMTHI(uint32_t rs) {
		cpuRegs.HI.UD[0] = ru64(rs);
		cpuRegs.HI.UD[1] = rhi64(rs);
	}
	inline void PMTLO(uint32_t rs) {
		cpuRegs.LO.UD[0] = ru64(rs);
		cpuRegs.LO.UD[1] = rhi64(rs);
	}
	// LO0 HI0 LO2 HI2, and LO1 HI1 LO3 HI3.
	inline void PMFHL_LW(uint32_t rd) {
		const __m128i l = _mm_shuffle_epi32(loq(), _MM_SHUFFLE(2, 0, 2, 0)), h = _mm_shuffle_epi32(hiq(), _MM_SHUFFLE(2, 0, 2, 0));
		sq(rd, _mm_unpacklo_epi32(l, h));
	}
	inline void PMFHL_UW(uint32_t rd) {
		const __m128i l = _mm_shuffle_epi32(loq(), _MM_SHUFFLE(3, 1, 3, 1)), h = _mm_shuffle_epi32(hiq(), _MM_SHUFFLE(3, 1, 3, 1));
		sq(rd, _mm_unpacklo_epi32(l, h));
	}
	inline void PMFHL_SLW(uint32_t rd) {
		const auto h = hiLanes<uint32_t>(), l = loLanes<uint32_t>();
		auto sat = [](uint32_t hi, uint32_t lo) -> uint64_t {
			const auto v = static_cast<int64_t>((static_cast<uint64_t>(hi) << 32) | lo);
			if (v >= 0x7FFFFFFFLL) return 0x7FFFFFFFull;
			if (v <= -0x80000000LL) return 0xFFFFFFFF80000000ull;
			return sext32(lo);
		};
		set128(rd, sat(h.v[0], l.v[0]), sat(h.v[2], l.v[2]));
	}
	// The low halfword of each word: LO's 0 and 2, HI's 0 and 2, LO's 4 and 6,
	// HI's 4 and 6 (halfword lanes).
	inline void PMFHL_LH(uint32_t rd) {
		const __m128i even = _mm_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1);
		const __m128i l = _mm_shuffle_epi8(loq(), even), h = _mm_shuffle_epi8(hiq(), even);
		sq(rd, _mm_unpacklo_epi32(l, h));
	}
	inline void PMFHL_SH(uint32_t rd) {
		const auto h = hiLanes<int32_t>(), l = loLanes<int32_t>();
		auto s = [](int32_t v) { return static_cast<uint16_t>(saturate<int16_t>(v)); };
		sl(rd, Lanes<uint16_t>{{s(l.v[0]), s(l.v[1]), s(h.v[0]), s(h.v[1]), s(l.v[2]), s(l.v[3]), s(h.v[2]), s(h.v[3])}});
	}
	inline void PMTHL_LW(uint32_t rs) {
		const auto s = gl<uint32_t>(rs);
		auto h = hiLanes<uint32_t>(), l = loLanes<uint32_t>();
		l.v[0] = s.v[0];
		h.v[0] = s.v[1];
		l.v[2] = s.v[2];
		h.v[2] = s.v[3];
		__builtin_memcpy(cpuRegs.HI.UD, h.v, 16);
		__builtin_memcpy(cpuRegs.LO.UD, l.v, 16);
	}

	// ---- parallel multiply / divide on words 0 and 2 ------------------------------
	// Lane k (0/1) of HI/LO and rd receives the result for source word 2k.
	inline void pmulWord(uint32_t rd, int64_t r0, int64_t r1) {
		cpuRegs.LO.UD[0] = sext32(static_cast<uint32_t>(r0));
		cpuRegs.HI.UD[0] = sext32(static_cast<uint32_t>(static_cast<uint64_t>(r0) >> 32));
		cpuRegs.LO.UD[1] = sext32(static_cast<uint32_t>(r1));
		cpuRegs.HI.UD[1] = sext32(static_cast<uint32_t>(static_cast<uint64_t>(r1) >> 32));
		set128(rd, static_cast<uint64_t>(r0), static_cast<uint64_t>(r1));
	}
	inline int64_t hiLoWord(int k) {
		const auto h = hiLanes<uint32_t>(), l = loLanes<uint32_t>();
		return static_cast<int64_t>((static_cast<uint64_t>(h.v[2 * k]) << 32) | l.v[2 * k]);
	}
	inline void PMULTW(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<int32_t>(rs), t = gl<int32_t>(rt);
		pmulWord(rd, int64_t(s.v[0]) * t.v[0], int64_t(s.v[2]) * t.v[2]);
	}
	inline void PMULTUW(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<uint32_t>(rs), t = gl<uint32_t>(rt);
		pmulWord(rd, static_cast<int64_t>(uint64_t(s.v[0]) * t.v[0]), static_cast<int64_t>(uint64_t(s.v[2]) * t.v[2]));
	}
	inline void PMADDW(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<int32_t>(rs), t = gl<int32_t>(rt);
		pmulWord(rd, static_cast<int64_t>(static_cast<uint64_t>(hiLoWord(0)) + static_cast<uint64_t>(int64_t(s.v[0]) * t.v[0])),
				 static_cast<int64_t>(static_cast<uint64_t>(hiLoWord(1)) + static_cast<uint64_t>(int64_t(s.v[2]) * t.v[2])));
	}
	inline void PMADDUW(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<uint32_t>(rs), t = gl<uint32_t>(rt);
		pmulWord(rd, static_cast<int64_t>(static_cast<uint64_t>(hiLoWord(0)) + uint64_t(s.v[0]) * t.v[0]),
				 static_cast<int64_t>(static_cast<uint64_t>(hiLoWord(1)) + uint64_t(s.v[2]) * t.v[2]));
	}
	inline void PMSUBW(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<int32_t>(rs), t = gl<int32_t>(rt);
		pmulWord(rd, static_cast<int64_t>(static_cast<uint64_t>(hiLoWord(0)) - static_cast<uint64_t>(int64_t(s.v[0]) * t.v[0])),
				 static_cast<int64_t>(static_cast<uint64_t>(hiLoWord(1)) - static_cast<uint64_t>(int64_t(s.v[2]) * t.v[2])));
	}
	inline void PDIVW(uint32_t rs, uint32_t rt) {
		const auto s = gl<int32_t>(rs), t = gl<int32_t>(rt);
		signedDiv(0, s.v[0], t.v[0]);
		signedDiv(1, s.v[2], t.v[2]);
	}
	inline void PDIVUW(uint32_t rs, uint32_t rt) {
		const auto s = gl<uint32_t>(rs), t = gl<uint32_t>(rt);
		unsignedDiv(0, s.v[0], t.v[0]);
		unsignedDiv(1, s.v[2], t.v[2]);
	}
	// All four words of rs divided by halfword 0 of rt.
	inline void PDIVBW(uint32_t rs, uint32_t rt) {
		const auto s = gl<int32_t>(rs);
		const auto d = static_cast<int16_t>(ru32(rt));
		Lanes<int32_t> lo, hi;
		for (int i = 0; i < 4; ++i) {
			if (s.v[i] == INT32_MIN && d == -1) {
				lo.v[i] = INT32_MIN;
				hi.v[i] = 0;
			} else if (d != 0) {
				lo.v[i] = s.v[i] / d;
				hi.v[i] = s.v[i] % d;
			} else {
				lo.v[i] = s.v[i] < 0 ? 1 : -1;
				hi.v[i] = s.v[i];
			}
		}
		__builtin_memcpy(cpuRegs.LO.UD, lo.v, 16);
		__builtin_memcpy(cpuRegs.HI.UD, hi.v, 16);
	}

	// ---- parallel halfword multiply ---------------------------------------------
	// Products of halfword pairs land in LO/HI words in the order
	// LO0 LO1 HI0 HI1 LO2 LO3 HI2 HI3; rd gets LO0 HI0 LO2 HI2.
	inline void phResult(uint32_t rd, const int32_t r[8]) {
		const Lanes<int32_t> lo{{r[0], r[1], r[4], r[5]}}, hi{{r[2], r[3], r[6], r[7]}};
		__builtin_memcpy(cpuRegs.LO.UD, lo.v, 16);
		__builtin_memcpy(cpuRegs.HI.UD, hi.v, 16);
		sl(rd, Lanes<int32_t>{{lo.v[0], hi.v[0], lo.v[2], hi.v[2]}});
	}
	inline void phAccumulators(int32_t acc[8]) {
		const auto lo = loLanes<int32_t>(), hi = hiLanes<int32_t>();
		const int32_t a[8] = {lo.v[0], lo.v[1], hi.v[0], hi.v[1], lo.v[2], lo.v[3], hi.v[2], hi.v[3]};
		__builtin_memcpy(acc, a, sizeof(a));
	}
	// PMULTH, PMADDH and PMSUBH with SSE, as PCSX2's recompiler does them: the
	// eight products from the low and high halves of the 16-bit products
	// (PMULLW, PMULHW), then LO and HI and the result read and written whole.
	// The loops above, which compilers vectorised into partial loads of HI and
	// LO just after whole stores of them, were among the hottest instructions
	// of Jak and Daxter's code (2026-11-20). phSse: 0 products, 1 added to LO
	// and HI, 2 taken from them.
	template <int Accumulate>
	inline void phSse(uint32_t rd, uint32_t rs, uint32_t rt) {
		const __m128i s = _mm_loadu_si128(reinterpret_cast<const __m128i*>(cpuRegs.GPR.r[rs].UD));
		const __m128i t = _mm_loadu_si128(reinterpret_cast<const __m128i*>(cpuRegs.GPR.r[rt].UD));
		const __m128i low = _mm_mullo_epi16(s, t), high = _mm_mulhi_epi16(s, t);
		const __m128i p03 = _mm_unpacklo_epi16(low, high); // products 0 to 3
		const __m128i p47 = _mm_unpackhi_epi16(low, high); // products 4 to 7
		__m128i lo = _mm_unpacklo_epi64(p03, p47); // 0 1 4 5: LO's words
		__m128i hi = _mm_unpackhi_epi64(p03, p47); // 2 3 6 7: HI's words
		if constexpr (Accumulate != 0) {
			const __m128i lo_old = _mm_loadu_si128(reinterpret_cast<const __m128i*>(cpuRegs.LO.UD));
			const __m128i hi_old = _mm_loadu_si128(reinterpret_cast<const __m128i*>(cpuRegs.HI.UD));
			lo = Accumulate == 1 ? _mm_add_epi32(lo_old, lo) : _mm_sub_epi32(lo_old, lo);
			hi = Accumulate == 1 ? _mm_add_epi32(hi_old, hi) : _mm_sub_epi32(hi_old, hi);
		}
		_mm_storeu_si128(reinterpret_cast<__m128i*>(cpuRegs.LO.UD), lo);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(cpuRegs.HI.UD), hi);
		if (rd) {
			// rd: LO0 HI0 LO2 HI2.
			const __m128i lo02 = _mm_shuffle_epi32(lo, _MM_SHUFFLE(2, 0, 2, 0));
			const __m128i hi02 = _mm_shuffle_epi32(hi, _MM_SHUFFLE(2, 0, 2, 0));
			_mm_storeu_si128(reinterpret_cast<__m128i*>(cpuRegs.GPR.r[rd].UD), _mm_unpacklo_epi32(lo02, hi02));
		}
	}
	inline void PMULTH(uint32_t rd, uint32_t rs, uint32_t rt) { phSse<0>(rd, rs, rt); }
	inline void PMADDH(uint32_t rd, uint32_t rs, uint32_t rt) { phSse<1>(rd, rs, rt); }
	inline void PMSUBH(uint32_t rd, uint32_t rs, uint32_t rt) { phSse<2>(rd, rs, rt); }
	// Horizontal: each word gets s[2i+1]*t[2i+1] +/- s[2i]*t[2i]; the odd words of
	// HI/LO keep the first product (inverted for PHMSBH), as observed on hardware.
	inline void PHMADH(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<int16_t>(rs), t = gl<int16_t>(rt);
		int32_t r[8];
		for (int i = 0; i < 4; ++i) {
			const auto first = static_cast<uint32_t>(int32_t(s.v[2 * i + 1]) * t.v[2 * i + 1]);
			r[2 * i] = static_cast<int32_t>(first + static_cast<uint32_t>(int32_t(s.v[2 * i]) * t.v[2 * i]));
			r[2 * i + 1] = static_cast<int32_t>(first);
		}
		phResult(rd, r);
	}
	inline void PHMSBH(uint32_t rd, uint32_t rs, uint32_t rt) {
		const auto s = gl<int16_t>(rs), t = gl<int16_t>(rt);
		int32_t r[8];
		for (int i = 0; i < 4; ++i) {
			const auto first = static_cast<uint32_t>(int32_t(s.v[2 * i + 1]) * t.v[2 * i + 1]);
			r[2 * i] = static_cast<int32_t>(first - static_cast<uint32_t>(int32_t(s.v[2 * i]) * t.v[2 * i]));
			r[2 * i + 1] = static_cast<int32_t>(~first);
		}
		phResult(rd, r);
	}
} // namespace ps2n::ee::ops
