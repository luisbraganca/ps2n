// SPDX-License-Identifier: GPL-3.0-or-later
//
// The blocks PCSX2's EE recompiler would have formed.
//
// PCSX2's recompiler compiles EE code a block at a time, the first time
// execution arrives at an address, and counts time by those blocks: it adds a
// block's cycles and tests for events where the block ends. Where a block
// ends, what its instructions are counted as and a number of things it does
// in between were all fixed when it was compiled, from what the machine
// looked like at that moment: which other blocks existed, what COP0's Config
// register held, which registers the block itself had set to constants. Games
// are tested against that. So that PS2Native counts time exactly as stock
// PCSX2 does, this module keeps the same record: for every address at which
// the recompiler would hold a compiled block, the block as it would have
// compiled it.
//
// Nothing here generates code. A block is a list of the instructions it
// holds, each with what the recompiler decided about it, which PS2Native's
// interpreter carries out (runtime/ee_provider.cpp). The rules are those of
// recRecompile(), recompileNextInstruction(), recClear() and their helpers in
// pcsx2/x86/ix86-32/iR5900.cpp, of the instruction recompilers beside it, and
// of COP2MicroFinishPass in pcsx2/x86/iR5900Analysis.cpp (PCSX2 v2.9.189,
// Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+); docs/execution-model.md
// lists them.
#pragma once

#include "ps2n/module.h"
#include "ps2n/r5900.h"

#include "pcsx2/MemoryTypes.h"

#include <cstdint>
#include <vector>

namespace ps2n::ee::blocks
{
	// What the recompiler decided about one instruction of a block.
	enum StepInfo : uint16_t
	{
		kCostOnly = 1 << 0, // only counted here; a later step carries it out (DI)
		kDisableInterrupts = 1 << 1, // DI taking effect, after the instruction that follows it
		kFlush = 1 << 2, // the cycles of the block so far are added to the cycle count before it
		kSkippedSyscall = 1 << 3, // a FlushCache call: not made, a fixed number of cycles instead
		kEndsBlock = 1 << 4, // the block ends after it
		kForcesEventTest = 1 << 5, // an event test follows it whatever the time
		kConstAddress = 1 << 6, // a load or store whose address the recompiler knew
		kCop2Sync = 1 << 7, // VU0 is brought up to date with the EE first
		kCop2Finish = 1 << 8, // VU0's micro program is run to its end first
		kCop2Interlocked = 1 << 9, // an interlocked COP2 transfer came earlier in the block
		kDelaySlot = 1 << 10, // the delay slot of the step before
		kBranch = 1 << 11, // a branch or jump; a kDelaySlot step follows unless it was dropped
		kFastmem = 1 << 12, // a load or store compiled as a direct access to host memory, which faults once
		                    // if the memory turns out not to be plain; cleared when that has happened
		kCop2Status = 1 << 13, // a VU0 instruction that writes VU0's status flags: here they reach the register
		kCop2Mac = 1 << 14, // one that writes its MAC flags: here they reach the register
		kPlain = 1 << 15, // none of the above that the interpreter acts on but kFastmem, and no COP2 instruction: it
		                  // only carries it out, a load or store's memory looked at first (set when the block is formed;
		                  // ee_provider.cpp, RunRecSteps)
	};

	struct Step
	{
		r5900::Insn insn; // the word as it was when the block was formed, its address, the instruction
		uint16_t eighths; // what it is counted as, in eighths of a cycle
		uint16_t info; // StepInfo
	};

	// Block::body before the provider has compared the block with compiled code,
	// and when it found none that matches.
	inline constexpr uint8_t kBodyUnknown = 0xFF;
	inline constexpr uint8_t kBodyNone = 0xFE;

	enum BlockFlags : uint32_t
	{
		kIdleLoop = 1 << 0, // its branch back to its own start skips to the next event
		kTimeoutLoop = 1 << 1, // not run: its time passes (ps2n/waitloop.h)
		kManual = 1 << 2, // its page is not write-protected: its words are compared at every execution
		kCounted = 1 << 3, // and each execution counts towards protecting the page again
		kHookMain = 1 << 4, // eeloadHook() is called at every execution
		kHookExec = 1 << 5, // eeloadHook2() is called at every execution
		kCutShort = 1 << 6, // it ends where the scan for its end stopped, not at a branch or an instruction that ends one
		kInterpreted = 1 << 7, // the interpreter has carried it out whole (for the provider's report)
	};

	struct Block
	{
		uint32_t start = 0; // the address it was formed at
		uint32_t hw_start = 0; // its physical address: what it is found by
		uint32_t nominal_end = 0; // where the scan for its end stopped
		uint32_t size = 0; // the instructions it holds, as the recompiler records it
		uint32_t flags = 0;
		uint32_t timeout_reg = 0;
		uint32_t factor = 1; // what its instructions' counts were multiplied by: 2 if it was formed while the EE issued
		                     // one instruction a cycle
		uint8_t body = kBodyUnknown; // which compiled code its words are, for the provider: not looked at yet, none,
		                             // or a number the provider gives
		uint32_t* cache = nullptr; // where compiled code keeps what it knows of the block; zeroed when the block goes
		std::vector<Step> steps;
		std::vector<uint32_t> words; // of a manual block: [start, nominal_end) as it was when formed
		const uint8_t* host = nullptr; // of a manual block: where those words are in the host's memory, which does
		                               // not move while the machine runs (PSM() of its physical address)
		// What the provider notes of a block the interpreter carries out, for its
		// report, worked out once for as long as the images in use stay what they
		// were (ee_provider.cpp, NoteBlock): their epoch then (0: never), where
		// the block lies, and which of the BIOS's places; and the epoch at which
		// it last marked the block's start as one compiled code lacks.
		uint64_t noted = 0;
		uint64_t missing_noted = 0;
		uint8_t where = 0;
		uint8_t bios_place = 0;
	};

