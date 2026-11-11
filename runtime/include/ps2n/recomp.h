// SPDX-License-Identifier: GPL-3.0-or-later
//
// Everything statically compiled EE code needs: the registers and memory of
// the PCSX2 core, the instruction semantics, and the contract by which
// compiled code, the fallback interpreter and the core's scheduler work
// together. docs/execution-model.md explains the contract; this header is
// its definition.
#pragma once

#include "ps2n/ee_memory.h"
#include "ps2n/ee_state.h"
#include "ps2n/ops_cop0.h"
#include "ps2n/ops_cop2.h"
#include "ps2n/ops_fpu.h"
#include "ps2n/ops_int.h"
#include "ps2n/ops_mmi.h"

#include <cstddef>
#include <cstdint>

namespace ps2n::ee
{
	// A guest function compiled ahead of time.
	//
	// `pc` is where to enter it: its first instruction or another of its block
	// leaders. It returns the PC at which execution continues, which its
	// caller compares with the address it expects the function to return to;
	// on a mismatch the caller returns the value in turn, so that the host
	// stack unwinds to the dispatcher. That is sound because a host frame
	// holds no guest state: the registers are the core's, in memory.
	//
	// kUnwind is returned in place of a PC when every compiled frame has to be
	// left whatever the PC is. The dispatcher then continues at cpuRegs.pc.
	using StaticFn = uint32_t (*)(uint32_t pc);
	inline constexpr uint32_t kUnwind = 1; // never the address of an instruction

	struct FunctionInfo
	{
		uint32_t start; // guest address of its first instruction
		uint32_t end; // one past the last instruction it owns; its entries are in [start, end)
		StaticFn fn;
		uint32_t first_leader; // its slice of ImageInfo::leaders; the first is `start`
		uint32_t leader_count;
		uint32_t first_variant; // its slice of ImageInfo::variants
		uint32_t variant_count;
	};

	// Other code that an earlier run found in a function's place in memory (a
	// program that rewrites itself, a patch), compiled as well. It has the
	// function's range and leaders; which of them runs a block is decided by
	// what is in memory when the block is formed.
	struct VariantInfo
	{
		StaticFn fn;
		const uint64_t* segment_hashes; // one for each leader of its function, as ImageInfo::segment_hashes
	};

	// The code of one executable, compiled ahead of time.
	struct ImageInfo
	{
		const char* id; // names it among the images of a program
		const char* name; // the executable's file name
		uint32_t crc; // of the executable, as PCSX2 computes a game's CRC
		uint32_t text_start; // lowest compiled address
		uint32_t text_end; // one past the highest
		const FunctionInfo* functions; // sorted by start
		uint32_t function_count;
		const uint32_t* leaders; // every address at which a block of compiled code can begin, sorted
		const uint64_t* segment_hashes; // for each: of the words from it to the next leader or to its function's end
		                                // (ps2n::CodeHash). The image holds these fingerprints, not the code.
		uint32_t leader_count;
		const VariantInfo* variants;
		uint32_t variant_count;
		uint32_t* blocks; // for each leader: zero, or what compiled code needs to know of the block that begins there
		const char* generator; // the recompiler that wrote it
	};

	// ---- time ---------------------------------------------------------------
	// The core keeps time in EE cycles and schedules everything else against
	// them. PS2Native counts them exactly as PCSX2's EE recompiler does: by
	// the blocks that recompiler would have formed (runtime/rec_blocks.h).
	// Where a block begins is decided by execution, and where it ends by what
	// existed when it first ran, so compiled code learns both at run time:
	//
	//  - a block can only begin at a label (a leader). There the function
	//    looks up what the runtime knows of the block (PS2N_BLOCK_START): its
	//    end, and two things about how it ends;
	//  - reaching a label from the instruction before it, in the middle of a
	//    block, the function compares the label with that end. If they are
	//    equal the block is over there (PS2N_SPLIT_END) and another begins;
	//  - a block's cost is the sum of its instructions' counts
	//    (ps2n::r5900::cycles, in eighths of a cycle) divided by eight and
	//    rounded down, never less than one. At some instructions the count so
	//    far is added before the block ends (PS2N_FLUSH); the remainder stays.
	//
	// The function keeps three things in locals: `acc`, the eighths counted
	// since the block began or since the last PS2N_FLUSH, apart from those the
	// recompiler has folded into the macros' arguments; `ps2n_start`, the
	// address the block began at; `ps2n_end`, the word described below.
	inline uint32_t blockCycles(uint32_t eighths)
	{
		const uint32_t cycles = eighths >> 3;
		return cycles ? cycles : 1;
	}

