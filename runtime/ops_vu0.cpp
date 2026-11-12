// SPDX-License-Identifier: GPL-3.0-or-later
//
// VU0's instructions in EE code: what is out of line. ps2n/ops_vu0.h has the
// instructions themselves, inline, and says what they agree with and where
// that is not the console; the comments here name the part of microVU
// (pcsx2/x86/microVU_*.inl, PCSX2 v2.9.170) that each piece reproduces.

#include "ps2n/ops_vu0.h"

#include "pcsx2/PrecompiledHeader.h"

#include "pcsx2/Common.h"
#include "pcsx2/MTVU.h"
#include "pcsx2/VUmicro.h"

namespace ps2n::ee::vu0
{
	namespace detail
	{
		// The four bits reversed: lanes (lane 0 is x) to flag positions (x is the
		// highest bit).
		constexpr uint8_t kFlip[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};

		// mVUupdateFlags (microVU_Upper.inl): the zero and sign flags of the
		// components of `dest` in `result`, to the registers `flags` says; and
		// with PCSX2's VU overflow game fix, the overflow flags of those whose
		// magnitude is the largest float or more.
		void Flags(__m128 result, uint32_t dest, uint32_t flags)
		{
			const uint32_t sign = kFlip[_mm_movemask_ps(result)] & dest;
			const uint32_t zero = kFlip[_mm_movemask_ps(CMPEQPS(_mm_setzero_ps(), result))] & dest;
			uint32_t mac = (sign << 4) | zero;

			uint32_t overflow = 0;
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
				uint32_t d = Denormalize(VU0.VI[REG_STATUS_FLAG].UL) & 0xFFFC00FFu; // clears O, U, S and Z
				if (overflow)
					d |= 0x820000u; // O and OS
				d |= (mac & 0xFFu) | ((mac & 0xFFu) << 8);
				VU0.VI[REG_STATUS_FLAG].UL = Normalize(d);
			}
		}

		// ADD_SS_TriAceHack: of the two, the one whose exponent is smaller by 25
		// or more is replaced by a zero of its sign, and nothing is clamped.
		__m128 TriAceAdd(__m128 to, __m128 from, uint32_t dest)
		{
			const int lane = dest == 8 ? 0 : (dest == 4 ? 1 : (dest == 2 ? 2 : 3));
			alignas(16) uint32_t a[4], b[4];
			_mm_store_si128(reinterpret_cast<__m128i*>(a), Int(to));
			_mm_store_si128(reinterpret_cast<__m128i*>(b), Int(from));
			uint32_t x = a[lane], y = b[lane];
			const int diff = static_cast<int>((y >> 23) & 0xFF) - static_cast<int>((x >> 23) & 0xFF);
			if (diff <= -25)
				y &= 0x80000000u;
			else if (diff >= 25)
				x &= 0x80000000u;
			return Broadcast(Bits(ADDSS(Scalar(x), Scalar(y))));
		}

		// ---- VU0's memory (mVUaddrFix in microVU_Misc.inl) ---------------------------
		// A quadword address: VU0's 4 KB of data memory, or from 0x400 up VU1's
		// floating-point and integer registers, which wait for VU1's thread first.
		uint8_t* Address(uint32_t a)
		{
			if (a & 0x400)
			{
				if (THREAD_VU1)
					vu1Thread.WaitVU();
				return reinterpret_cast<uint8_t*>(VU1.VF) + (a & 0x3F) * 16;
			}
			return VU0.Mem + (a & 0xFF) * 16;
		}

		// mVUloadReg, mVUsaveReg: the components of `dest`.
		__m128 LoadQuad(const uint8_t* p) { return Float(_mm_load_si128(reinterpret_cast<const __m128i*>(p))); }
		void StoreQuad(uint8_t* p, uint32_t dest, __m128 value)
		{
			__m128i* q = reinterpret_cast<__m128i*>(p);
			_mm_store_si128(q, Merge(_mm_load_si128(q), Int(value), dest));
		}

