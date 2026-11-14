// SPDX-License-Identifier: GPL-3.0-or-later
//
// Everything micro code compiled ahead of time needs: the contract between
// the code `ps2n-recomp vu` generates and PS2Native's VU provider
// (runtime/vu_interp.h), which runs a block of micro code by its plan
// (ps2n/vu_plan.h) and, where a build has compiled that plan, by the compiled
// function in the plan's place. docs/execution-model.md, "The vector units'
// micro programs", explains it.
//
// A compiled block is its plan's steps in order, each a call of the step's
// function (ps2n/vu_ops.h) with its operands as constants, on an accessor of
// its own (Locals below): the VU's registers are taken into locals of the
// function when it begins, the steps work on the locals, and what a step
// wrote is put back when the function ends. Everything else is where the
// machine has it. That is sound because nothing else looks at the VU's
// registers while one of its blocks runs: a block runs from its first pair
// to its last on the CPU thread, and what it calls out to (the GIF transfer
// of an XGKICK) reads data memory, which the steps write at once.
//
// A block then goes on to the next by itself, as the blocks of PCSX2's VU
// recompiler jump to each other: its last step (Leave) picks the block the
// record of programs and blocks has after it (Link), tests that block's
// cycles as microVU's code does where a block begins, and calls it in the
// place of returning. Only when the program stops (its end, its cycles used,
// a T bit) does a block return, to the provider.
#pragma once

#include "ps2n/vu_ops.h"

#include <cstdint>
#include <cstring>

// The calling convention of compiled blocks: as compiled EE functions
// (PS2N_CC in ps2n/recomp.h), under clang they keep no registers for their
// caller, so a block that uses many SSE registers saves none on entry.
#if defined(__clang__) && defined(__has_attribute)
#if __has_attribute(preserve_none)
#define PS2N_VU_CC __attribute__((preserve_none))
#endif
#endif
#ifndef PS2N_VU_CC
#define PS2N_VU_CC
#endif

// A block's call of the next one, in the place of its return: under clang a
// jump, so that a program of any length uses no host stack. Elsewhere the
// block returns the next one to the provider, which calls it.
#if defined(__clang__)
#define PS2N_VU_NEXT(next) [[clang::musttail]] return (next)->fn(c, (next))
#else
#define PS2N_VU_NEXT(next) return (next)
#endif

namespace ps2n::vu
{
	struct Link;

	// A block of micro code, carried out: by its plan's steps compiled ahead
	// of time, each with its operands as constants, or by the interpreter.
	//
	// `c` is the VU it runs on, `b` the block in the record of programs and
	// blocks. It returns null when the program has stopped (Ctx::stop says
	// why, Ctx::at at which block), and otherwise the block to carry out next:
	// with PS2N_VU_NEXT a jump that is never the case.
	using BlockFn = const Link* (PS2N_VU_CC*)(Ctx& c, const Link* b);

	// What a block's code is given of the record of programs and blocks: the
	// provider's record of a block begins with it.
	struct Link
	{
		BlockFn fn; // carries the block out
		uint32_t cycles; // the cycles it takes, tested before it begins (mVUtestCycles)
		uint32_t base; // the byte address of its first pair
		uint32_t pairs; // how many pairs it has
		// The blocks after it: [1] the one its branch leads to, [0] the one
		// after a conditional branch that is not taken. An indirect jump's
		// block is looked up (JumpTo).
		const Link* next[2];
	};

	// Why a program stopped (Ctx::stop).
	inline constexpr uint32_t kStopEnd = 1; // at the end of the block Ctx::at: the program's end, or an M bit
	inline constexpr uint32_t kStopCycles = 2; // before the block Ctx::at: its cycles are used
	inline constexpr uint32_t kStopUnsupported = 3; // at a block that holds something not carried out
	inline constexpr uint32_t kStopTBit = 16; // + n: the block's T bit n ended the program

	// The block an indirect jump of block `from` to `target` goes on at: the
	// start of a program of its own, found as microVU finds it
	// (runtime/vu_interp.cpp).
	const Link* JumpTo(Ctx& c, const Link* from, uint32_t target);

