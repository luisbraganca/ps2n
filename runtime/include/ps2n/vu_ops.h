// SPDX-License-Identifier: GPL-3.0-or-later
//
// The steps of a micro-code block's plan, carried out (ps2n/vu_plan.h,
// ps2n/vu_steps.inc): one function for each step, named as the step, its
// parameters the step's operands. PS2Native's VU interpreter calls them with
// the operands a plan holds (runtime/vu_interp.cpp); code compiled ahead of
// time calls the same functions with the operands as constants
// (ps2n/vu_recomp.h), and what is left of each then is a few host
// instructions. So each instruction is defined once, here, for both.
//
// What they agree with is PCSX2's VU recompiler (microVU) in micro mode, as
// docs/adr/0005 has every family follow PCSX2's recompiler: the host's SSE
// instructions are the ones microVU emits (microVU_Upper.inl,
// microVU_Lower.inl, microVU_Clamp.inl, microVU_Misc.inl; PCSX2 v2.9.170,
// Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+), in the same order on the
// same operands, under the VU's floating-point control register, which
// whoever runs a block has set. The arithmetic is the helpers of
// ps2n/ops_vu0.h, which COP2's instructions in EE code use too.
//
// A step reaches the machine through an accessor, `A`:
//   c                       the Ctx below
//   vf(r), setVf(r, v)      a VF register, whole; vfLane(r, lane) one component's bits
//   acc(), setAcc(v)        ACC
//   vi(r), setVi(r, v)      a VI register (16 bits)
//   h(slot)                 a host register microVU keeps VI registers in (32 bits)
//   stat(i), setStat(i, v)  an instance of the status flag, in microVU's layout
//   mac(i), setMac(i, v)    of the MAC flag;  clip(i), setClip(i, v)  of the clip flag
//   pq(i), setPq(i, v)      Q, its other instance, P, its other instance
//   t0(), ea(), tv(k)       what one step leaves for the next: a value on its way to a
//                           register, the address of a quadword, a VF register set aside
//   base()                  the byte address of the block's first pair
//   branch(), setBranch(v)  what the block's end tests or jumps to (mVU.branch)
//   regs()                  the VU's registers where the machine has them (VURegs), for
//                           the ones no other member is for: I, R, the flags of VURegs
//   micro(), memMask()      the VU's micro memory, as words, and the byte address of its
//                           last pair
//   avx()                   whether PCSX2's emitter writes AVX
//   vu0Mem(), vu1Mem(), vu1Regs()  where the VUs' data memory is, and VU1's registers,
//                           which VU0 reaches as memory
// The interpreter's accessor (Mem) is the machine's memory. Compiled code
// may keep registers in locals of its function between its steps.
#pragma once

#include "ps2n/ops_vu0.h"
#include "ps2n/vu_plan.h"

#include "Config.h"
#include "VU.h"

#include <cstdint>
#include <cstring>
#include <smmintrin.h>

#define PS2N_VU_OP [[gnu::always_inline]] inline

namespace ps2n::vu
{
	struct Link;

	// What a block's code works on beside the VU's registers: microVU's
	// run-time variables, what it keeps in host registers while a program
	// runs (four instances of each flag, the status flag in microVU's layout;
	// P and Q, each with its other instance), and how the run stands.
	struct Ctx
	{
		VURegs* R = nullptr; // this VU's registers
		const uint32_t* micro = nullptr; // its micro memory, as words
		uint32_t memMask = 0; // the byte address of its last pair
		uint32_t avx = 0; // PCSX2's emitter writes AVX: it swaps the sources of some additions and multiplications
		uint32_t base = 0; // the byte address of the running block's first pair
		uint32_t index = 0; // which VU this is

		// The run (ps2n/vu_recomp.h): the cycles the program may still use, and
		// when it has stopped, at which block and why.
		int32_t cycles = 0;
		uint32_t syncHack = 0; // Gamefixes.VUSyncHack: a block is begun only with all of its cycles left, not with one
		uint32_t stop = 0;
		const Link* at = nullptr;
		uint64_t blocks = 0; // blocks begun, for the report
		uint64_t pairs = 0; // their pairs
		uint64_t interpreted = 0; // of the blocks, those carried out step by step

		uint32_t statFlag[4] = {};
		uint32_t macFlag[4] = {};
		uint32_t clipFlag[4] = {};
		uint32_t pq[4] = {};
		uint32_t divFlag = 0;
		uint32_t VIbackup = 0;
		uint32_t VIxgkick = 0;
		uint32_t branch = 0;
		uint32_t badBranch = 0;
		uint32_t evilBranch = 0;
		uint32_t evilevilBranch = 0;

		// The interpreter's: compiled code has these in locals.
		uint32_t h[16] = {};
		uint32_t t0 = 0;
		uint8_t* ea = nullptr;
		__m128 tv[2] = {};
	};

	// The GIF transfer of an XGKICK: PCSX2's own function for it, as microVU
	// calls it (runtime/vu_interp.cpp).
	void Xgkick(uint32_t address);

	// What the analysis and the settings decided for an instruction, in a
	// step's "mode" operand.
	namespace mode
	{
		inline constexpr uint32_t kKindMask = 7; // FMACa: ee::vu0::detail::Kind
		inline constexpr uint32_t kSecondShift = 3; // the second operand: ee::vu0::detail::Second, two bits
		inline constexpr uint32_t kToAcc = 1u << 5; // the result goes to ACC
		inline constexpr uint32_t kClampShift = 6; // the operands clamped first: cFt, cFs, cACC, three bits
		inline constexpr uint32_t kSwap = 1u << 9; // with AVX, the sources of the multiplication (or of the one addition) the other way round
		inline constexpr uint32_t kSwapAdd = 1u << 10; // with AVX, those of the addition that follows a multiplication
		inline constexpr uint32_t kReadQ = 1u << 11; // which instance of Q is read
		inline constexpr uint32_t kOverflow = 1u << 12; // the VU clamp mode: Normal and up
		inline constexpr uint32_t kExtra = 1u << 13; // Extra and up
		inline constexpr uint32_t kSign = 1u << 14; // Extra + Preserve Sign
		inline constexpr uint32_t kAddSubHack = 1u << 15; // PCSX2's VU add/sub game fix
		inline constexpr uint32_t kSubtract = 1u << 16; // FMACb: MSUBA, not MADDA
		inline constexpr uint32_t kWriteQ = 1u << 17; // which instance of Q is written
		inline constexpr uint32_t kWriteP = 1u << 18; // which instance of P
	} // namespace mode

	// Where an instruction's flags go, in a step's "flags" operand.
	namespace flag
	{
		inline constexpr uint32_t kStatus = 1u << 0; // it updates the status flag
		inline constexpr uint32_t kNonSticky = 1u << 1; // and its non-sticky bits
		inline constexpr uint32_t kStatusLastShift = 2; // the instance written before it, two bits
		inline constexpr uint32_t kStatusWriteShift = 4; // the instance it writes
		inline constexpr uint32_t kMac = 1u << 6; // it updates the MAC flag
		inline constexpr uint32_t kMacWriteShift = 7; // the instance it writes
		inline constexpr uint32_t kOverflowHack = 1u << 9; // PCSX2's VU overflow game fix
	} // namespace flag

	// The EFU's instructions, by the step Efu's first operand.
	enum class EfuOp : uint32_t
	{
		Esadd,
		Ersadd,
		Eleng,
		Erleng,
		EatanXY,
		EatanXZ,
		Esum,
		Esqrt,
		Ersqrt,
		Ercpr,
		Esin,
		Eatan,
		Eexp,
	};

