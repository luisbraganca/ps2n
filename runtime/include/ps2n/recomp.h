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

// The calling convention of compiled functions. Under clang they keep no
// registers for their caller (preserve_none). A compiled function holds
// nothing in host registers across a call of another that it does not take
// again from the core afterwards, so saving registers for it is wasted; and
// the eight a function saved on entry and restored on leaving were most of
// what a call between compiled functions cost. The runtime's own functions
// keep the platform's convention, so a compiled function may still keep
// values in registers across a call of the runtime. Elsewhere, and with a
// compiler that does not have the attribute, it is the platform's own.
#if defined(__clang__) && defined(__has_attribute)
#if __has_attribute(preserve_none)
#define PS2N_CC __attribute__((preserve_none))
#endif
#endif
#ifndef PS2N_CC
#define PS2N_CC
#endif

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
	using StaticFn = uint32_t(PS2N_CC*)(uint32_t pc);
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
	// The function keeps four things in locals: `acc`, the eighths counted
	// since the block began or since the last PS2N_FLUSH, apart from those the
	// recompiler has folded into the macros' arguments; `ps2n_start`, the
	// address the block began at; `ps2n_end`, the word described below; and
	// `ps2n_cycle`, the core's cycle count. The count in the core is always
	// right: whenever the function adds to its own it writes the sum there,
	// and after anything that may have changed the core's it takes it again
	// (PS2N_TIME). What the local saves is reading the count back from memory
	// at the end of every block, which is where a short loop spends its time.
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

	// Whether the core has an event to handle; the second form for a caller
	// that has the cycle count at hand.
	inline bool eventDue()
	{
		return static_cast<int64_t>(cpuRegs.cycle - cpuRegs.nextEventCycle) >= 0;
	}
	inline bool eventDueAt(uint64_t cycle)
	{
		return static_cast<int64_t>(cycle - cpuRegs.nextEventCycle) >= 0;
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
	// It has compiled functions' calling convention: they jump to it.
	uint32_t PS2N_CC jump(uint32_t pc);

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
// function, and so that they can use its locals `ps2n_start`, `ps2n_end` and
// `ps2n_cycle`. `acc_` is the function's `acc`.

// The function's copy of the cycle count, taken again: after every call out
// of the function that comes back into it, since any of them may have let
// time pass.
#define PS2N_TIME() ps2n_cycle = cpuRegs.cycle

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
// `addr_`. `blocks` is the image's table. (What the runtime answers goes
// through a variable of its own: were `ps2n_end` handed over, it would have
// to live in memory, and every block would begin by writing it there.)
#define PS2N_BLOCK_START(index_, addr_) \
	do \
	{ \
		ps2n_start = (addr_); \
		ps2n_end = blocks[index_]; \
		if (ps2n_end == 0) [[unlikely]] \
		{ \
			uint32_t ps2n_block = 0; \
			const uint32_t ps2n_pc = ::ps2n::ee::blockStart(image, index_, 0, &ps2n_block); \
			if (ps2n_block == 0) \
				return ps2n_pc; \
			ps2n_end = ps2n_block; \
			PS2N_TIME(); \
		} \
	} while (0)

// The same in variant `variant_` of a function, which always asks.
#define PS2N_BLOCK_START_VARIANT(index_, addr_, variant_) \
	do \
	{ \
		ps2n_start = (addr_); \
		uint32_t ps2n_block = 0; \
		const uint32_t ps2n_pc = ::ps2n::ee::blockStart(image, index_, 1 + (variant_), &ps2n_block); \
		if (ps2n_block == 0) \
			return ps2n_pc; \
		ps2n_end = ps2n_block; \
		PS2N_TIME(); \
	} while (0)

// The block was cut short at `next_`, a label reached from the instruction
// before it: its cost is counted, and events are tested for if it is long
// enough for PCSX2's recompiler to do so.
#define PS2N_SPLIT_END(acc_, eighths_, next_) \
	do \
	{ \
		ps2n_cycle += ::ps2n::ee::blockCycles((acc_) + (eighths_)); \
		cpuRegs.cycle = ps2n_cycle; \
		(acc_) = 0; \
		if ((ps2n_end & ::ps2n::ee::kBlockTestsAtSplit) && ::ps2n::ee::eventDueAt(ps2n_cycle)) [[unlikely]] \
		{ \
			const uint32_t ps2n_next = ::ps2n::ee::eventTest(next_); \
			if (ps2n_next != (next_)) \
				return ps2n_next; \
			PS2N_TIME(); \
		} \
	} while (0)

// The end of a block at a branch, a jump or an instruction that ends one,
// with execution continuing at `next_`.
#define PS2N_BLOCK_END(acc_, eighths_, next_) \
	do \
	{ \
		const uint32_t ps2n_expected = (next_); \
		ps2n_cycle += ::ps2n::ee::blockCycles((acc_) + (eighths_)); \
		cpuRegs.cycle = ps2n_cycle; \
		(acc_) = 0; \
		if (::ps2n::ee::eventDueAt(ps2n_cycle)) [[unlikely]] \
		{ \
			const uint32_t ps2n_next = ::ps2n::ee::eventTest(ps2n_expected); \
			if (ps2n_next != ps2n_expected) \
				return ps2n_next; \
			PS2N_TIME(); \
		} \
	} while (0)

// The same at a branch that may close an idle loop (ps2n/waitloop.h): taken
// (`back_`), in a block the runtime found to be one, time moves on to the
// next event, which is then handled whatever the time.
#define PS2N_BLOCK_END_LOOP(acc_, eighths_, next_, back_) \
	do \
	{ \
		const uint32_t ps2n_expected = (next_); \
		ps2n_cycle += ::ps2n::ee::blockCycles((acc_) + (eighths_)); \
		cpuRegs.cycle = ps2n_cycle; \
		(acc_) = 0; \
		bool ps2n_test = ::ps2n::ee::eventDueAt(ps2n_cycle); \
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
			PS2N_TIME(); \
		} \
	} while (0)