	PS2N_VU_OP const Link* Stop(Ctx& c, const Link* b, uint32_t why)
	{
		c.at = b;
		c.stop = why;
		return nullptr;
	}

	// mVUtestCycles: a block begins only while the program has cycles left
	// (with PCSX2's VU sync game fix, all of the block's). False: the program
	// stops before it.
	PS2N_VU_OP bool Enter(Ctx& c, const Link* next)
	{
		const int32_t left = c.syncHack ? c.cycles - static_cast<int32_t>(next->cycles) : c.cycles - 1;
		if (left < 0) [[unlikely]]
		{
			c.at = next;
			c.stop = kStopCycles;
			return false;
		}
		c.cycles -= static_cast<int32_t>(next->cycles);
		c.blocks++;
		c.pairs += next->pairs;
		return true;
	}

	struct BlockInfo
	{
		uint64_t key_a; // the plan's key (ps2n::vu::PlanKey)
		uint64_t key_b;
		BlockFn fn;
		uint32_t vu; // 0 or 1
	};

	// The compiled blocks one build of the recompiler gave (ps2n-recomp vu).
	struct ImageInfo
	{
		const char* id; // names it among the images of a host
		const BlockInfo* blocks;
		uint32_t block_count;
		const char* generator; // the recompiler that wrote it
	};

	namespace step
	{
		// Compiled code's accessor: the VU's registers in locals.
		//
		// Every register is taken when the function begins and every one a step
		// wrote is put back when it ends (finish()), and nothing is counted or
		// looked up for that as the code runs: which registers a block reads and
		// writes is in the code, so the compiler drops the loads of those it
		// never reads and knows which were written.
		struct Locals
		{
			Ctx& c;
			const uint32_t base_;
			// What the context and the machine say that a block cannot change,
			// read once: the compiler cannot know that a store into data memory
			// leaves them alone.
			VURegs* const R_;
			const uint32_t* const micro_;
			const uint32_t memMask_;
			const bool avx_;
			uint8_t* const vu0Mem_;
			uint8_t* const vu1Mem_;
			__m128 vf_[32];
			__m128 acc_;
			uint32_t vi_[16];
			uint32_t stat_[4];
			uint32_t mac_[4];
			uint32_t clip_[4];
			uint32_t pq_[4];
			uint32_t h_[16];
			uint32_t t0_;
			uint8_t* ea_;
			__m128 tv_[2];
			uint32_t branch_;
			bool branchWritten_;
			uint64_t vfWritten_; // bit r: VF register r; bit 32: ACC
			uint32_t viWritten_;
			uint32_t statWritten_, macWritten_, clipWritten_, pqWritten_;

#define PS2N_VU_EACH4(X) X(0) X(1) X(2) X(3)
#define PS2N_VU_EACH16(X) PS2N_VU_EACH4(X) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15)
#define PS2N_VU_EACH32(X) \
	PS2N_VU_EACH16(X) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)

			PS2N_VU_OP Locals(Ctx& ctx, uint32_t base)
				: c(ctx)
				, base_(base)
				, R_(ctx.R)
				, micro_(ctx.micro)
				, memMask_(ctx.memMask)
				, avx_(ctx.avx != 0)
				, vu0Mem_(VU0.Mem)
				, vu1Mem_(VU1.Mem)
				, t0_(0)
				, ea_(nullptr)
				, branch_(ctx.branch)
				, branchWritten_(false)
				, vfWritten_(0)
				, viWritten_(0)
				, statWritten_(0)
				, macWritten_(0)
				, clipWritten_(0)
				, pqWritten_(0)
			{
#define PS2N_VU_TAKE(r) vf_[r] = _mm_load_ps(R_->VF[r].F);
				PS2N_VU_EACH32(PS2N_VU_TAKE)
#undef PS2N_VU_TAKE
				acc_ = _mm_load_ps(R_->ACC.F);
#define PS2N_VU_TAKE(r) \
	vi_[r] = R_->VI[r].US[0]; \
	h_[r] = 0; // the host registers hold nothing (reset)
				PS2N_VU_EACH16(PS2N_VU_TAKE)
#undef PS2N_VU_TAKE
#define PS2N_VU_TAKE(i) \
	stat_[i] = c.statFlag[i]; \
	mac_[i] = c.macFlag[i]; \
	clip_[i] = c.clipFlag[i]; \
	pq_[i] = c.pq[i];
				PS2N_VU_EACH4(PS2N_VU_TAKE)
#undef PS2N_VU_TAKE
				tv_[0] = _mm_setzero_ps();
				tv_[1] = _mm_setzero_ps();
			}

