// SPDX-License-Identifier: GPL-3.0-or-later
//
// Everything IOP code compiled ahead of time needs: the contract between the
// code `ps2n-recomp iop` generates and PS2Native's IOP provider
// (runtime/iop_provider.h). docs/execution-model.md, "The I/O processor's
// code", explains it.
//
// A compiled block is one block as PCSX2's IOP recompiler formed it in a run
// that was captured: its instructions in order, each the function of
// ps2n/iop_ops.h for it with its operands as constants, on the IOP's
// registers held in locals of the function. The registers a block reads are
// taken from the core where it begins; those it wrote are put back before
// anything else can look: where it ends, before a SYSCALL or BREAK, before
// an import's function of the core's, before an instruction of the GTE's.
// What the core does for a load or a store that is not RAM reads none of
// the IOP's registers, so the locals stay across it.
//
// Nothing in a block's code says where the block is: what depends on that
// (a branch's target, the address a call returns to) is reckoned from the
// address the record has for it (Block::pc), so the same instructions run by
// the same function wherever the IOP's kernel loaded them.
//
// A block goes on to the next by itself where the record has one, as the
// blocks of PCSX2's recompiler jump to each other: it calls the next block
// in the place of returning. It returns to the provider when the IOP's
// stretch of time is over, and where the record has no block yet.
#pragma once

#include "ps2n/iop_ops.h"

#include <cstdint>

// A block's call of the next one, in the place of its return: under clang a
// jump, so that a stretch of any length uses no host stack. Elsewhere the
// block returns to the provider, which finds the next one.
#if defined(__clang__)
#define PS2N_IOP_NEXT(next) [[clang::musttail]] return (next)->fn(next)
#else
#define PS2N_IOP_NEXT(next) return true
#endif

namespace ps2n::iop
{
	struct BlockInfo
	{
		uint64_t key_a; // the key of the block's instructions (ps2n::r3000a::key)
		uint64_t key_b;
		BlockFn fn;
	};

	// The compiled blocks one build of the recompiler gave (ps2n-recomp iop).
	struct ImageInfo
	{
		const char* id; // names it among the images of a host
		const BlockInfo* blocks;
		uint32_t block_count;
		const char* generator; // the recompiler that wrote it
	};

	// Where the IOP goes on after block `b`, by its way out `way` to `target`:
	// at the next block if the record has one, else through the provider.
	// `tested` says an event test came first, which may have sent the IOP
	// elsewhere.
#define PS2N_IOP_GO(way, target, tested) \
	do \
	{ \
		if ((tested) && psxRegs.pc != (target)) [[unlikely]] \
			return true; \
		::ps2n::iop::Block* const ps2n_next = ::ps2n::iop::Next(b, (way), (target)); \
		if (!ps2n_next) [[unlikely]] \
			return true; \
		PS2N_IOP_NEXT(ps2n_next); \
	} while (0)

	// Where a compiled block begins: it is counted, and it knows where the
	// IOP's registers are and where it is itself.
#define PS2N_IOP_ENTER() \
	b->runs++; \
	[[maybe_unused]] uint32_t* const r = psxRegs.GPR.r; \
	[[maybe_unused]] const uint32_t pc = b->pc

	// Where it ends at a branch or jump whose target the code says: by its way
	// out `way` to `target`, its cycles added and tested (iPsxBranchTest).
#define PS2N_IOP_LEAVE(way, target, cycles) \
	do \
	{ \
		const uint32_t ps2n_to = (target); \
		psxRegs.pc = ps2n_to; \
		const int ps2n_went = ::ps2n::iop::BranchTest(cycles); \
		if (ps2n_went == ::ps2n::iop::kOver) \
			return false; \
		PS2N_IOP_GO((way), ps2n_to, ps2n_went == ::ps2n::iop::kOnTested); \
	} while (0)

	// The same for a block that may be a wait loop, where its branch is
	// taken: the record says whether it is one (Block::flags), and it is one
	// only when the branch goes back to the block's first instruction.
#define PS2N_IOP_LEAVE_LOOP(way, target, cycles) \
	do \
	{ \
		const uint32_t ps2n_to = (target); \
		psxRegs.pc = ps2n_to; \
		const int ps2n_went = ((b->flags & ::ps2n::iop::kWaitLoop) && ps2n_to == b->pc) ? ::ps2n::iop::WaitLoopTest() : \
																							  ::ps2n::iop::BranchTest(cycles); \
		if (ps2n_went == ::ps2n::iop::kOver) \
			return false; \
		PS2N_IOP_GO((way), ps2n_to, ps2n_went == ::ps2n::iop::kOnTested); \
	} while (0)

	// Where it ends at a jump through a register.
#define PS2N_IOP_LEAVE_ANYWHERE(target, cycles) \
	do \
	{ \
		const uint32_t ps2n_to = (target); \
		psxRegs.pc = ps2n_to; \
		if (ps2n_to & 3) [[unlikely]] \
			return ::ps2n::iop::Unaligned(ps2n_to); \
		const int ps2n_went = ::ps2n::iop::BranchTest(cycles); \
		if (ps2n_went == ::ps2n::iop::kOver) \
			return false; \
		PS2N_IOP_GO_ANYWHERE(ps2n_to, ps2n_went == ::ps2n::iop::kOnTested); \
	} while (0)

	// Where it was cut short, and runs on into the block at `target`.
#define PS2N_IOP_RUN_ON(target, cycles) \
	do \
	{ \
		const uint32_t ps2n_to = (target); \
		::ps2n::iop::RunOn(cycles); \
		psxRegs.pc = ps2n_to; \
		PS2N_IOP_GO(1u, ps2n_to, false); \
	} while (0)

	// The same after a jump through a register: the block is looked up.
#define PS2N_IOP_GO_ANYWHERE(target, tested) \
	do \
	{ \
		if ((tested) && psxRegs.pc != (target)) [[unlikely]] \
			return true; \
		::ps2n::iop::Block* const ps2n_next = ::ps2n::iop::Find(target); \
		if (!ps2n_next) [[unlikely]] \
			return true; \
		PS2N_IOP_NEXT(ps2n_next); \
	} while (0)
} // namespace ps2n::iop