	// What compiled code knows of a block (ImageInfo::blocks, `ps2n_end`): the
	// address at which it ends, and in the two low bits how. Zero in the
	// table: nothing is known, or the block is not one compiled code may run
	// without asking; blockStart() says.
	inline constexpr uint32_t kBlockTestsAtSplit = 1; // cut short at its end address, it tests for events there
	inline constexpr uint32_t kBlockIdleLoop = 2; // its branch back to its own start skips to the next event
	inline constexpr uint32_t kBlockEndMask = ~3u;

	// Whether the core has an event to handle.
	inline bool eventDue()
	{
		return static_cast<int64_t>(cpuRegs.cycle - cpuRegs.nextEventCycle) >= 0;
	}

	// Whether `address` is anything other than plain memory: a device's
	// registers, or nothing at all.
	inline bool notPlainMemory(uint32_t address)
	{
		return vtlb_private::vtlbdata.vmap[address >> vtlb_private::VTLB_PAGE_BITS].isHandler(address);
	}

	// ---- runtime entry points for compiled code ------------------------------
	// A block begins at leader `index` of `image`, in the function's own code
	// (`body` 0) or in its variant n (`body` 1 + n), and the image's table
	// says nothing of it. Forms the block if there is none yet, and checks
	// that the code it was formed from is the code this body was compiled
	// from. If compiled code is to run it, *block is what PS2N_BLOCK_START
	// needs. If not, *block is zero and the runtime has dealt with it some
	// other way (interpreted it, run the body that does match, let its time
	// pass): the caller returns the value returned, a PC or kUnwind.
	uint32_t blockStart(const ImageInfo& image, uint32_t index, uint32_t body, uint32_t* block);

	// Lets the core handle its events: interrupts, timers, DMA, the IOP, the
	// VUs. `next` is the instruction execution continues at. Returns the PC to
	// continue at: `next`, unless an exception was taken; or kUnwind.
	uint32_t eventTest(uint32_t next);

	// BREAK, ERET, EI or a trap at `pc`, carried out by the core's own
	// function for it. `eighths` is the cost of the block this instruction
	// ends. Returns the PC to continue at (pc + 4 when nothing was raised), or
	// kUnwind.
	uint32_t systemInstruction(uint32_t pc, uint32_t code, uint32_t eighths);

	// SYSCALL at `pc`, in the block that began at `start` and that it ends.
	// `eighths`: the cost of the block up to and including it. PCSX2's
	// recompiler does not make a FlushCache call whose number the block has
	// made a constant; it counts a fixed cost instead, and the runtime knows
	// which those are. Either way the block has ended when this returns, its
	// cost counted. Returns the PC to continue at (pc + 4 when no exception was
	// raised: a call not made, or one the core answers itself), or kUnwind.
	uint32_t syscallInstruction(uint32_t pc, uint32_t code, uint32_t start, uint32_t eighths);

	// A COP2 instruction, LQC2 or SQC2 at `pc`, in the block that began at
	// `start`, with `eighths` counted up to and including it. Whether VU0 has
	// to be brought up to date first, and whether the cycles so far are added
	// before that, PCSX2's recompiler decides from the whole block, so the
	// runtime does it all. Returns the eighths that remain counted.
	uint32_t cop2Instruction(uint32_t pc, uint32_t start, uint32_t eighths);

	// The load or store at `pc`, in the block that began at `start`, is about
	// to touch memory that is not plain. The code PCSX2's recompiler makes for
	// one whose address it did not know faults there the first time, and its
	// block is compiled again; the runtime keeps count of that. Returns true
	// when the block ends after this instruction: a read of an EE timer
	// register through an address the recompiler knew.
	bool memoryFault(uint32_t pc, uint32_t start);

	// An instruction PS2Native has no semantics of its own for yet, carried out
	// by the core's interpreter function for it. The second form is for an
	// instruction in a delay slot.
	void delegate(uint32_t pc, uint32_t code);
	void delegateInDelaySlot(uint32_t pc, uint32_t code);