	namespace step
	{
		namespace d = ps2n::ee::vu0::detail;

		// The interpreter's accessor: everything where the machine has it.
		struct Mem
		{
			Ctx& c;

			PS2N_VU_OP __m128 vf(uint32_t r) const { return _mm_load_ps(c.R->VF[r].F); }
			PS2N_VU_OP void setVf(uint32_t r, __m128 v) { _mm_store_ps(c.R->VF[r].F, v); }
			PS2N_VU_OP uint32_t vfLane(uint32_t r, uint32_t lane) const { return c.R->VF[r].UL[lane]; }
			PS2N_VU_OP __m128 acc() const { return _mm_load_ps(c.R->ACC.F); }
			PS2N_VU_OP void setAcc(__m128 v) { _mm_store_ps(c.R->ACC.F, v); }
			PS2N_VU_OP uint32_t vi(uint32_t r) const { return c.R->VI[r].US[0]; }
			PS2N_VU_OP void setVi(uint32_t r, uint32_t v) { c.R->VI[r].US[0] = static_cast<u16>(v); }
			PS2N_VU_OP uint32_t& h(uint32_t slot) { return c.h[slot]; }
			PS2N_VU_OP uint32_t stat(uint32_t i) const { return c.statFlag[i]; }
			PS2N_VU_OP void setStat(uint32_t i, uint32_t v) { c.statFlag[i] = v; }
			PS2N_VU_OP uint32_t mac(uint32_t i) const { return c.macFlag[i]; }
			PS2N_VU_OP void setMac(uint32_t i, uint32_t v) { c.macFlag[i] = v; }
			PS2N_VU_OP uint32_t clip(uint32_t i) const { return c.clipFlag[i]; }
			PS2N_VU_OP void setClip(uint32_t i, uint32_t v) { c.clipFlag[i] = v; }
			PS2N_VU_OP uint32_t pq(uint32_t i) const { return c.pq[i]; }
			PS2N_VU_OP void setPq(uint32_t i, uint32_t v) { c.pq[i] = v; }
			PS2N_VU_OP uint32_t& t0() { return c.t0; }
			PS2N_VU_OP uint8_t*& ea() { return c.ea; }
			PS2N_VU_OP __m128& tv(uint32_t k) { return c.tv[k]; }
			PS2N_VU_OP uint32_t base() const { return c.base; }
			PS2N_VU_OP uint32_t branch() const { return c.branch; }
			PS2N_VU_OP void setBranch(uint32_t v) { c.branch = v; }
			PS2N_VU_OP VURegs& regs() const { return *c.R; }
			PS2N_VU_OP const uint32_t* micro() const { return c.micro; }
			PS2N_VU_OP uint32_t memMask() const { return c.memMask; }
			PS2N_VU_OP bool avx() const { return c.avx != 0; }
			PS2N_VU_OP uint8_t* vu0Mem() const { return VU0.Mem; }
			PS2N_VU_OP uint8_t* vu1Mem() const { return VU1.Mem; }
			PS2N_VU_OP uint8_t* vu1Regs() const { return reinterpret_cast<uint8_t*>(VU1.VF); }
			PS2N_VU_OP void finish() const {} // everything is where the machine has it already
		};

		// ---- what the steps share -------------------------------------------------------

		PS2N_VU_OP d::Clamp ClampOf(uint32_t m)
		{
			return {(m & mode::kOverflow) != 0, (m & mode::kExtra) != 0, (m & mode::kSign) != 0};
		}

		// A result written to a VF register: the components of `dest`. VF00 is
		// never written.
		template <class A>
		PS2N_VU_OP void WriteVF(A& a, uint32_t r, uint32_t dest, __m128 v)
		{
			if (r == 0)
				return;
			if (dest == 0xF)
				a.setVf(r, v);
			else
				a.setVf(r, d::Float(d::Merge(d::Int(a.vf(r)), d::Int(v), dest)));
		}
		template <class A>
		PS2N_VU_OP void WriteACC(A& a, uint32_t dest, __m128 v)
		{
			if (dest == 0xF)
				a.setAcc(v);
			else
				a.setAcc(d::Float(d::Merge(d::Int(a.acc()), d::Int(v), dest)));
		}

		// A result in the host register it was given: the VI register that
		// register holds takes its low half.
		template <class A>
		PS2N_VU_OP void Put(A& a, uint32_t h, uint32_t vi, uint32_t value)
		{
			a.h(h) = value;
			if (vi)
				a.setVi(vi, value);
		}

		// mVUaddrFix: where a quadword of data memory is. VU0's addresses from
		// 0x400 up are VU1's registers.
		template <class A>
		PS2N_VU_OP uint8_t* Addr(A& a, uint32_t vu1, uint32_t quadword)
		{
			if (vu1)
				return a.vu1Mem() + ((quadword & 0x3ff) << 4);
			if (quadword & 0x400)
				return a.vu1Regs() + ((quadword & 0x3f) << 4);
			return a.vu0Mem() + ((quadword & 0xff) << 4);
		}

		// A pair's lower word as micro memory holds it now (ptr32[&curI] in
		// microVU's code, with the I-bit game fix).
		template <class A>
		PS2N_VU_OP uint32_t Live(A& a, uint32_t pair)
		{
			return a.micro()[((a.base() + pair * 8) & a.memMask()) >> 2];
		}
		PS2N_VU_OP uint32_t Imm15(uint32_t w) { return ((w >> 10) & 0x7800) | (w & 0x7ff); }
		PS2N_VU_OP uint32_t Imm5(uint32_t w) { return static_cast<uint32_t>(static_cast<int32_t>(w << 21) >> 27); }
		PS2N_VU_OP uint32_t Imm11(uint32_t w) { return static_cast<uint32_t>(static_cast<int32_t>(w << 21) >> 21); }

		// ---- VI registers in host registers ------------------------------------------------

		template <class A>
		PS2N_VU_OP void ViLoad(A& a, uint32_t h, uint32_t vi)
		{
			a.h(h) = a.vi(vi);
		}
		template <class A>
		PS2N_VU_OP void ViZero(A& a, uint32_t h)
		{
			a.h(h) = 0;
		}
		template <class A>
		PS2N_VU_OP void ViCopy(A& a, uint32_t to, uint32_t from)
		{
			a.h(to) = a.h(from);
		}
		template <class A>
		PS2N_VU_OP void ViCopyZ(A& a, uint32_t to, uint32_t from)
		{
			a.h(to) = a.h(from) & 0xffff;
		}
		template <class A>
		PS2N_VU_OP void ViZext(A& a, uint32_t h)
		{
			a.h(h) &= 0xffff;
		}
		template <class A>
		PS2N_VU_OP void ViBackup(A& a, uint32_t h)
		{
			a.c.VIbackup = a.h(h);
		}