	// What became of the blocks, for the host's report.
	struct Stats
	{
		uint64_t formed = 0; // blocks formed
		uint64_t resets = 0; // times every block was thrown away
		uint64_t clears = 0; // requests to throw away the blocks over a range
		uint64_t cleared = 0; // blocks thrown away by them
		uint64_t manual = 0; // blocks formed in a page that is not write-protected
		uint64_t discarded = 0; // manual blocks found changed at execution
		uint64_t page_resets = 0; // pages put back under write protection
		uint64_t stale = 0; // times an older block over a new one's range was found changed
		uint64_t fastmem_faults = 0; // loads and stores the recompiler would have had to patch
		uint64_t delay_slot_branches = 0; // branches in delay slots, which the recompiler drops
		uint64_t delay_slot_oddities = 0; // other things in delay slots that are not reproduced
	};
	const Stats& GetStats();

	// The table of blocks by physical address (rec_blocks.cpp): a place for
	// every instruction of RAM and of the ROMs (the recompiler's recLUT).
	// Here so that Find() and Physical() are inlined: the interpreter asks
	// them for every block it carries out.
	namespace detail
	{
		extern Block** ram_slots;
		extern Block** rom_slots; // 1FC00000-1FFFFFFF
		extern Block** rom12_slots; // 1E000000-1E7FFFFF
		extern uint32_t ram_size;
	} // namespace detail
	inline constexpr uint32_t kNowhere = ~0u;

	// The physical address the recompiler files `pc` under, or kNowhere when
	// it has no table for it (neither RAM nor ROM): its hwLUT, RAM through its
	// eight mirrors, the ROMs in useg, kseg0 and kseg1.
	inline uint32_t Physical(uint32_t pc)
	{
		constexpr bool kRamMirror[16] = {true, false, true, true, false, false, false, false, true, false, true, true, true, true, false, false};
		const uint32_t top = pc >> 28;
		const uint32_t rest = pc & 0x0FFFFFFFu;
		if (kRamMirror[top] && rest < Ps2MemSize::ExposedRam)
			return rest;
		if (top == 0x1 || top == 0x9 || top == 0xB)
		{
			const uint32_t hw = 0x10000000u | rest;
			if (hw >= 0x1FC00000u || (hw >= 0x1E000000u && hw < 0x1E800000u))
				return hw;
		}
		return kNowhere;
	}

	// The slot for physical address `hw`, which Physical() gave.
	inline Block** Slot(uint32_t hw)
	{
		if (hw < detail::ram_size)
			return &detail::ram_slots[hw >> 2];
		if (hw >= 0x1FC00000u)
			return &detail::rom_slots[(hw - 0x1FC00000u) >> 2];
		return &detail::rom12_slots[(hw - 0x1E000000u) >> 2];
	}

	// Throws every block away (recResetRaw).
	void Reset();

	// The block at `pc`, or null.
	inline Block* Find(uint32_t pc)
	{
		const uint32_t hw = Physical(pc);
		if (hw == kNowhere || !detail::ram_slots) [[unlikely]]
			return nullptr;
		return *Slot(hw);
	}

	// Forms the block at `pc`, where there is none yet (recRecompile). Null
	// when `pc` is neither in RAM nor in ROM.
	Block* Form(uint32_t pc);

	// Throws away the blocks over `words` words from `addr` (recClear).
	void Clear(uint32_t addr, uint32_t words);

	// What the recompiler's code does first in a block whose page is not
	// write-protected: compares the block's words with memory, and counts the
	// execution towards protecting the page again. False when the block is gone
	// as a result: execution continues at cpuRegs.pc with whatever is formed
	// there next.
	bool EnterManual(Block& block);

	// Its parts, for compiled code that compares a block's words itself
	// (ps2n::ee::checkedAnswer): the count the block's page keeps towards
	// being protected again, when the block counts towards it (else null);
	// what follows when the words differ (dyna_block_discard); and what
	// follows when the count runs over (dyna_page_reset). Both of those throw
	// the block away.
	uint16_t* ManualCount(const Block& block);
	void DiscardManual(Block& block);
	void ResetManualPage(Block& block);

	// A block that is thrown away may be the one being carried out (it may
	// write to its own page). The recompiler's code for it would run on to the
	// block's end all the same. So a block thrown away is only no longer found
	// by Find(); it stays good to use until Collect(), which the provider calls
	// where no block is being carried out: at every event test. FindRunning()
	// finds a block that began at `start` among those too: what compiled code,
	// which only knows where its block began, asks with.
	Block* FindRunning(uint32_t start);
	void Collect();

	// Counts up whenever a block is formed, thrown away or gone for good. For
	// as long as it stays what it was, what FindRunning() answered still
	// holds, and the block and its steps are where they were.
	extern PS2N_API uint64_t epoch;

	// A load or store at `pc`, in a block that did not know its address, hit
	// memory that is not plain: the recompiler's code would have faulted there
	// the first time, been patched, and its block thrown away
	// (vtlb_BackpatchLoadStore). Does that once for each such instruction.
	void MemoryFault(uint32_t pc);
} // namespace ps2n::ee::blocks