			// What the steps wrote, put back where the machine has it.
			PS2N_VU_OP void finish()
			{
#define PS2N_VU_GIVE(r) \
	if (vfWritten_ & (1ull << r)) \
		_mm_store_ps(R_->VF[r].F, vf_[r]);
				PS2N_VU_EACH32(PS2N_VU_GIVE)
#undef PS2N_VU_GIVE
				if (vfWritten_ & (1ull << 32))
					_mm_store_ps(R_->ACC.F, acc_);
				if (branchWritten_)
					c.branch = branch_;
#define PS2N_VU_GIVE(r) \
	if (viWritten_ & (1u << r)) \
		R_->VI[r].US[0] = static_cast<u16>(vi_[r]);
				PS2N_VU_EACH16(PS2N_VU_GIVE)
#undef PS2N_VU_GIVE
#define PS2N_VU_GIVE(i) \
	if (statWritten_ & (1u << i)) \
		c.statFlag[i] = stat_[i]; \
	if (macWritten_ & (1u << i)) \
		c.macFlag[i] = mac_[i]; \
	if (clipWritten_ & (1u << i)) \
		c.clipFlag[i] = clip_[i]; \
	if (pqWritten_ & (1u << i)) \
		c.pq[i] = pq_[i];
				PS2N_VU_EACH4(PS2N_VU_GIVE)
#undef PS2N_VU_GIVE
			}

#undef PS2N_VU_EACH32
#undef PS2N_VU_EACH16
#undef PS2N_VU_EACH4

			PS2N_VU_OP __m128 vf(uint32_t r) const { return vf_[r]; }
			PS2N_VU_OP void setVf(uint32_t r, __m128 v)
			{
				vf_[r] = v;
				vfWritten_ |= 1ull << r;
			}
			PS2N_VU_OP uint32_t vfLane(uint32_t r, uint32_t lane) const
			{
				const __m128i v = _mm_castps_si128(vf_[r]);
				return static_cast<uint32_t>(lane == 0 ? _mm_cvtsi128_si32(v) :
											 lane == 1 ? _mm_extract_epi32(v, 1) :
											 lane == 2 ? _mm_extract_epi32(v, 2) :
														 _mm_extract_epi32(v, 3));
			}
			PS2N_VU_OP __m128 acc() const { return acc_; }
			PS2N_VU_OP void setAcc(__m128 v)
			{
				acc_ = v;
				vfWritten_ |= 1ull << 32;
			}
			PS2N_VU_OP uint32_t vi(uint32_t r) const { return vi_[r]; }
			PS2N_VU_OP void setVi(uint32_t r, uint32_t v)
			{
				vi_[r] = v & 0xffff;
				viWritten_ |= 1u << r;
			}
			PS2N_VU_OP uint32_t& h(uint32_t slot) { return h_[slot]; }
			PS2N_VU_OP uint32_t stat(uint32_t i) const { return stat_[i]; }
			PS2N_VU_OP void setStat(uint32_t i, uint32_t v)
			{
				stat_[i] = v;
				statWritten_ |= 1u << i;
			}
			PS2N_VU_OP uint32_t mac(uint32_t i) const { return mac_[i]; }
			PS2N_VU_OP void setMac(uint32_t i, uint32_t v)
			{
				mac_[i] = v;
				macWritten_ |= 1u << i;
			}
			PS2N_VU_OP uint32_t clip(uint32_t i) const { return clip_[i]; }
			PS2N_VU_OP void setClip(uint32_t i, uint32_t v)
			{
				clip_[i] = v;
				clipWritten_ |= 1u << i;
			}
			PS2N_VU_OP uint32_t pq(uint32_t i) const { return pq_[i]; }
			PS2N_VU_OP void setPq(uint32_t i, uint32_t v)
			{
				pq_[i] = v;
				pqWritten_ |= 1u << i;
			}
			PS2N_VU_OP uint32_t& t0() { return t0_; }
			PS2N_VU_OP uint8_t*& ea() { return ea_; }
			PS2N_VU_OP __m128& tv(uint32_t k) { return tv_[k]; }
			PS2N_VU_OP uint32_t base() const { return base_; }
			PS2N_VU_OP uint32_t branch() const { return branch_; }
			PS2N_VU_OP void setBranch(uint32_t v)
			{
				branch_ = v;
				branchWritten_ = true;
			}
			PS2N_VU_OP VURegs& regs() const { return *R_; }
			PS2N_VU_OP const uint32_t* micro() const { return micro_; }
			PS2N_VU_OP uint32_t memMask() const { return memMask_; }
			PS2N_VU_OP bool avx() const { return avx_; }
			PS2N_VU_OP uint8_t* vu0Mem() const { return vu0Mem_; }
			PS2N_VU_OP uint8_t* vu1Mem() const { return vu1Mem_; }
			PS2N_VU_OP uint8_t* vu1Regs() const { return reinterpret_cast<uint8_t*>(VU1.VF); }
		};

