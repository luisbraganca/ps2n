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
		uint32_t extent; // one past the last instruction it was compiled from: beyond `end`
		                 // when it runs on into the code that follows it
		StaticFn fn;
		uint64_t hash; // of the instruction words in [start, extent) (ps2n::CodeHash)
		uint32_t first_resume; // its slice of ImageInfo::resume_points
		uint32_t resume_count;
		uint32_t first_variant; // its slice of ImageInfo::variants
		uint32_t variant_count;
	};

	// Other code that an earlier run found in a function's place in memory (a
	// program that rewrites itself, a patch), compiled as well. It has the
	// function's range and entries; which of them runs is decided by what is
	// in memory.
	struct VariantInfo
	{
		StaticFn fn;
		uint64_t hash; // of the instruction words in [start, extent)
		uint32_t extent;
	};

	// Which compiled code may run in a function's place. A function's state
	// starts as zero.
	enum FunctionState : uint8_t
	{
		kUnverified = 0, // RAM must be compared before anything runs
		kTrusted = 1, // RAM holds the function as the executable has it, and its pages are write-protected since
		kInvalid = 2, // RAM holds code nothing was compiled from; interpreted until that memory changes
		kFirstVariant = 3, // kFirstVariant + n: RAM holds the function's variant n, write-protected
	};
	inline constexpr uint32_t kMaxVariants = 256 - kFirstVariant;

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
		const uint32_t* resume_points; // every entry that is not a function start, sorted
		uint32_t resume_count;
		const VariantInfo* variants;
		uint32_t variant_count;
		uint8_t* state; // a FunctionState per function
		const char* generator; // the recompiler that wrote it
	};

	// ---- time ---------------------------------------------------------------
	// The core keeps time in EE cycles and schedules everything else against
	// them. Code is counted in blocks: a block ends after a branch or jump and
	// its delay slot (taken or not), and after SYSCALL, BREAK, ERET, EI and the
	// trap instructions (ps2n::r5900::isSystem). Its cost is the sum of its
	// instructions' cycle counts (ps2n::r5900::cycles, in eighths of a cycle),
	// divided by eight and rounded down, and never less than one: what PCSX2's
	// EE recompiler adds for a block (scaleblockcycles in
	// x86/ix86-32/iR5900.cpp, at its default cycle rate). Where a block starts
	// is decided by execution, not by the recompiler: at whatever instruction
	// follows the end of the block before. Compiled code and the fallback
	// interpreter count exactly alike, so a run is the same whichever of them
	// executes a block.
	inline uint32_t blockCycles(uint32_t eighths)
	{
		const uint32_t cycles = eighths >> 3;
		return cycles ? cycles : 1;
	}

	// Whether the core has an event to handle. Tested at the end of every block.
	inline bool eventDue()
	{
		return static_cast<int64_t>(cpuRegs.cycle - cpuRegs.nextEventCycle) >= 0;
	}

	// At the end of an idle loop's block (ps2n/waitloop.h): nothing will change
	// until the next event, so time moves on to it. PCSX2's WaitLoop setting
	// switches this off, as it does in its recompiler.
	inline void skipIdleTime()
	{
		if (EmuConfig.Speedhacks.WaitLoop && !eventDue())
			cpuRegs.cycle = cpuRegs.nextEventCycle;
	}

	// ---- runtime entry points for compiled code ------------------------------
	// Lets the core handle its events: interrupts, timers, DMA, the IOP, the
	// VUs. `next` is the instruction execution continues at. Returns the PC to
	// continue at: `next`, unless an exception was taken; or kUnwind.
	uint32_t eventTest(uint32_t next);

	// SYSCALL, BREAK, ERET, EI or a trap at `pc`, carried out by the core's own
	// function for it. `eighths` is the cost of the block this instruction
	// ends. Returns the PC to continue at (pc + 4 when nothing was raised), or
	// kUnwind.
	uint32_t systemInstruction(uint32_t pc, uint32_t code, uint32_t eighths);

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

	// Leaves compiled code in the middle of a block: the interpreter carries
	// out the rest of it, from `pc`, with `eighths` already counted. For what
	// the recompiler does not translate: an unknown encoding, a branch in a
	// delay slot. Returns the PC to continue at, or kUnwind.
	uint32_t leave(uint32_t pc, uint32_t eighths);

	// Called at the entry of a function, or of one of its variants, when the
	// function's state is not `mine`, the state in which that code is the one
	// to run. Returns true when it turns out to be, after comparing RAM.
	// Otherwise the caller must return *result at once: the PC to continue at
	// after the right code ran in its place, or kUnwind when RAM holds code
	// nothing was compiled from, which is then interpreted from `pc`.
	bool enterSlow(const ImageInfo& image, uint32_t index, uint32_t mine, uint32_t pc, uint32_t* result);

	// A function was entered at an address it has no label for. This cannot
	// happen unless the recompiler and the runtime disagree; it stops the run.
	[[noreturn]] void badEntry(const ImageInfo& image, uint32_t index, uint32_t pc);

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
// function. `acc_` is the function's count of eighths of a cycle since the
// last end of a block.

// A jump to another compiled function. Without the attribute it still works,
// but may leave a host frame behind for each such jump.
#if defined(__clang__)
#define PS2N_MUSTTAIL [[clang::musttail]]
#elif defined(__GNUC__) && __GNUC__ >= 15
#define PS2N_MUSTTAIL [[gnu::musttail]]
#else
#define PS2N_MUSTTAIL
#endif

// At the top of a generated function. `mine_` is the state in which this is
// the code to run: kTrusted for a function, kFirstVariant + n for its variant n.
#define PS2N_ENTER(state_, image_, index_, mine_, pc_) \
	do \
	{ \
		if ((state_)[index_] != (mine_)) [[unlikely]] \
		{ \
			uint32_t ps2n_pc; \
			if (!::ps2n::ee::enterSlow(image_, index_, mine_, pc_, &ps2n_pc)) \
				return ps2n_pc; \
		} \
	} while (0)

// The end of a block whose instructions since `acc_` was last brought up to
// date cost `eighths_`, with execution continuing at `next_`.
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

// The end of a block whose branch goes back to a place from which the code up
// to the branch is an idle loop (ps2n/waitloop.h). `back_`: the branch is
// taken. `whole_`: what the block costs when it began at that place, which is
// how compiled code tells that it did; only then is it the loop's block.
#define PS2N_BLOCK_END_LOOP(acc_, eighths_, next_, back_, whole_) \
	do \
	{ \
		const uint32_t ps2n_expected = (next_); \
		const uint32_t ps2n_eighths = (acc_) + (eighths_); \
		cpuRegs.cycle += ::ps2n::ee::blockCycles(ps2n_eighths); \
		(acc_) = 0; \
		if ((back_) && ps2n_eighths == (whole_)) \
			::ps2n::ee::skipIdleTime(); \
		if (::ps2n::ee::eventDue()) [[unlikely]] \
		{ \
			const uint32_t ps2n_next = ::ps2n::ee::eventTest(ps2n_expected); \
			if (ps2n_next != ps2n_expected) \
				return ps2n_next; \
		} \
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

// SYSCALL, BREAK, ERET, EI or a trap at `pc_`, which ends the block.
#define PS2N_SYSTEM(acc_, eighths_, pc_, code_) \
	do \
	{ \
		const uint32_t ps2n_pc = ::ps2n::ee::systemInstruction(pc_, code_, (acc_) + (eighths_)); \
		(acc_) = 0; \
		if (ps2n_pc != (pc_) + 4) \
			return ps2n_pc; \
	} while (0)