// Before an instruction at which PCSX2's recompiler adds the cycles of the
// block so far to the cycle count (ps2n::r5900::flushesCycles). `eighths_`
// includes the instruction.
#define PS2N_FLUSH(acc_, eighths_) \
	do \
	{ \
		const uint32_t ps2n_eighths = (acc_) + (eighths_); \
		ps2n_cycle += ::ps2n::ee::blockCycles(ps2n_eighths); \
		cpuRegs.cycle = ps2n_cycle; \
		(acc_) = ps2n_eighths & 7u; \
	} while (0)

// A call of a compiled function that is expected to return to `ret_`.
#define PS2N_CALL(fn_, target_, ret_) \
	do \
	{ \
		const uint32_t ps2n_pc = fn_(target_); \
		if (ps2n_pc != (ret_)) \
			return ps2n_pc; \
		PS2N_TIME(); \
	} while (0)

// A call whose target is only known at run time.
#define PS2N_CALL_DYNAMIC(target_, ret_) \
	do \
	{ \
		const uint32_t ps2n_pc = ::ps2n::ee::dispatch(target_); \
		if (ps2n_pc != (ret_)) \
			return ps2n_pc; \
		PS2N_TIME(); \
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
		PS2N_TIME(); \
	} while (0)

// SYSCALL at `pc_`, which ends the block whether the call is made or not.
#define PS2N_SYSCALL(acc_, eighths_, pc_, code_) \
	do \
	{ \
		const uint32_t ps2n_pc = ::ps2n::ee::syscallInstruction(pc_, code_, ps2n_start, (acc_) + (eighths_)); \
		(acc_) = 0; \
		if (ps2n_pc != (pc_) + 4) \
			return ps2n_pc; \
		PS2N_TIME(); \
	} while (0)

// A COP2 instruction, LQC2 or SQC2 at `pc_`.
#define PS2N_COP2(acc_, eighths_, pc_) \
	do \
	{ \
		(acc_) = ::ps2n::ee::cop2Instruction(pc_, ps2n_start, (acc_) + (eighths_)); \
		PS2N_TIME(); \
	} while (0)

// An instruction the core's interpreter function carries out, at `pc_`; the
// second form for one in a delay slot.
#define PS2N_DELEGATE(pc_, code_) \
	do \
	{ \
		::ps2n::ee::delegate(pc_, code_); \
		PS2N_TIME(); \
	} while (0)
#define PS2N_DELEGATE_IN_SLOT(pc_, code_) \
	do \
	{ \
		::ps2n::ee::delegateInDelaySlot(pc_, code_); \
		PS2N_TIME(); \
	} while (0)