		template <class A>
		PS2N_VU_OP void ViSetT(A& a, uint32_t h, uint32_t vi)
		{
			Put(a, h, vi, a.t0());
		}
		template <class A>
		PS2N_VU_OP void ViAddK(A& a, uint32_t h, uint32_t vi, uint32_t k)
		{
			Put(a, h, vi, a.h(h) + k);
		}
		template <class A>
		PS2N_VU_OP void ViAddLive(A& a, uint32_t h, uint32_t vi, uint32_t pair, uint32_t kind)
		{
			const uint32_t w = Live(a, pair);
			Put(a, h, vi, kind == 0 ? a.h(h) + Imm15(w) : kind == 1 ? a.h(h) - Imm15(w) : a.h(h) + Imm5(w));
		}
		template <class A>
		PS2N_VU_OP void ViAlu(A& a, uint32_t hs, uint32_t ht, uint32_t vi, uint32_t fn)
		{
			const uint32_t x = a.h(hs), y = a.h(ht);
			Put(a, hs, vi, fn == 0 ? x + y : fn == 1 ? x - y : fn == 2 ? (x & y) : fn == 3 ? (x | y) : x);
		}
		template <class A>
		PS2N_VU_OP void ViMove(A& a, uint32_t to, uint32_t vi, uint32_t from)
		{
			Put(a, to, vi, a.h(from));
		}
		template <class A>
		PS2N_VU_OP void ViSext(A& a, uint32_t h)
		{
			a.h(h) = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(a.h(h))));
		}

		// ---- a value on its way ------------------------------------------------------------

		template <class A>
		PS2N_VU_OP void T0Const(A& a, uint32_t value)
		{
			a.t0() = value;
		}
		template <class A>
		PS2N_VU_OP void T0Live(A& a, uint32_t pair, uint32_t kind)
		{
			const uint32_t w = Live(a, pair);
			a.t0() = kind == 0 ? Imm15(w) : Imm5(w);
		}
		template <class A>
		PS2N_VU_OP void T0Link(A& a, uint32_t rel)
		{
			a.t0() = ((a.base() + rel) & a.memMask()) >> 3;
		}
		template <class A>
		PS2N_VU_OP void T0LinkEvil(A& a, uint32_t var)
		{
			a.t0() = ((var ? a.c.evilBranch : a.c.badBranch) + 8) >> 3;
		}
		template <class A>
		PS2N_VU_OP void T0VfLane(A& a, uint32_t fs, uint32_t lane)
		{
			a.t0() = a.vfLane(fs, lane);
		}
		template <class A>
		PS2N_VU_OP void T0Vif(A& a, uint32_t which)
		{
			a.t0() = which == 0 ? vif1Regs.top : which == 1 ? (vif0Regs.itop & 0xffu) : (vif1Regs.itop & 0x3ffu);
		}
		template <class A>
		PS2N_VU_OP void T0Rd16(A& a, uint32_t lane)
		{
			uint16_t v;
			std::memcpy(&v, a.ea() + lane * 4, 2);
			a.t0() = v;
		}
		template <class A>
		PS2N_VU_OP void T0Jump(A& a, uint32_t h)
		{
			a.t0() = h == kNone ? 0 : (((a.h(h) & 0xffff) << 3) & a.memMask());
		}
		template <class A>
		PS2N_VU_OP void T0FromH(A& a, uint32_t h)
		{
			a.t0() = h == kNone ? 0 : (a.h(h) & 0xffff);
		}
		template <class A>
		PS2N_VU_OP void T0Backup(A& a)
		{
			a.t0() = a.c.VIbackup;
		}
		template <class A>
		PS2N_VU_OP void T0XorH(A& a, uint32_t h)
		{
			a.t0() ^= a.h(h);
		}
		template <class A>
		PS2N_VU_OP void T0XorBackup(A& a)
		{
			a.t0() ^= a.c.VIbackup;
		}
		template <class A>
		PS2N_VU_OP void T0Fceq(A& a, uint32_t read, uint32_t imm)
		{
			a.t0() = ((a.clip(read) ^ imm) - 1) >> 31;
		}
		template <class A>
		PS2N_VU_OP void T0Fcand(A& a, uint32_t read, uint32_t imm)
		{
			a.t0() = ((a.clip(read) & imm) + 0xffffff) >> 24;
		}
		template <class A>
		PS2N_VU_OP void T0Fcor(A& a, uint32_t read, uint32_t imm)
		{
			a.t0() = ((a.clip(read) | imm) + 1) >> 24;
		}
		template <class A>
		PS2N_VU_OP void T0Fcget(A& a, uint32_t read)
		{
			a.t0() = a.clip(read) & 0xfff;
		}
		// FSEQ: the status flag, its groups set whole, against the immediate in
		// the same layout.
		template <class A>
		PS2N_VU_OP void T0Fseq(A& a, uint32_t read, uint32_t imm)
		{
			uint32_t s = a.stat(read);
			if (s & 0x0f00u)
				s |= 0x0f00u;
			if (s & 0xf000u)
				s |= 0xf000u;
			if (s & 0x000fu)
				s |= 0x000fu;
			if (s & 0x00f0u)
				s |= 0x00f0u;
			a.t0() = ((s ^ imm) - 1) >> 31;
		}
		template <class A>
		PS2N_VU_OP void T0Fsand(A& a, uint32_t read, uint32_t imm12)
		{
			a.t0() = d::Normalize(a.stat(read)) & imm12;
		}
		template <class A>
		PS2N_VU_OP void T0Fsor(A& a, uint32_t read, uint32_t imm12)
		{
			a.t0() = d::Normalize(a.stat(read)) | imm12;
		}

		// ---- the flags ---------------------------------------------------------------------

		template <class A>
		PS2N_VU_OP void Fcset(A& a, uint32_t write, uint32_t imm)
		{
			a.setClip(write, imm);
		}
		template <class A>
		PS2N_VU_OP void Fsset(A& a, uint32_t write, uint32_t last, uint32_t copyFirst, uint32_t imm)
		{
			const uint32_t s = copyFirst ? a.stat(last) : a.stat(write);
			a.setStat(write, (s & 0xfff00u) | imm);
		}
		// FMEQ compares all 32 bits of the host register with the MAC flag.
		template <class A>
		PS2N_VU_OP void Fmop(A& a, uint32_t h, uint32_t vi, uint32_t read, uint32_t which)
		{
			const uint32_t mac = a.mac(read) & 0xffff;
			const uint32_t v = a.h(h);
			Put(a, h, vi, which == 0 ? ((v ^ mac) - 1) >> 31 : which == 1 ? (v & mac) : (v | mac));
		}
		// mVUdivSet: a division's flags reach the status flag.
		template <class A>
		PS2N_VU_OP void DivSet(A& a, uint32_t write, uint32_t last, uint32_t copyFirst)
		{
			const uint32_t s = copyFirst ? a.stat(last) : a.stat(write);
			a.setStat(write, (s & 0xfff3ffffu) | a.c.divFlag);
		}

		// mVUupdateFlags: the zero and sign flags of a result, to the instances
		// of the MAC and status flags the analysis chose.
		template <class A>
		PS2N_VU_OP void UpdateFlags(A& a, __m128 result, uint32_t dest, uint32_t fl)
		{
			static constexpr uint16_t flipMask[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
			const bool doStatus = (fl & flag::kStatus) != 0, doMac = (fl & flag::kMac) != 0;
			if (!doStatus && !doMac)
				return;
			const bool nonSticky = (fl & flag::kNonSticky) != 0;
			const bool single = d::Single(dest);
			__m128 t2 = result;
			if (doMac && !single)
				t2 = d::Float(_mm_shuffle_epi32(d::Int(result), 0x1B));
			uint32_t sReg = 0;
			if (doStatus)
			{
				sReg = a.stat((fl >> flag::kStatusLastShift) & 3);
				if (nonSticky)
					sReg &= 0xfffc00ffu;
			}
			// One component: microVU computes it in the first lane and takes its
			// flags there; here it is in its own lane.
			const int shiftDown = single ? ((dest & 8) ? 0 : ((dest & 4) ? 1 : ((dest & 2) ? 2 : 3))) : 0;
			const uint32_t andXYZW = single ? 1u : (doMac ? dest : flipMask[dest]);
			uint32_t mReg = (static_cast<uint32_t>(_mm_movemask_ps(t2)) >> shiftDown) & andXYZW;
			mReg <<= 4;
			mReg |= (static_cast<uint32_t>(_mm_movemask_ps(d::CMPEQPS(_mm_setzero_ps(), t2))) >> shiftDown) & andXYZW;
			if (doStatus && (fl & flag::kOverflowHack))
			{
				const __m128 mag = _mm_and_ps(t2, d::All(0x7fffffffu));
				const uint32_t big = (static_cast<uint32_t>(_mm_movemask_ps(d::CMPNLTPS(mag, d::All(0x7f7fffffu)))) >> shiftDown) & andXYZW;
				if (big)
				{
					sReg |= 0x820000u;
					if (doMac)
						mReg |= big << 12;
				}
			}
			if (doMac)
			{
				if (single && !(dest & 1))
					mReg <<= (dest & 8) ? 3 : ((dest & 4) ? 2 : 1);
				a.setMac((fl >> flag::kMacWriteShift) & 3, mReg);
			}
			if (doStatus)
			{
				mReg &= 0xff;
				sReg |= mReg;
				if (nonSticky)
					sReg |= mReg << 8;
				a.setStat((fl >> flag::kStatusWriteShift) & 3, sReg);
			}
		}

		// mVUsetupBranch, where a block links on: the flags' instances put in the
		// order the next block expects them in (mVUsetupFlags), and P and Q
		// exchanged with their other instances (shufflePQ). `setup`: 1, 2, 4 the
		// status, MAC and clip flags' instances are put in a new order; 8 Q and
		// its other instance are exchanged; 16 P and its. The orders are in two
		// bits an instance, the instance each of the four takes its value from:
		// the status flag's from bit 8, the MAC flag's from bit 16, the clip
		// flag's from bit 24.
		template <class A>
		PS2N_VU_OP void Setup(A& a, uint32_t setup)
		{
			if (setup & 1)
			{
				const uint32_t v[4] = {a.stat(0), a.stat(1), a.stat(2), a.stat(3)};
				a.setStat(0, v[(setup >> 8) & 3]);
				a.setStat(1, v[(setup >> 10) & 3]);
				a.setStat(2, v[(setup >> 12) & 3]);
				a.setStat(3, v[(setup >> 14) & 3]);
			}
			if (setup & 2)
			{
				const uint32_t v[4] = {a.mac(0), a.mac(1), a.mac(2), a.mac(3)};
				a.setMac(0, v[(setup >> 16) & 3]);
				a.setMac(1, v[(setup >> 18) & 3]);
				a.setMac(2, v[(setup >> 20) & 3]);
				a.setMac(3, v[(setup >> 22) & 3]);
			}
			if (setup & 4)
			{
				const uint32_t v[4] = {a.clip(0), a.clip(1), a.clip(2), a.clip(3)};
				a.setClip(0, v[(setup >> 24) & 3]);
				a.setClip(1, v[(setup >> 26) & 3]);
				a.setClip(2, v[(setup >> 28) & 3]);
				a.setClip(3, v[(setup >> 30) & 3]);
			}
			if (setup & 8)
			{
				const uint32_t q = a.pq(0), other = a.pq(1);
				a.setPq(0, other);
				a.setPq(1, q);
			}
			if (setup & 16)
			{
				const uint32_t p = a.pq(2), other = a.pq(3);
				a.setPq(2, other);
				a.setPq(3, p);
			}
		}

		// ---- branches ----------------------------------------------------------------------

		template <class A>
		PS2N_VU_OP void BranchTo(A& a, uint32_t vars, uint32_t target)
		{
			if (vars & 1)
				a.setBranch(target);
			if (vars & 2)
				a.c.badBranch = target;
			if (vars & 4)
				a.c.evilBranch = target;
			if (vars & 8)
				a.c.evilevilBranch = target;
		}
		template <class A>
		PS2N_VU_OP void BranchT(A& a, uint32_t vars)
		{
			BranchTo(a, vars, a.t0());
		}
		template <class A>
		PS2N_VU_OP void BranchRel(A& a, uint32_t vars, uint32_t rel)
		{
			BranchTo(a, vars, (a.base() + rel) & a.memMask());
		}

		// Whether a conditional branch with this opcode goes, its value compared
		// as 16 bits (condBranch's CMP of mVU.branch).
		PS2N_VU_OP bool Taken(uint32_t opcode, uint32_t value)
		{
			const int16_t v = static_cast<int16_t>(value);
			switch (opcode)
			{
				case 0x28: return v == 0; // IBEQ
				case 0x29: return v != 0; // IBNE
				case 0x2C: return v < 0; // IBLTZ
				case 0x2D: return v > 0; // IBGTZ
				case 0x2E: return v <= 0; // IBLEZ
				case 0x2F: return v >= 0; // IBGEZ
			}
			return false;
		}

		// A conditional branch's value, for the block's end to test (kind 0); or,
		// where a branch is in another's delay slot (condEvilBranch), where each
		// of the two goes.
		template <class A>
		PS2N_VU_OP void CondSet(A& a, uint32_t kind, uint32_t opcode, uint32_t relTaken, uint32_t relNot)
		{
			const uint32_t v = a.t0();
			if (kind == 0)
			{
				a.setBranch(v);
				return;
			}
			const uint32_t target = (a.base() + relTaken) & a.memMask();
			const bool taken = Taken(opcode, v);
			if (kind == 1)
			{
				a.setBranch(v);
				a.c.badBranch = taken ? target : ((a.base() + relNot) & a.memMask());
			}
			else if (kind == 2)
			{
				a.c.evilevilBranch = taken ? target : a.c.evilBranch + 8;
			}
			else
			{
				a.c.evilBranch = taken ? target : a.c.badBranch + 8;
			}
		}

		// ---- data memory -------------------------------------------------------------------

		template <class A>
		PS2N_VU_OP void Ea(A& a, uint32_t h, uint32_t vu1, uint32_t live, uint32_t imm, uint32_t pair)
		{
			const uint32_t base = h == kNone ? 0 : (a.h(h) & 0xffff);
			const uint32_t offset = live ? Imm11(Live(a, pair)) : imm;
			a.ea() = Addr(a, vu1, base + offset);
		}
		template <class A>
		PS2N_VU_OP void EaH(A& a, uint32_t h, uint32_t vu1, uint32_t signExtended)
		{
			const uint32_t v = a.h(h);
			a.ea() = Addr(a, vu1, signExtended ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v))) : (v & 0xffff));
		}
		template <class A>
		PS2N_VU_OP void EaZero(A& a, uint32_t vu1)
		{
			a.ea() = Addr(a, vu1, 0);
		}
		template <class A>
		PS2N_VU_OP void EaFixed(A& a)
		{
			a.ea() = a.vu1Mem() + 0x3ff8;
		}
		template <class A>
		PS2N_VU_OP void LoadQ(A& a, uint32_t ft, uint32_t dest)
		{
			WriteVF(a, ft, dest, d::Float(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a.ea()))));
		}
		template <class A>
		PS2N_VU_OP void StoreQ(A& a, uint32_t fs, uint32_t dest)
		{
			uint8_t* p = a.ea();
			if (dest == 0xF)
			{
				_mm_storeu_si128(reinterpret_cast<__m128i*>(p), d::Int(a.vf(fs)));
				return;
			}
			alignas(16) uint32_t v[4];
			_mm_store_si128(reinterpret_cast<__m128i*>(v), d::Int(a.vf(fs)));
			if (dest & 8)
				std::memcpy(p + 0, &v[0], 4);
			if (dest & 4)
				std::memcpy(p + 4, &v[1], 4);
			if (dest & 2)
				std::memcpy(p + 8, &v[2], 4);
			if (dest & 1)
				std::memcpy(p + 12, &v[3], 4);
		}
		// ISW and ISWR store all 32 bits of the host register.
		template <class A>
		PS2N_VU_OP void Wr32(A& a, uint32_t h, uint32_t dest)
		{
			uint8_t* p = a.ea();
			const uint32_t v = a.h(h);
			if (dest & 8)
				std::memcpy(p + 0, &v, 4);
			if (dest & 4)
				std::memcpy(p + 4, &v, 4);
			if (dest & 2)
				std::memcpy(p + 8, &v, 4);
			if (dest & 1)
				std::memcpy(p + 12, &v, 4);
		}

		// ---- the upper instructions (microVU_Upper.inl) --------------------------------------

		struct Operand
		{
			__m128 v;
			bool clampable;
		};

		// The second operand: opCase1 to opCase4 of setupFtReg. I is loaded as a
		// register written as VF00's, which checkVFClamp clamps.
		template <class A>
		PS2N_VU_OP Operand Fetch(A& a, d::Second k, uint32_t ft, uint32_t bc, uint32_t m)
		{
			switch (k)
			{
				case d::Second::Vector: return {a.vf(ft), ft != 0};
				case d::Second::Bc: return {d::All(a.vfLane(ft, bc)), true};
				case d::Second::I: return {d::All(a.regs().VI[REG_I].UL), true};
				case d::Second::Q: break;
			}
			return {d::All(a.pq((m & mode::kReadQ) ? 1 : 0)), true};
		}

		// mVU_FMACa: ADD, SUB, MUL, MAX, MINI, and the first three into ACC.
		template <class A>
		PS2N_VU_OP void FMACa(A& a, uint32_t code, uint32_t m, uint32_t fl)
		{
			const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31, bc = code & 3;
			const d::Kind kind = static_cast<d::Kind>(m & mode::kKindMask);
			const d::Second k = static_cast<d::Second>((m >> mode::kSecondShift) & 3);
			const bool toAcc = (m & mode::kToAcc) != 0;
			const uint32_t clamp = (m >> mode::kClampShift) & 7;
			if (kind == d::Kind::Sub && k == d::Second::Vector && fs == ft) // doSafeSub
			{
				const __m128 zero = _mm_setzero_ps();
				if (toAcc)
					WriteACC(a, dest, zero);
				else
					WriteVF(a, fd, dest, zero);
				UpdateFlags(a, zero, dest, fl);
				return;
			}
			const d::Clamp c = ClampOf(m);
			const bool swap = (m & mode::kSwap) && a.avx();
			Operand t = Fetch(a, k, ft, bc, m);
			__m128 s = a.vf(fs);
			const bool sClampable = fs != 0;
			if (clamp & d::cFt)
				t.v = d::Clamp2(t.v, c, t.clampable, false);
			if (clamp & d::cFs)
				s = d::Clamp2(s, c, sClampable, false);
			__m128 r;
			switch (kind)
			{
				case d::Kind::Add: r = d::Compute(d::Arith::Add, s, sClampable, t.v, t.clampable, c, swap); break;
				case d::Kind::Sub: r = d::Compute(d::Arith::Sub, s, sClampable, t.v, t.clampable, c); break;
				case d::Kind::Mul: r = d::Compute(d::Arith::Mul, s, sClampable, t.v, t.clampable, c, swap); break;
				case d::Kind::Max: r = d::MinMax(s, t.v, false); break;
				case d::Kind::Min: r = d::MinMax(s, t.v, true); break;
				default:
					if (d::Single(dest) && (m & mode::kAddSubHack)) [[unlikely]]
						r = d::TriAceAdd(s, t.v, dest);
					else
						r = d::Compute(d::Arith::Add, s, sClampable, t.v, t.clampable, c, swap);
					break;
			}
			if (toAcc)
				WriteACC(a, dest, r);
			else
				WriteVF(a, fd, dest, r);
			if (kind != d::Kind::Max && kind != d::Kind::Min)
				UpdateFlags(a, r, dest, fl);
		}

		// mVU_FMACb: MADDA and MSUBA. ACC plus or minus the product.
		template <class A>
		PS2N_VU_OP void FMACb(A& a, uint32_t code, uint32_t m, uint32_t fl)
		{
			const uint32_t dest = (code >> 21) & 0xF, fs = (code >> 11) & 31, ft = (code >> 16) & 31, bc = code & 3;
			const d::Second k = static_cast<d::Second>((m >> mode::kSecondShift) & 3);
			const bool subtract = (m & mode::kSubtract) != 0;
			const uint32_t clamp = (m >> mode::kClampShift) & 7;
			const d::Clamp c = ClampOf(m);
			const bool swapMul = (m & mode::kSwap) && a.avx(), swapAdd = (m & mode::kSwapAdd) && a.avx();
			Operand t = Fetch(a, k, ft, bc, m);
			__m128 s = a.vf(fs);
			const bool sClampable = fs != 0;
			if (clamp & d::cFt)
				t.v = d::Clamp2(t.v, c, t.clampable, false);
			if (clamp & d::cFs)
				s = d::Clamp2(s, c, sClampable, false);
			const __m128 product = d::Compute(d::Arith::Mul, s, sClampable, t.v, t.clampable, c, swapMul);
			const __m128 r = d::Compute(subtract ? d::Arith::Sub : d::Arith::Add, a.acc(), true, product, sClampable, c, swapAdd);
			WriteACC(a, dest, r);
			UpdateFlags(a, r, dest, fl);
		}

		// mVU_FMACc: MADD. The product plus ACC.
		template <class A>
		PS2N_VU_OP void FMACc(A& a, uint32_t code, uint32_t m, uint32_t fl)
		{
			const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31, bc = code & 3;
			const d::Second k = static_cast<d::Second>((m >> mode::kSecondShift) & 3);
			const uint32_t clamp = (m >> mode::kClampShift) & 7;
			const d::Clamp c = ClampOf(m);
			const bool swapMul = (m & mode::kSwap) && a.avx(), swapAdd = (m & mode::kSwapAdd) && a.avx();
			Operand t = Fetch(a, k, ft, bc, m);
			__m128 acc = a.acc();
			__m128 s = a.vf(fs);
			const bool sClampable = fs != 0;
			if (clamp & d::cFt)
				t.v = d::Clamp2(t.v, c, t.clampable, false);
			if (clamp & d::cFs)
				s = d::Clamp2(s, c, sClampable, false);
			if (clamp & d::cACC)
				acc = d::Clamp2(acc, c, true, false);
			const __m128 product = d::Compute(d::Arith::Mul, s, sClampable, t.v, t.clampable, c, swapMul);
			const __m128 r = d::Compute(d::Arith::Add, product, sClampable, acc, true, c, swapAdd);
			WriteVF(a, fd, dest, r);
			UpdateFlags(a, r, dest, fl);
		}

		// mVU_FMACd: MSUB. ACC minus the product.
		template <class A>
		PS2N_VU_OP void FMACd(A& a, uint32_t code, uint32_t m, uint32_t fl)
		{
			const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31, bc = code & 3;
			const d::Second k = static_cast<d::Second>((m >> mode::kSecondShift) & 3);
			const uint32_t clamp = (m >> mode::kClampShift) & 7;
			const d::Clamp c = ClampOf(m);
			const bool swapMul = (m & mode::kSwap) && a.avx();
			Operand t = Fetch(a, k, ft, bc, m);
			__m128 s = a.vf(fs);
			__m128 acc = a.acc();
			const bool sClampable = fs != 0;
			if (clamp & d::cFt)
				t.v = d::Clamp2(t.v, c, t.clampable, false);
			if (clamp & d::cFs)
				s = d::Clamp2(s, c, sClampable, false);
			if (clamp & d::cACC)
				acc = d::Clamp2(acc, c, true, false);
			const __m128 product = d::Compute(d::Arith::Mul, s, sClampable, t.v, t.clampable, c, swapMul);
			const __m128 r = d::Compute(d::Arith::Sub, acc, true, product, sClampable, c);
			WriteVF(a, fd, dest, r);
			UpdateFlags(a, r, dest, fl);
		}

		// mVU_OPMSUB: fd = ACC - (fs.y ft.z, fs.z ft.x, fs.x ft.y). With a single
		// component, the first lane of the product is what is subtracted.
		template <class A>
		PS2N_VU_OP void Opmsub(A& a, uint32_t code, uint32_t m, uint32_t fl)
		{
			const uint32_t dest = (code >> 21) & 0xF, fd = (code >> 6) & 31, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
			const d::Clamp c = ClampOf(m);
			const bool swap = (m & mode::kSwap) && a.avx();
			const __m128 t = d::Float(_mm_shuffle_epi32(d::Int(a.vf(ft)), 0xD2));
			const __m128 s = d::Float(_mm_shuffle_epi32(d::Int(a.vf(fs)), 0xC9));
			__m128 product = d::Compute(d::Arith::Mul, s, fs != 0, t, ft != 0, c, swap);
			if (d::Single(dest))
				product = d::Float(_mm_shuffle_epi32(d::Int(product), 0x00));
			const __m128 r = d::Compute(d::Arith::Sub, a.acc(), true, product, fs != 0, c);
			WriteVF(a, fd, dest, r);
			UpdateFlags(a, r, dest, fl);
		}

		// mVU_OPMULA: ACC = (fs.y ft.z, fs.z ft.x, fs.x ft.y). (With a single
		// component what microVU gives depends on its allocator: such a block has
		// no plan.)
		template <class A>
		PS2N_VU_OP void Opmula(A& a, uint32_t code, uint32_t m, uint32_t fl)
		{
			const uint32_t dest = (code >> 21) & 0xF, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
			const d::Clamp c = ClampOf(m);
			const bool swap = (m & mode::kSwap) && a.avx();
			__m128 s, t;
			if (d::Single(dest))
			{
				s = _mm_setzero_ps();
				t = _mm_setzero_ps();
			}
			else
			{
				s = d::Float(_mm_shuffle_epi32(d::Int(a.vf(fs)), 0xC9));
				t = d::Float(_mm_shuffle_epi32(d::Int(a.vf(ft)), 0xD2));
			}
			const __m128 r = d::Compute(d::Arith::Mul, s, fs != 0, t, ft != 0, c, swap);
			WriteACC(a, dest, r);
			UpdateFlags(a, r, dest, fl);
		}

		// mVU_ITOFx, mVU_FTOIx: CVTTPS2DQ gives 0x80000000 for what it cannot
		// represent; a positive value of that kind becomes 0x7FFFFFFF.
		template <class A>
		PS2N_VU_OP void Itof(A& a, uint32_t code, uint32_t scale)
		{
			const uint32_t dest = (code >> 21) & 0xF, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
			__m128 v = d::CVTDQ2PS(d::Int(a.vf(fs)));
			if (scale)
				v = d::MULPS(v, d::All(scale));
			WriteVF(a, ft, dest, v);
		}
		template <class A>
		PS2N_VU_OP void Ftoi(A& a, uint32_t code, uint32_t scale)
		{
			const uint32_t dest = (code >> 21) & 0xF, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
			__m128 v = a.vf(fs);
			if (scale)
				v = d::MULPS(v, d::All(scale));
			const __m128i positiveOverflow = _mm_cmpgt_epi32(d::Int(v), _mm_set1_epi32(0x4EFFFFFF));
			WriteVF(a, ft, dest, d::Float(_mm_xor_si128(d::CVTTPS2DQ(v), positiveOverflow)));
		}
		template <class A>
		PS2N_VU_OP void Abs(A& a, uint32_t code)
		{
			const uint32_t dest = (code >> 21) & 0xF, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
			WriteVF(a, ft, dest, _mm_and_ps(a.vf(fs), d::All(0x7FFFFFFFu)));
		}

		// mVU_CLIP: x, y and z of fs against the magnitude of ft.w, on the bits;
		// a denormal component reads as +0. The clip flag shifts up six.
		template <class A>
		PS2N_VU_OP void Clip(A& a, uint32_t code, uint32_t last, uint32_t write)
		{
			const uint32_t fs = (code >> 11) & 31, ft = (code >> 16) & 31;
			const __m128i fsv = d::Int(a.vf(fs));
			__m128i ftv = _mm_set1_epi32(static_cast<int>(a.vfLane(ft, 3)));
			const uint32_t prev = a.clip(last) << 6;
			const __m128i expo = _mm_and_si128(fsv, _mm_set1_epi32(0x7f800000));
			__m128i t1 = _mm_cmpeq_epi32(expo, _mm_setzero_si128());
			t1 = _mm_andnot_si128(t1, fsv);
			ftv = _mm_and_si128(ftv, _mm_set1_epi32(0x7fffffff));
			const __m128i neg = _mm_xor_si128(t1, _mm_set1_epi32(static_cast<int>(0x80000000u)));
			const __m128i gtPos = _mm_cmpgt_epi32(t1, ftv);
			const __m128i gtNeg = _mm_cmpgt_epi32(neg, ftv);
			__m128i packed = _mm_blend_epi16(gtNeg, gtPos, 0x55);
			packed = _mm_packs_epi16(packed, packed);
			const uint32_t bits = static_cast<uint32_t>(_mm_movemask_epi8(packed)) & 0x3f;
			a.setClip(write, (prev & 0xffffff) | bits);
		}

		// doIbit: I from the lower word of a pair with the I bit; with the I-bit
		// game fix, as micro memory holds it when the pair runs.
		template <class A>
		PS2N_VU_OP void IbitLive(A& a, uint32_t pair)
		{
			a.regs().VI[REG_I].UL = Live(a, pair);
		}
		template <class A>
		PS2N_VU_OP void IbitConst(A& a, uint32_t value)
		{
			a.regs().VI[REG_I].UL = value;
		}

		// ---- the lower instructions on VF registers, Q, P and R (microVU_Lower.inl) -----------

		template <class A>
		PS2N_VU_OP void Move(A& a, uint32_t code)
		{
			const uint32_t dest = (code >> 21) & 0xF, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
			WriteVF(a, ft, dest, a.vf(fs));
		}
		template <class A>
		PS2N_VU_OP void Mr32(A& a, uint32_t code)
		{
			const uint32_t dest = (code >> 21) & 0xF, fs = (code >> 11) & 31, ft = (code >> 16) & 31;
			WriteVF(a, ft, dest, d::Float(_mm_shuffle_epi32(d::Int(a.vf(fs)), 0x39)));
		}

		// DIV, SQRT and RSQRT: Q to its instance now, and the division flags
		// (divFlag), which reach the status flag later (DivSet).
		template <class A>
		PS2N_VU_OP void Div(A& a, uint32_t code, uint32_t m)
		{
			const uint32_t fs = (code >> 11) & 31, ft = (code >> 16) & 31, fsf = (code >> 21) & 3, ftf = (code >> 23) & 3;
			const d::Clamp c = ClampOf(m);
			__m128 s = d::Scalar(a.vfLane(fs, fsf));
			const __m128 t = d::Scalar(a.vfLane(ft, ftf));
			if (d::IsZero(t))
			{
				a.c.divFlag = d::IsZero(s) ? d::kDivI : d::kDivD;
				s = _mm_xor_ps(s, t);
				s = _mm_and_ps(s, d::All(0x80000000u));
				s = _mm_or_ps(s, d::All(0x7f7fffffu));
			}
			else
			{
				a.c.divFlag = 0;
				s = d::Compute(d::Arith::Div, s, fs != 0, t, ft != 0, c);
				s = d::Clamp1(s, c, fs != 0, true);
			}
			a.setPq((m & mode::kWriteQ) ? 1 : 0, d::Bits(s));
		}
		template <class A>
		PS2N_VU_OP void Sqrt(A& a, uint32_t code, uint32_t m)
		{
			const uint32_t ft = (code >> 16) & 31, ftf = (code >> 23) & 3;
			__m128 t = d::Scalar(a.vfLane(ft, ftf));
			a.c.divFlag = 0;
			if (static_cast<int32_t>(d::Bits(t)) < 0) // testNeg: invalid, and the magnitude's root
			{
				a.c.divFlag = d::kDivI;
				t = _mm_and_ps(t, d::All(0x7fffffffu));
			}
			if (m & mode::kOverflow)
				t = d::MINSS(t, d::Scalar(0x7f7fffffu));
			a.setPq((m & mode::kWriteQ) ? 1 : 0, d::Bits(d::SQRTSS(t)));
		}
		template <class A>
		PS2N_VU_OP void Rsqrt(A& a, uint32_t code, uint32_t m)
		{
			const uint32_t fs = (code >> 11) & 31, ft = (code >> 16) & 31, fsf = (code >> 21) & 3, ftf = (code >> 23) & 3;
			const d::Clamp c = ClampOf(m);
			__m128 s = d::Scalar(a.vfLane(fs, fsf));
			__m128 t = d::Scalar(a.vfLane(ft, ftf));
			a.c.divFlag = 0;
			if (static_cast<int32_t>(d::Bits(t)) < 0)
			{
				a.c.divFlag = d::kDivI;
				t = _mm_and_ps(t, d::All(0x7fffffffu));
			}
			t = d::SQRTSS(t);
			if (d::IsZero(t))
			{
				a.c.divFlag = d::IsZero(s) ? d::kDivI : d::kDivD;
				s = _mm_and_ps(s, d::All(0x80000000u));
				s = _mm_or_ps(s, d::All(0x7f7fffffu));
			}
			else
			{
				s = d::Compute(d::Arith::Div, s, fs != 0, t, true, c);
				s = d::Clamp1(s, c, fs != 0, true);
			}
			a.setPq((m & mode::kWriteQ) ? 1 : 0, d::Bits(s));
		}

		// MFIR sign-extends the host register in place, saying nothing.
		template <class A>
		PS2N_VU_OP void Mfir(A& a, uint32_t h, uint32_t code)
		{
			const uint32_t dest = (code >> 21) & 0xF, ft = (code >> 16) & 31;
			int32_t v = 0;
			if (h != kNone)
			{
				ViSext(a, h);
				v = static_cast<int32_t>(a.h(h));
			}
			WriteVF(a, ft, dest, d::Float(_mm_set1_epi32(v)));
		}
		template <class A>
		PS2N_VU_OP void Mfp(A& a, uint32_t code, uint32_t readP)
		{
			const uint32_t dest = (code >> 21) & 0xF, ft = (code >> 16) & 31;
			WriteVF(a, ft, dest, d::All(a.pq(2 + readP)));
		}

		// R: the next of its sequence, then copied as by RGET.
		template <class A>
		PS2N_VU_OP void Rnext(A& a, uint32_t code, uint32_t noWriteVF)
		{
			const uint32_t dest = (code >> 21) & 0xF, ft = (code >> 16) & 31;
			uint32_t r = a.regs().VI[REG_R].UL;
			const uint32_t bit = ((r >> 4) & 1) ^ ((r >> 22) & 1);
			r = (((r << 1) ^ bit) & 0x007fffffu) | 0x3f800000u;
			a.regs().VI[REG_R].UL = r;
			if (!noWriteVF)
				WriteVF(a, ft, dest, d::All(r));
		}
		template <class A>
		PS2N_VU_OP void Rget(A& a, uint32_t code)
		{
			const uint32_t dest = (code >> 21) & 0xF, ft = (code >> 16) & 31;
			WriteVF(a, ft, dest, d::All(a.regs().VI[REG_R].UL));
		}
		template <class A>
		PS2N_VU_OP void Rinit(A& a, uint32_t code)
		{
			const uint32_t fs = (code >> 11) & 31, fsf = (code >> 21) & 3;
			if (fs || fsf == 3)
				a.regs().VI[REG_R].UL = (a.vfLane(fs, fsf) & 0x007fffffu) | 0x3f800000u;
			else
				a.regs().VI[REG_R].UL = 0x3f800000u;
		}
		template <class A>
		PS2N_VU_OP void Rxor(A& a, uint32_t code)
		{
			const uint32_t fs = (code >> 11) & 31, fsf = (code >> 21) & 3;
			if (fs || fsf == 3)
				a.regs().VI[REG_R].UL ^= a.vfLane(fs, fsf) & 0x7fffffu;
		}

		// ---- the EFU's arithmetic ------------------------------------------------------------

		PS2N_VU_OP __m128 DPPS71(__m128 v)
		{
			__asm__("dpps $0x71, %0, %0" : "+x"(v));
			return v;
		}

		// 1 / x: one loaded into its own register, then SSE_DIVSS.
		PS2N_VU_OP __m128 Rcp(__m128 x, const d::Clamp& c)
		{
			return d::Compute(d::Arith::Div, d::Scalar(0x3f800000u), true, x, true, c);
		}

		// mVU_EATAN_: the series of `t` in odd powers, plus pi / 4.
		PS2N_VU_OP __m128 Atan(__m128 t, const d::Clamp& c)
		{
			static constexpr uint32_t kT[7] = {0xbeaaa61cu, 0x3e4c40a6u, 0xbe0e6c63u, 0x3dc577dfu, 0xbd6501c4u, 0x3cb31652u, 0xbb84d7e7u};
			__m128 pq = d::MULPS(t, d::Scalar(0x3f7ffff5u)); // T1
			__m128 t2 = t;
			for (const uint32_t k : kT)
			{
				t2 = d::Compute(d::Arith::Mul, t2, true, t, true, c);
				t2 = d::Compute(d::Arith::Mul, t2, true, t, true, c);
				const __m128 t1 = d::MULPS(t2, d::Scalar(k));
				pq = d::Compute(d::Arith::Add, pq, true, t1, true, c);
			}
			return d::ADDSS(pq, d::Scalar(0x3f490fdbu)); // pi / 4
		}

		// The EFU's instructions: P to its instance now. (VU0 has no EFU: its
		// blocks have no such step.)
		template <class A>
		PS2N_VU_OP void Efu(A& a, uint32_t which, uint32_t code, uint32_t m)
		{
			const uint32_t fs = (code >> 11) & 31, fsf = (code >> 21) & 3;
			const d::Clamp c = ClampOf(m);
			const bool fsClampable = fs != 0;
			__m128 p;
			switch (static_cast<EfuOp>(which))
			{
				case EfuOp::Esadd: p = DPPS71(a.vf(fs)); break;
				case EfuOp::Ersadd: p = Rcp(DPPS71(a.vf(fs)), c); break;
				case EfuOp::Eleng: p = d::SQRTSS(DPPS71(a.vf(fs))); break;
				case EfuOp::Erleng: p = Rcp(d::SQRTSS(DPPS71(a.vf(fs))), c); break;
				case EfuOp::EatanXY:
				case EfuOp::EatanXZ:
				{
					const __m128 v = a.vf(fs);
					const __m128 x = v;
					const __m128 y = static_cast<EfuOp>(which) == EfuOp::EatanXY ? d::Float(_mm_shuffle_epi32(d::Int(v), 0x01)) :
																				   d::Float(_mm_shuffle_epi32(d::Int(v), 0x02));
					__m128 num = d::Compute(d::Arith::Sub, y, true, x, fsClampable, c);
					const __m128 den = d::Compute(d::Arith::Add, x, fsClampable, y, true, c);
					num = d::Compute(d::Arith::Div, num, true, den, true, c);
					p = Atan(num, c);
					break;
				}
				case EfuOp::Esum: // x + y + z + w, as (x + w) + (y + z)
				{
					__m128 f = a.vf(fs);
					__m128 t1 = d::Float(_mm_shuffle_epi32(d::Int(f), 0x1b));
					f = d::Compute(d::Arith::Add, f, fsClampable, t1, true, c, (m & mode::kSwap) && a.avx());
					t1 = d::Float(_mm_shuffle_epi32(d::Int(f), 0x01));
					p = d::Compute(d::Arith::Add, f, fsClampable, t1, true, c);
					break;
				}
				case EfuOp::Esqrt: p = d::SQRTSS(_mm_and_ps(d::Scalar(a.vfLane(fs, fsf)), d::All(0x7fffffffu))); break;
				case EfuOp::Ersqrt: p = Rcp(d::SQRTSS(_mm_and_ps(d::Scalar(a.vfLane(fs, fsf)), d::All(0x7fffffffu))), c); break;
				case EfuOp::Ercpr: p = Rcp(d::Scalar(a.vfLane(fs, fsf)), c); break;
				case EfuOp::Esin: // x + s2 x^3 + s3 x^5 + s4 x^7 + s5 x^9
				{
					const __m128 x = d::Scalar(a.vfLane(fs, fsf));
					__m128 pq = x;
					__m128 f = d::Compute(d::Arith::Mul, x, fsClampable, x, fsClampable, c); // x^2
					const __m128 t1 = f;
					f = d::Compute(d::Arith::Mul, f, fsClampable, pq, true, c); // x^3
					__m128 t2 = f;
					f = d::MULPS(f, d::Scalar(0xbe2aaaa4u)); // S2
					pq = d::Compute(d::Arith::Add, pq, true, f, fsClampable, c);
					for (const uint32_t k : {0x3c08873eu, 0xb94fb21fu}) // S3, S4
					{
						t2 = d::Compute(d::Arith::Mul, t2, true, t1, true, c);
						f = d::MULPS(t2, d::Scalar(k));
						pq = d::Compute(d::Arith::Add, pq, true, f, fsClampable, c);
					}
					t2 = d::Compute(d::Arith::Mul, t2, true, t1, true, c);
					t2 = d::MULPS(t2, d::Scalar(0x362e9c14u)); // S5
					p = d::Compute(d::Arith::Add, pq, true, t2, true, c);
					break;
				}
				case EfuOp::Eatan: // of (x - 1) / (x + 1)
				{
					const __m128 x = d::Scalar(a.vfLane(fs, fsf));
					const __m128 one = d::Scalar(0x3f800000u);
					__m128 f = d::SUBPS(x, one); // the first lanes: SUBSS
					const __m128 pq = d::ADDSS(x, one);
					f = d::Compute(d::Arith::Div, f, fsClampable, pq, true, c);
					p = Atan(f, c);
					break;
				}
				default: // EEXP: 1 / (1 + e1 x + e2 x^2 + ... + e6 x^6)^4
				{
					const __m128 x = d::Scalar(a.vfLane(fs, fsf));
					__m128 pq = d::MULPS(x, d::Scalar(0x3e7fffa8u)); // E1
					pq = d::ADDSS(pq, d::Scalar(0x3f800000u));
					__m128 t1 = d::Compute(d::Arith::Mul, x, fsClampable, x, fsClampable, c); // x^2
					__m128 t2 = t1;
					t1 = d::MULPS(t1, d::Scalar(0x3d0007f4u)); // E2
					pq = d::Compute(d::Arith::Add, pq, true, t1, true, c);
					for (const uint32_t k : {0x3b29d3ffu, 0x3933e553u, 0x36b63510u}) // E3, E4, E5
					{
						t2 = d::Compute(d::Arith::Mul, t2, true, x, fsClampable, c);
						t1 = d::MULPS(t2, d::Scalar(k));
						pq = d::Compute(d::Arith::Add, pq, true, t1, true, c);
					}
					t2 = d::Compute(d::Arith::Mul, t2, true, x, fsClampable, c);
					t2 = d::MULPS(t2, d::Scalar(0x353961acu)); // E6
					pq = d::Compute(d::Arith::Add, pq, true, t2, true, c);
					pq = d::Compute(d::Arith::Mul, pq, true, pq, true, c);
					pq = d::Compute(d::Arith::Mul, pq, true, pq, true, c);
					p = Rcp(pq, c);
					break;
				}
			}
			a.setPq(2 + ((m & mode::kWriteP) ? 1 : 0), d::Bits(p));
		}

		// ---- the rest -----------------------------------------------------------------------

		// doSwapOp: the lower instruction runs first, the upper reads the VF
		// register it writes as it was, and then the lower's result goes over it
		// whole.
		template <class A>
		PS2N_VU_OP void VfSave(A& a, uint32_t reg)
		{
			a.tv(0) = a.vf(reg);
		}
		template <class A>
		PS2N_VU_OP void VfSwap(A& a, uint32_t reg)
		{
			a.tv(1) = a.vf(reg);
			a.setVf(reg, a.tv(0));
		}
		template <class A>
		PS2N_VU_OP void VfRestore(A& a, uint32_t reg)
		{
			a.setVf(reg, a.tv(1));
		}

		template <class A>
		PS2N_VU_OP void MFlag(A& a)
		{
			a.regs().flags |= VUFLAG_MFLAGSET;
		}

		// A pair with the T bit: whether FBRST has the VU stop there. Whoever
		// runs the plan ends the program then, at the block's T bit `which`.
		template <class A>
		PS2N_VU_OP bool TBit(A&, uint32_t which, uint32_t vu1)
		{
			(void)which;
			return (VU0.VI[REG_FBRST].UL & (vu1 ? 0x800u : 0x8u)) != 0;
		}

		template <class A>
		PS2N_VU_OP void Xgkick(A& a)
		{
			::ps2n::vu::Xgkick(a.c.VIxgkick);
		}
		template <class A>
		PS2N_VU_OP void SetXgkick(A& a, uint32_t h)
		{
			a.c.VIxgkick = a.h(h);
		}
	} // namespace step
} // namespace ps2n::vu