		// The interpreter's accessor for a compiled block: everything where the
		// machine has it, as when the block's plan is interpreted. To tell a
		// fault of the locals from one of a step: build the generated code with
		// PS2N_VU_ACCESS defined as ps2n::vu::step::InPlace.
		struct InPlace : Mem
		{
			PS2N_VU_OP InPlace(Ctx& ctx, uint32_t base)
				: Mem{ctx}
			{
				ctx.base = base;
				std::memset(ctx.h, 0, sizeof(ctx.h));
			}
		};
	} // namespace step
} // namespace ps2n::vu

// The accessor compiled blocks are built on.
#ifndef PS2N_VU_ACCESS
#define PS2N_VU_ACCESS ::ps2n::vu::step::Locals
#endif

// The step Leave, a block's last: how the program goes on from block `b`
// (`how`, `op` and `setup` as ps2n/vu_steps.inc has them), `a` being the
// block's accessor. What the block's link does to the flags' instances and
// to P and Q comes first, on the accessor; then what the steps wrote goes
// back (finish()); then the program goes on at the next block, or stops.
// With the operands constants, what is left is the one way this block takes.
#define PS2N_VU_GO(next_block) \
	do \
	{ \
		const ::ps2n::vu::Link* const ps2n_next = (next_block); \
		if (!::ps2n::vu::Enter(c, ps2n_next)) [[unlikely]] \
			return nullptr; \
		PS2N_VU_NEXT(ps2n_next); \
	} while (0)

#define PS2N_VU_LEAVE(how, op, setup) \
	do \
	{ \
		::ps2n::vu::step::Setup(a, (setup)); \
		a.finish(); \
		if ((how) == 0u) \
		{ \
			return ::ps2n::vu::Stop(c, b, ::ps2n::vu::kStopEnd); \
		} \
		else if ((how) == 1u) \
		{ \
			PS2N_VU_GO(b->next[1]); \
		} \
		else if ((how) == 2u) \
		{ \
			if (::ps2n::vu::step::Taken((op), a.branch())) \
				PS2N_VU_GO(b->next[1]); \
			else \
				PS2N_VU_GO(b->next[0]); \
		} \
		else if ((how) == 3u) \
		{ \
			PS2N_VU_GO(::ps2n::vu::JumpTo(c, b, a.branch())); \
		} \
		else \
		{ \
			const uint32_t ps2n_target = c.evilBranch; \
			c.evilBranch = c.evilevilBranch; \
			PS2N_VU_GO(::ps2n::vu::JumpTo(c, b, ps2n_target)); \
		} \
	} while (0)