// The load or store `op_` at `pc_`, carried out through register numbers, of
// the address in general register `base_` plus `offset_`. (The loads and
// stores of whole general registers are not written this way: PS2N_LOAD and
// PS2N_STORE, below.) The runtime is told first when it is about to touch
// memory that is not plain. `op_` is written out on both sides so that on the
// plain side the compiler already has the answer to what kind of memory it
// is, and asks only once. The second form is for a load after which the
// block may end: `ends_` is set when it does.
#define PS2N_ACCESS(base_, offset_, pc_, op_) \
	do \
	{ \
		if (::ps2n::ee::notPlainMemory(::ps2n::ee::ru32(base_) + static_cast<uint32_t>(offset_))) [[unlikely]] \
		{ \
			::ps2n::ee::memoryFault(pc_, ps2n_start); \
			op_; \
			PS2N_TIME(); \
		} \
		else \
		{ \
			op_; \
		} \
	} while (0)
#define PS2N_ACCESS_ENDS(ends_, base_, offset_, pc_, op_) \
	do \
	{ \
		if (::ps2n::ee::notPlainMemory(::ps2n::ee::ru32(base_) + static_cast<uint32_t>(offset_))) [[unlikely]] \
		{ \
			(ends_) = ::ps2n::ee::memoryFault(pc_, ps2n_start); \
			op_; \
			PS2N_TIME(); \
		} \
		else \
		{ \
			op_; \
		} \
	} while (0)

// ---- registers in local variables ---------------------------------------------
// A generated function keeps the registers it works on in local variables,
// which the C++ compiler can keep in host registers: nothing else can reach
// them, so a store into the console's memory does not make it read them
// again. The general registers are `g1` to `g31`, bits 0-63 of each; the
// FPU's are `f0` to `f31` and, for its accumulator, `facc`, 32 bits each. The
// generator knows at every point which locals hold a register's value and
// which of those differ from the core's copy. It puts the changed ones back
// (PS2N_PUT, PS2N_PUT_F, PS2N_PUT_ACC) before anything outside the function
// can look at a register or change one: the end of a block (an event test
// may follow), a call, an instruction carried out through register numbers.
// It takes a register (PS2N_GET, PS2N_GET_F, PS2N_GET_ACC) when the local
// does not hold it. After a call of another function, which may change any
// register, the locals hold nothing. At a label they hold what every way
// into it holds, and none of it changed: each way in brings them to that
// first, and an entry from outside the function (`E_` and the label's
// address) takes it from the core.
#define PS2N_GPR(n_) cpuRegs.GPR.r[n_].UD[0]
#define PS2N_GET(n_) g##n_ = PS2N_GPR(n_)
#define PS2N_PUT(n_) PS2N_GPR(n_) = g##n_
#define PS2N_GET_F(n_) f##n_ = fpuRegs.fpr[n_].UL
#define PS2N_PUT_F(n_) fpuRegs.fpr[n_].UL = f##n_
#define PS2N_GET_ACC() facc = fpuRegs.ACC.UL
#define PS2N_PUT_ACC() fpuRegs.ACC.UL = facc

namespace ps2n::ee
{
	// Whether `address` is anything other than plain memory: a device's
	// registers, or nothing at all.
	inline bool notPlainMemory(uint32_t address)
	{
		return vtlb_private::vtlbdata.vmap[address >> vtlb_private::VTLB_PAGE_BITS].isHandler(address);
	}

	// The address table of the core, for a function to keep the address of
	// (`ps2n_vmap`). The table's entries change as the kernel maps memory;
	// where the table is does not, for as long as the machine exists.
	inline const vtlb_private::VTLBVirtual* addressTable()
	{
		return vtlb_private::vtlbdata.vmap;
	}

	// A load from, and a store to, plain memory through its table entry.
	template <typename T>
	inline T plainRead(const vtlb_private::VTLBVirtual entry, uint32_t address)
	{
		T value;
		std::memcpy(&value, reinterpret_cast<const void*>(entry.assumePtr(address)), sizeof(T));
		return value;
	}
	template <typename T>
	inline void plainWrite(const vtlb_private::VTLBVirtual entry, uint32_t address, T value)
	{
		std::memcpy(reinterpret_cast<void*>(entry.assumePtr(address)), &value, sizeof(T));
	}
} // namespace ps2n::ee