		// VSQI and VSQD: fs stored. With no component in `dest`, mVUsaveReg's
		// default case stores all four, and the register allocator has read VF00
		// as a cleared register (it does so for a partial write without w).
		void StoreVF(uint8_t* p, uint32_t dest, uint32_t fs)
		{
			if (dest == 0)
				StoreQuad(p, 0xF, fs ? VF(fs) : _mm_setzero_ps());
			else
				StoreQuad(p, dest, VF(fs));
		}
	} // namespace detail

	// mVU_LQI: load from the address in is, sign-extended, then add one to is.
	void VLQI(uint32_t dest, uint32_t ft, uint32_t is)
	{
		is &= 15;
		uint8_t* p = VU0.Mem;
		if (is)
		{
			const uint32_t a = VI(is);
			SetVI(is, a + 1);
			p = Address(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(a))));
		}
		else if (ft == 0)
			return;
		WriteVF(ft, dest, LoadQuad(p));
	}

	// mVU_LQD: subtract one from is, then load from it. With VI00 the address
	// is VI00 minus one, which reaches VU1's registers.
	void VLQD(uint32_t dest, uint32_t ft, uint32_t is)
	{
		is &= 15;
		if (is == 0 && ft == 0)
			return;
		const uint32_t a = VI(is) - 1;
		SetVI(is, a);
		WriteVF(ft, dest, LoadQuad(Address(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(a))))));
	}

	// mVU_SQI: store to the address in it, then add one to it.
	void VSQI(uint32_t dest, uint32_t fs, uint32_t it)
	{
		it &= 15;
		uint8_t* p = VU0.Mem;
		if (it)
		{
			const uint32_t a = VI(it);
			SetVI(it, a + 1);
			p = Address(a);
		}
		StoreVF(p, dest, fs);
	}

	// mVU_SQD: subtract one from it, then store to it; with VI00, as VLQD.
	void VSQD(uint32_t dest, uint32_t fs, uint32_t it)
	{
		const uint32_t a = (VI(it) - 1) & 0xFFFFu;
		SetVI(it, a);
		StoreVF(Address(a), dest, fs);
	}

	// mVU_ILWR: the low 16 bits of the first component of dest at the address
	// in is.
	void VILWR(uint32_t dest, uint32_t it, uint32_t is)
	{
		if ((it & 15) == 0)
			return;
		const uint8_t* p = (is & 15) ? Address(VI(is)) : VU0.Mem;
		const uint32_t offset = (dest & 8) ? 0 : ((dest & 4) ? 4 : ((dest & 2) ? 8 : 12));
		uint16_t value;
		std::memcpy(&value, p + offset, sizeof(value));
		SetVI(it, value);
	}

	// mVU_ISWR: it, zero-extended, to every component of dest at the address
	// in is.
	void VISWR(uint32_t dest, uint32_t it, uint32_t is)
	{
		uint8_t* p = (is & 15) ? Address(VI(is)) : VU0.Mem;
		StoreQuad(p, dest, Broadcast(VI(it)));
	}
} // namespace ps2n::ee::vu0

// The runtime's table: the same functions, out of line.
namespace ps2n::ee::ops
{
	namespace v = ps2n::ee::vu0;