	// A call whose target is only known at run time. Runs what is at `pc`,
	// compiled or not, as far as a compiled function goes or until interpreted
	// code reaches compiled code. Returns the PC to continue at, or kUnwind.
	uint32_t dispatch(uint32_t pc);

	// A jump whose target is only known at run time and is not one of the
	// jumping function's own blocks. Returns the PC to continue at, or kUnwind.
	uint32_t jump(uint32_t pc);

	// Leaves compiled code in the middle of the block that began at `start`:
	// the interpreter carries out the rest of it, from `pc`, with `eighths`
	// counted so far. For what the recompiler does not translate: an unknown
	// encoding; a branch whose delay slot holds what cannot be compiled there;
	// DI before a branch or before an instruction at which a block can begin.
	// Returns the PC to continue at, or kUnwind.
	uint32_t leave(uint32_t pc, uint32_t eighths, uint32_t start);

	// A function was entered at an address it has no label for. This cannot
	// happen unless the recompiler and the runtime disagree; it stops the run.
	[[noreturn]] void badEntry(const ImageInfo& image, uint32_t function, uint32_t pc);

	// The conditions of BC0x, BC1x and BC2x.
	bool cop0Condition();
	bool cop2Condition();
	inline bool fpuCondition()
	{
		return (fpuRegs.fprc[31] & 0x00800000u) != 0; // the C flag of FCR31
	}

	// The return address a call leaves in a register.
	inline void setLink(uint32_t reg, uint32_t address)
	{
		setU64(reg, address);
	}
} // namespace ps2n::ee

// ---- what generated code is written with --------------------------------------
// These are macros only so that the `return` inside them leaves the generated
// function, and so that they can use its locals `ps2n_start` and `ps2n_end`.
// `acc_` is the function's `acc`.

// A jump to another compiled function. Without the attribute it still works,
// but may leave a host frame behind for each such jump.
#if defined(__clang__)
#define PS2N_MUSTTAIL [[clang::musttail]]
#elif defined(__GNUC__) && __GNUC__ >= 15
#define PS2N_MUSTTAIL [[gnu::musttail]]
#else
#define PS2N_MUSTTAIL
#endif

// At a label, where a block begins: leader `index_` of the image, address
// `addr_`. `blocks` is the image's table.
#define PS2N_BLOCK_START(index_, addr_) \
	do \
	{ \
		ps2n_start = (addr_); \
		ps2n_end = blocks[index_]; \
		if (ps2n_end == 0) [[unlikely]] \
		{ \
			const uint32_t ps2n_pc = ::ps2n::ee::blockStart(image, index_, 0, &ps2n_end); \
			if (ps2n_end == 0) \
				return ps2n_pc; \
		} \
	} while (0)

// The same in variant `variant_` of a function, which always asks.
#define PS2N_BLOCK_START_VARIANT(index_, addr_, variant_) \
	do \
	{ \
		ps2n_start = (addr_); \
		const uint32_t ps2n_pc = ::ps2n::ee::blockStart(image, index_, 1 + (variant_), &ps2n_end); \
		if (ps2n_end == 0) \
			return ps2n_pc; \
	} while (0)

// The block was cut short at `next_`, a label reached from the instruction
// before it: its cost is counted, and events are tested for if it is long
// enough for PCSX2's recompiler to do so.
#define PS2N_SPLIT_END(acc_, eighths_, next_) \
	do \
	{ \
		cpuRegs.cycle += ::ps2n::ee::blockCycles((acc_) + (eighths_)); \
		(acc_) = 0; \
		if ((ps2n_end & ::ps2n::ee::kBlockTestsAtSplit) && ::ps2n::ee::eventDue()) [[unlikely]] \
		{ \
			const uint32_t ps2n_next = ::ps2n::ee::eventTest(next_); \
			if (ps2n_next != (next_)) \
				return ps2n_next; \
		} \
	} while (0)

// The end of a block at a branch, a jump or an instruction that ends one,
// with execution continuing at `next_`.
#define PS2N_BLOCK_END(acc_, eighths_, next_) \
	do \
	{ \
		const uint32_t ps2n_expected = (next_); \
		cpuRegs.cycle += ::ps2n::ee::blockCycles((acc_) + (eighths_)); \
		(acc_) = 0; \
		if (::ps2n::ee::eventDue()) [[unlikely]] \
		{ \
			const uint32_t ps2n_next = ::ps2n::ee::eventTest(ps2n_expected); \
			if (ps2n_next != ps2n_expected) \
				return ps2n_next; \
		} \
	} while (0)