// A value read from memory as an `as_` (int8_t for LB, uint8_t for LBU, and
// so on), widened to a general register.
#define PS2N_WIDEN(as_, value_) static_cast<uint64_t>(static_cast<int64_t>(static_cast<as_>(value_)))

// A load at `pc_` into the local `dst_`: a `T_` (mem8_t to mem64_t) read at
// `addr_` and widened as an `as_`. The address table is looked at once. When
// the address is not plain memory, the registers the generator knows to be
// changed are put back first (`put_`, a list of PS2N_PUT), the runtime is
// told, and the core's own function does the read. PS2N_LOAD_ENDS is for a
// load after which the block may end: `ends_` is set when it does.
// PS2N_LOAD_F is a load of 32 bits into the local of an FPU register.
#define PS2N_LOAD(dst_, as_, T_, addr_, pc_, put_) \
	do \
	{ \
		const uint32_t ps2n_a = (addr_); \
		const vtlb_private::VTLBVirtual ps2n_e = ps2n_vmap[ps2n_a >> vtlb_private::VTLB_PAGE_BITS]; \
		if (ps2n_e.isHandler(ps2n_a)) [[unlikely]] \
		{ \
			put_; \
			::ps2n::ee::memoryFault(pc_, ps2n_start); \
			(dst_) = PS2N_WIDEN(as_, vtlb_memRead<T_>(ps2n_a)); \
			PS2N_TIME(); \
		} \
		else \
		{ \
			(dst_) = PS2N_WIDEN(as_, ::ps2n::ee::plainRead<T_>(ps2n_e, ps2n_a)); \
		} \
	} while (0)
#define PS2N_LOAD_ENDS(ends_, dst_, as_, T_, addr_, pc_, put_) \
	do \
	{ \
		const uint32_t ps2n_a = (addr_); \
		const vtlb_private::VTLBVirtual ps2n_e = ps2n_vmap[ps2n_a >> vtlb_private::VTLB_PAGE_BITS]; \
		if (ps2n_e.isHandler(ps2n_a)) [[unlikely]] \
		{ \
			put_; \
			(ends_) = ::ps2n::ee::memoryFault(pc_, ps2n_start); \
			(dst_) = PS2N_WIDEN(as_, vtlb_memRead<T_>(ps2n_a)); \
			PS2N_TIME(); \
		} \
		else \
		{ \
			(dst_) = PS2N_WIDEN(as_, ::ps2n::ee::plainRead<T_>(ps2n_e, ps2n_a)); \
		} \
	} while (0)
#define PS2N_LOAD_F(dst_, addr_, pc_, put_) \
	do \
	{ \
		const uint32_t ps2n_a = (addr_); \
		const vtlb_private::VTLBVirtual ps2n_e = ps2n_vmap[ps2n_a >> vtlb_private::VTLB_PAGE_BITS]; \
		if (ps2n_e.isHandler(ps2n_a)) [[unlikely]] \
		{ \
			put_; \
			::ps2n::ee::memoryFault(pc_, ps2n_start); \
			(dst_) = vtlb_memRead<mem32_t>(ps2n_a); \
			PS2N_TIME(); \
		} \
		else \
		{ \
			(dst_) = ::ps2n::ee::plainRead<mem32_t>(ps2n_e, ps2n_a); \
		} \
	} while (0)

// A store at `pc_` of the low bits of `value_` as a `T_` at `addr_`, in the
// same way.
#define PS2N_STORE(T_, addr_, value_, pc_, put_) \
	do \
	{ \
		const uint32_t ps2n_a = (addr_); \
		const vtlb_private::VTLBVirtual ps2n_e = ps2n_vmap[ps2n_a >> vtlb_private::VTLB_PAGE_BITS]; \
		if (ps2n_e.isHandler(ps2n_a)) [[unlikely]] \
		{ \
			put_; \
			::ps2n::ee::memoryFault(pc_, ps2n_start); \
			vtlb_memWrite<T_>(ps2n_a, static_cast<T_>(value_)); \
			PS2N_TIME(); \
		} \
		else \
		{ \
			::ps2n::ee::plainWrite<T_>(ps2n_e, ps2n_a, static_cast<T_>(value_)); \
		} \
	} while (0)