	void VADD(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { v::VADD(flags, dest, fd, fs, ft); }
	void VSUB(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { v::VSUB(flags, dest, fd, fs, ft); }
	void VMUL(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { v::VMUL(flags, dest, fd, fs, ft); }
	void VMADD(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { v::VMADD(flags, dest, fd, fs, ft); }
	void VMSUB(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { v::VMSUB(flags, dest, fd, fs, ft); }
	void VMAX(u32 dest, u32 fd, u32 fs, u32 ft) { v::VMAX(dest, fd, fs, ft); }
	void VMINI(u32 dest, u32 fd, u32 fs, u32 ft) { v::VMINI(dest, fd, fs, ft); }
	void VOPMSUB(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft) { v::VOPMSUB(flags, dest, fd, fs, ft); }

	void VADDbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { v::VADDbc(flags, dest, fd, fs, ft, bc); }
	void VSUBbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { v::VSUBbc(flags, dest, fd, fs, ft, bc); }
	void VMULbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { v::VMULbc(flags, dest, fd, fs, ft, bc); }
	void VMADDbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { v::VMADDbc(flags, dest, fd, fs, ft, bc); }
	void VMSUBbc(u32 flags, u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { v::VMSUBbc(flags, dest, fd, fs, ft, bc); }
	void VMAXbc(u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { v::VMAXbc(dest, fd, fs, ft, bc); }
	void VMINIbc(u32 dest, u32 fd, u32 fs, u32 ft, u32 bc) { v::VMINIbc(dest, fd, fs, ft, bc); }

	void VADDq(u32 flags, u32 dest, u32 fd, u32 fs) { v::VADDq(flags, dest, fd, fs); }
	void VSUBq(u32 flags, u32 dest, u32 fd, u32 fs) { v::VSUBq(flags, dest, fd, fs); }
	void VMULq(u32 flags, u32 dest, u32 fd, u32 fs) { v::VMULq(flags, dest, fd, fs); }
	void VMADDq(u32 flags, u32 dest, u32 fd, u32 fs) { v::VMADDq(flags, dest, fd, fs); }
	void VMSUBq(u32 flags, u32 dest, u32 fd, u32 fs) { v::VMSUBq(flags, dest, fd, fs); }

	void VADDi(u32 flags, u32 dest, u32 fd, u32 fs) { v::VADDi(flags, dest, fd, fs); }
	void VSUBi(u32 flags, u32 dest, u32 fd, u32 fs) { v::VSUBi(flags, dest, fd, fs); }
	void VMULi(u32 flags, u32 dest, u32 fd, u32 fs) { v::VMULi(flags, dest, fd, fs); }
	void VMADDi(u32 flags, u32 dest, u32 fd, u32 fs) { v::VMADDi(flags, dest, fd, fs); }
	void VMSUBi(u32 flags, u32 dest, u32 fd, u32 fs) { v::VMSUBi(flags, dest, fd, fs); }
	void VMAXi(u32 dest, u32 fd, u32 fs) { v::VMAXi(dest, fd, fs); }
	void VMINIi(u32 dest, u32 fd, u32 fs) { v::VMINIi(dest, fd, fs); }

	void VADDA(u32 flags, u32 dest, u32 fs, u32 ft) { v::VADDA(flags, dest, fs, ft); }
	void VSUBA(u32 flags, u32 dest, u32 fs, u32 ft) { v::VSUBA(flags, dest, fs, ft); }
	void VMULA(u32 flags, u32 dest, u32 fs, u32 ft) { v::VMULA(flags, dest, fs, ft); }
	void VMADDA(u32 flags, u32 dest, u32 fs, u32 ft) { v::VMADDA(flags, dest, fs, ft); }
	void VMSUBA(u32 flags, u32 dest, u32 fs, u32 ft) { v::VMSUBA(flags, dest, fs, ft); }
	void VOPMULA(u32 flags, u32 dest, u32 fs, u32 ft) { v::VOPMULA(flags, dest, fs, ft); }

	void VADDAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { v::VADDAbc(flags, dest, fs, ft, bc); }
	void VSUBAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { v::VSUBAbc(flags, dest, fs, ft, bc); }
	void VMULAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { v::VMULAbc(flags, dest, fs, ft, bc); }
	void VMADDAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { v::VMADDAbc(flags, dest, fs, ft, bc); }
	void VMSUBAbc(u32 flags, u32 dest, u32 fs, u32 ft, u32 bc) { v::VMSUBAbc(flags, dest, fs, ft, bc); }

	void VADDAq(u32 flags, u32 dest, u32 fs) { v::VADDAq(flags, dest, fs); }
	void VSUBAq(u32 flags, u32 dest, u32 fs) { v::VSUBAq(flags, dest, fs); }
	void VMULAq(u32 flags, u32 dest, u32 fs) { v::VMULAq(flags, dest, fs); }
	void VMADDAq(u32 flags, u32 dest, u32 fs) { v::VMADDAq(flags, dest, fs); }
	void VMSUBAq(u32 flags, u32 dest, u32 fs) { v::VMSUBAq(flags, dest, fs); }

	void VADDAi(u32 flags, u32 dest, u32 fs) { v::VADDAi(flags, dest, fs); }
	void VSUBAi(u32 flags, u32 dest, u32 fs) { v::VSUBAi(flags, dest, fs); }
	void VMULAi(u32 flags, u32 dest, u32 fs) { v::VMULAi(flags, dest, fs); }
	void VMADDAi(u32 flags, u32 dest, u32 fs) { v::VMADDAi(flags, dest, fs); }
	void VMSUBAi(u32 flags, u32 dest, u32 fs) { v::VMSUBAi(flags, dest, fs); }

	void VABS(u32 dest, u32 ft, u32 fs) { v::VABS(dest, ft, fs); }
	void VMOVE(u32 dest, u32 ft, u32 fs) { v::VMOVE(dest, ft, fs); }
	void VMR32(u32 dest, u32 ft, u32 fs) { v::VMR32(dest, ft, fs); }
	void VFTOI0(u32 dest, u32 ft, u32 fs) { v::VFTOI0(dest, ft, fs); }
	void VFTOI4(u32 dest, u32 ft, u32 fs) { v::VFTOI4(dest, ft, fs); }
	void VFTOI12(u32 dest, u32 ft, u32 fs) { v::VFTOI12(dest, ft, fs); }
	void VFTOI15(u32 dest, u32 ft, u32 fs) { v::VFTOI15(dest, ft, fs); }
	void VITOF0(u32 dest, u32 ft, u32 fs) { v::VITOF0(dest, ft, fs); }
	void VITOF4(u32 dest, u32 ft, u32 fs) { v::VITOF4(dest, ft, fs); }
	void VITOF12(u32 dest, u32 ft, u32 fs) { v::VITOF12(dest, ft, fs); }
	void VITOF15(u32 dest, u32 ft, u32 fs) { v::VITOF15(dest, ft, fs); }
	void VCLIPw(u32 dest, u32 fs, u32 ft) { v::VCLIPw(dest, fs, ft); }

	void VDIV(u32 flags, u32 fs, u32 fsf, u32 ft, u32 ftf) { v::VDIV(flags, fs, fsf, ft, ftf); }
	void VSQRT(u32 flags, u32 ft, u32 ftf) { v::VSQRT(flags, ft, ftf); }
	void VRSQRT(u32 flags, u32 fs, u32 fsf, u32 ft, u32 ftf) { v::VRSQRT(flags, fs, fsf, ft, ftf); }
	void VWAITQ() {}
	void VNOP() {}

	void VIADD(u32 id, u32 is, u32 it) { v::VIADD(id, is, it); }
	void VISUB(u32 id, u32 is, u32 it) { v::VISUB(id, is, it); }
	void VIAND(u32 id, u32 is, u32 it) { v::VIAND(id, is, it); }
	void VIOR(u32 id, u32 is, u32 it) { v::VIOR(id, is, it); }
	void VIADDI(u32 it, u32 is, int32_t imm5) { v::VIADDI(it, is, imm5); }
	void VMTIR(u32 it, u32 fs, u32 fsf) { v::VMTIR(it, fs, fsf); }
	void VMFIR(u32 dest, u32 ft, u32 is) { v::VMFIR(dest, ft, is); }
	void VLQI(u32 dest, u32 ft, u32 is) { v::VLQI(dest, ft, is); }
	void VLQD(u32 dest, u32 ft, u32 is) { v::VLQD(dest, ft, is); }
	void VSQI(u32 dest, u32 fs, u32 it) { v::VSQI(dest, fs, it); }
	void VSQD(u32 dest, u32 fs, u32 it) { v::VSQD(dest, fs, it); }
	void VILWR(u32 dest, u32 it, u32 is) { v::VILWR(dest, it, is); }
	void VISWR(u32 dest, u32 it, u32 is) { v::VISWR(dest, it, is); }
	void VRINIT(u32 fs, u32 fsf) { v::VRINIT(fs, fsf); }
	void VRXOR(u32 fs, u32 fsf) { v::VRXOR(fs, fsf); }
	void VRGET(u32 dest, u32 ft) { v::VRGET(dest, ft); }
	void VRNEXT(u32 dest, u32 ft) { v::VRNEXT(dest, ft); }
} // namespace ps2n::ee::ops