// The same at a branch that may close an idle loop (ps2n/waitloop.h): taken
// (`back_`), in a block the runtime found to be one, time moves on to the
// next event, which is then handled whatever the time.
#define PS2N_BLOCK_END_LOOP(acc_, eighths_, next_, back_) \
	do \
	{ \
		const uint32_t ps2n_expected = (next_); \
		cpuRegs.cycle += ::ps2n::ee::blockCycles((acc_) + (eighths_)); \
		(acc_) = 0; \
		bool ps2n_test = ::ps2n::ee::eventDue(); \
		if ((back_) && (ps2n_end & ::ps2n::ee::kBlockIdleLoop)) \
		{ \
			if (!ps2n_test) \
				cpuRegs.cycle = cpuRegs.nextEventCycle; \
			ps2n_test = true; \
		} \
		if (ps2n_test) [[unlikely]] \
		{ \
			const uint32_t ps2n_next = ::ps2n::ee::eventTest(ps2n_expected); \
			if (ps2n_next != ps2n_expected) \
				return ps2n_next; \
		} \
	} while (0)

// Before an instruction at which PCSX2's recompiler adds the cycles of the
// block so far to the cycle count (ps2n::r5900::flushesCycles). `eighths_`
// includes the instruction.
#define PS2N_FLUSH(acc_, eighths_) \
	do \
	{ \
		const uint32_t ps2n_eighths = (acc_) + (eighths_); \
		cpuRegs.cycle += ::ps2n::ee::blockCycles(ps2n_eighths); \
		(acc_) = ps2n_eighths & 7u; \
	} while (0)

// A call of a compiled function that is expected to return to `ret_`.
#define PS2N_CALL(fn_, target_, ret_) \
	do \
	{ \
		const uint32_t ps2n_pc = fn_(target_); \
		if (ps2n_pc != (ret_)) \
			return ps2n_pc; \
	} while (0)

// A call whose target is only known at run time.
#define PS2N_CALL_DYNAMIC(target_, ret_) \
	do \
	{ \
		const uint32_t ps2n_pc = ::ps2n::ee::dispatch(target_); \
		if (ps2n_pc != (ret_)) \
			return ps2n_pc; \
	} while (0)

// A jump to an entry of another compiled function.
#define PS2N_JUMP(fn_, target_) PS2N_MUSTTAIL return fn_(target_)

// BREAK, ERET, EI or a trap at `pc_`, which ends the block.
#define PS2N_SYSTEM(acc_, eighths_, pc_, code_) \
	do \
	{ \
		const uint32_t ps2n_pc = ::ps2n::ee::systemInstruction(pc_, code_, (acc_) + (eighths_)); \
		(acc_) = 0; \
		if (ps2n_pc != (pc_) + 4) \
			return ps2n_pc; \
	} while (0)

// SYSCALL at `pc_`, which ends the block whether the call is made or not.
#define PS2N_SYSCALL(acc_, eighths_, pc_, code_) \
	do \
	{ \
		const uint32_t ps2n_pc = ::ps2n::ee::syscallInstruction(pc_, code_, ps2n_start, (acc_) + (eighths_)); \
		(acc_) = 0; \
		if (ps2n_pc != (pc_) + 4) \
			return ps2n_pc; \
	} while (0)

// A COP2 instruction, LQC2 or SQC2 at `pc_`.
#define PS2N_COP2(acc_, eighths_, pc_) (acc_) = ::ps2n::ee::cop2Instruction(pc_, ps2n_start, (acc_) + (eighths_))

// Before the load or store at `pc_` of the address in general register
// `base_` plus `offset_`. The second form is an expression, for a load after
// which the block may end: true when it does.
#define PS2N_MEMORY(base_, offset_, pc_) \
	do \
	{ \
		if (::ps2n::ee::notPlainMemory(::ps2n::ee::ru32(base_) + static_cast<uint32_t>(offset_))) [[unlikely]] \
			::ps2n::ee::memoryFault(pc_, ps2n_start); \
	} while (0)
#define PS2N_MEMORY_ENDS(base_, offset_, pc_) \
	(::ps2n::ee::notPlainMemory(::ps2n::ee::ru32(base_) + static_cast<uint32_t>(offset_)) && ::ps2n::ee::memoryFault(pc_, ps2n_start))
