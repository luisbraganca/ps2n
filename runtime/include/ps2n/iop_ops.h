// SPDX-License-Identifier: GPL-3.0-or-later
//
// The I/O processor's instructions, carried out, and the ends of its blocks:
// what PS2Native's IOP interpreter (runtime/iop_provider.cpp) and IOP code
// compiled ahead of time (ps2n/iop_recomp.h) are both made of, so that each
// instruction is defined once.
//
// What they agree with is PCSX2's IOP recompiler, as docs/adr/0005 has every
// family follow PCSX2's recompiler (pcsx2/x86/iR3000A.cpp, iR3000Atables.cpp;
// PCSX2 v2.9.170, Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+): no overflow
// exceptions, no load delay, a division by zero that gives what its code
// gives, loads that read RAM wherever bit 28 of the address is clear, and a
// block's time added where the block ends.
//
// The IOP's registers are the core's (`psxRegs`), its memory the core's
// (`iopMem`, and the core's functions for everything that is not RAM).
#pragma once

#include "ps2n/module.h"
#include "ps2n/pcsx2.h"
#include "ps2n/r3000a.h"

#include <stdint.h>

#define PS2N_IOP_OP [[gnu::always_inline]] inline

// The calling convention of a block's function: as compiled micro code
// (PS2N_VU_CC in ps2n/vu_recomp.h), under clang it keeps no registers for
// its caller.
#if defined(__clang__) && defined(__has_attribute)
#if __has_attribute(preserve_none)
#define PS2N_IOP_CC __attribute__((preserve_none))
#endif
#endif
#ifndef PS2N_IOP_CC
#define PS2N_IOP_CC
#endif

namespace ps2n::iop
{
	struct Block;

	// A block of IOP code, carried out: by the function a build compiled for
	// it ahead of time, or by the interpreter. False: the IOP's stretch of
	// time is over (the provider's ExecuteBlock returns). True: the provider
	// goes on at `psxRegs.pc`.
	using BlockFn = bool (PS2N_IOP_CC*)(Block* b);

	// The block one of a block's two ways out led to when it was last taken,
	// good for as long as no block has been thrown away since.
	struct Exit
	{
		Block* to = nullptr;
		uint32_t generation = 0;
	};

	// A block as PCSX2's IOP recompiler would hold it: formed when the IOP
	// first got to its address, from what memory held then.
	struct Block
	{
		BlockFn fn = nullptr; // carries it out
		uint32_t pc = 0; // the address it was formed at; everything in it that depends on where it is, is relative to this
		uint32_t count = 0; // its instructions
		uint32_t cycles = 0; // what it costs, whole
		uint32_t flags = 0;
		Exit exit[2]; // [1] its branch taken (or its jump, or its running on into the next block), [0] not taken
		uint64_t runs = 0; // how often it began
		const uint32_t* words = nullptr; // its instructions, as they were when it was formed
		// For each `addiu $zero, $zero, n` in it, in order: the core's function
		// for the import that instruction names, or null (irxImportHLE).
		const irxHLE* imports = nullptr;
		uint32_t hw = 0; // the address it is filed under (PCSX2's HWADDR)
		uint32_t shape = 0; // which of the shapes the provider has met it is, for its counts
	};

	inline constexpr uint32_t kBranch = 1; // it ends at a branch or jump and its delay slot
	inline constexpr uint32_t kWaitLoop = 2; // and that is a wait loop, with PCSX2's WaitLoop setting on
	inline constexpr uint32_t kCompiled = 4; // `fn` is a function compiled ahead of time
	inline constexpr uint32_t kRetired = 8; // the record no longer has it; it may still be running

	// What a block's code counts for the host's report.
	struct Counters
	{
		uint64_t exceptions = 0; // SYSCALL and BREAK instructions carried out
		uint64_t imports = 0; // imports of modules for which a function of the core's was called
		uint64_t importsDone = 0; // of them, those the function did itself
		uint64_t waits = 0; // branches of wait loops taken: time passed to the next event
	};
	extern PS2N_API Counters g_counters;

	// The provider's record, as far as a block's code needs it.
	extern PS2N_API uint32_t g_generation; // goes up whenever a block is thrown away
	extern PS2N_API uint32_t g_clearLimit; // PCSX2's g_psxMaxRecMem: no block ends above it
	extern PS2N_API uint32_t g_ramMask; // the size of the IOP's RAM, less one
	extern PS2N_API Block** g_lut; // for every address a block can begin at, the block that does
	extern PS2N_API int32_t g_page[0x10000]; // for every 64 KB of the address space, where its part of that table begins; -1: none

	// The block that begins at `pc`, if the record has one.
	PS2N_IOP_OP Block* Find(uint32_t pc)
	{
		const int32_t base = g_page[pc >> 16];
		return base < 0 ? nullptr : g_lut[static_cast<uint32_t>(base) + ((pc & 0xFFFF) >> 2)];
	}

	// A block one of `b`'s ways out leads to, or null when the record has
	// none there yet (the provider then forms it).
	PS2N_IOP_OP Block* Next(Block* b, uint32_t way, uint32_t target)
	{
		Exit& e = b->exit[way];
		if (e.generation == g_generation) [[likely]]
			return e.to;
		Block* const next = Find(target);
		if (next)
		{
			e.to = next;
			e.generation = g_generation;
		}
		return next;
	}

	// A word of RAM was written at `address` (a physical address, a multiple
	// of four) where a block begins: the blocks over it are thrown away
	// (PCSX2's psxRecClearMem).
	PS2N_API void ClearBlocksAt(uint32_t address);

	// What PCSX2's recClearIOP does for a store of the IOP's own.
	PS2N_IOP_OP void Written(uint32_t physical)
	{
		const uint32_t at = physical & ~3u;
		if (at < g_clearLimit && g_lut[(at & g_ramMask) >> 2]) [[unlikely]]
			ClearBlocksAt(at);
	}

	// An instruction of the GTE's: the core's function for it, as PCSX2's
	// recompiler calls it.
	PS2N_API void Gte(uint32_t code);

	// A jump through a register to an address that is not a multiple of four:
	// PCSX2's recompiler stops the machine there, and so does the provider.
	// Returns false, for a block to return.
	PS2N_API bool Unaligned(uint32_t target);

	// ---------------------------------------------------------------------
	// Memory.

	PS2N_IOP_OP uint8_t* Ram(uint32_t address)
	{
		return iopMem->Main + (address & g_ramMask);
	}

	// A load, as the recompiler compiles it (rpsxLoad): RAM itself wherever
	// bit 28 of the address is clear, the core's function otherwise.
	PS2N_IOP_OP uint32_t Load8(uint32_t address)
	{
		if (!(address & 0x10000000)) [[likely]]
			return *Ram(address);
		return iopMemRead8(address);
	}
	PS2N_IOP_OP uint32_t Load16(uint32_t address)
	{
		if (!(address & 0x10000000)) [[likely]]
		{
			uint16_t v;
			__builtin_memcpy(&v, Ram(address), 2);
			return v;
		}
		return iopMemRead16(address);
	}
	PS2N_IOP_OP uint32_t Load32(uint32_t address)
	{
		if (!(address & 0x10000000)) [[likely]]
		{
			uint32_t v;
			__builtin_memcpy(&v, Ram(address), 4);
			return v;
		}
		return iopMemRead32(address);
	}
	PS2N_IOP_OP uint32_t LoadS8(uint32_t address)
	{
		return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(Load8(address))));
	}
	PS2N_IOP_OP uint32_t LoadS16(uint32_t address)
	{
		return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(Load16(address))));
	}
	// A load into $zero: nothing where the recompiler reads RAM, the core's
	// function, for what reading does to a device, otherwise.
	PS2N_IOP_OP void LoadNowhere(uint32_t address, uint32_t bits)
	{
		if (!(address & 0x10000000))
			return;
		if (bits == 8)
			iopMemRead8(address);
		else if (bits == 16)
			iopMemRead16(address);
		else
			iopMemRead32(address);
	}

	// The core's iopMemRead32, with the way it takes for RAM here.
	PS2N_IOP_OP uint32_t Read32(uint32_t address)
	{
		const uint32_t physical = address & 0x1FFFFFFF;
		if (physical < 0x00800000) [[likely]]
		{
			uint32_t v;
			__builtin_memcpy(&v, Ram(physical), 4);
			return v;
		}
		return iopMemRead32(address);
	}

	// A store: the core's iopMemWrite8/16/32, with the way they take for RAM
	// here, and the provider told of it as the core tells it (`Clear`).
	PS2N_IOP_OP bool PlainRam(uint32_t physical)
	{
		return physical < 0x00800000 && !(psxRegs.CP0.n.Status & 0x10000);
	}
	PS2N_IOP_OP void Store8(uint32_t address, uint32_t value)
	{
		const uint32_t physical = address & 0x1FFFFFFF;
		if (PlainRam(physical)) [[likely]]
		{
			*Ram(physical) = static_cast<uint8_t>(value);
			Written(physical);
		}
		else
			iopMemWrite8(address, static_cast<uint8_t>(value));
	}
	PS2N_IOP_OP void Store16(uint32_t address, uint32_t value)
	{
		const uint32_t physical = address & 0x1FFFFFFF;
		if (PlainRam(physical)) [[likely]]
		{
			const uint16_t v = static_cast<uint16_t>(value);
			__builtin_memcpy(Ram(physical), &v, 2);
			Written(physical);
		}
		else
			iopMemWrite16(address, static_cast<uint16_t>(value));
	}
	PS2N_IOP_OP void Store32(uint32_t address, uint32_t value)
	{
		const uint32_t physical = address & 0x1FFFFFFF;
		if (PlainRam(physical)) [[likely]]
		{
			__builtin_memcpy(Ram(physical), &value, 4);
			Written(physical);
		}
		else
			iopMemWrite32(address, value);
	}

	// ---------------------------------------------------------------------
	// The instructions that compute.

	namespace val
	{
		PS2N_IOP_OP uint32_t ADDIU(uint32_t s, int32_t imm) { return s + static_cast<uint32_t>(imm); }
		PS2N_IOP_OP uint32_t SLTI(uint32_t s, int32_t imm) { return static_cast<int32_t>(s) < imm ? 1u : 0u; }
		PS2N_IOP_OP uint32_t SLTIU(uint32_t s, int32_t imm) { return s < static_cast<uint32_t>(imm) ? 1u : 0u; }
		PS2N_IOP_OP uint32_t ANDI(uint32_t s, uint32_t immu) { return s & immu; }
		PS2N_IOP_OP uint32_t ORI(uint32_t s, uint32_t immu) { return s | immu; }
		PS2N_IOP_OP uint32_t XORI(uint32_t s, uint32_t immu) { return s ^ immu; }
		PS2N_IOP_OP uint32_t LUI(uint32_t immu) { return immu << 16; }
		PS2N_IOP_OP uint32_t ADDU(uint32_t s, uint32_t t) { return s + t; }
		PS2N_IOP_OP uint32_t SUBU(uint32_t s, uint32_t t) { return s - t; }
		PS2N_IOP_OP uint32_t AND(uint32_t s, uint32_t t) { return s & t; }
		PS2N_IOP_OP uint32_t OR(uint32_t s, uint32_t t) { return s | t; }
		PS2N_IOP_OP uint32_t XOR(uint32_t s, uint32_t t) { return s ^ t; }
		PS2N_IOP_OP uint32_t NOR(uint32_t s, uint32_t t) { return ~(s | t); }
		PS2N_IOP_OP uint32_t SLT(uint32_t s, uint32_t t) { return static_cast<int32_t>(s) < static_cast<int32_t>(t) ? 1u : 0u; }
		PS2N_IOP_OP uint32_t SLTU(uint32_t s, uint32_t t) { return s < t ? 1u : 0u; }
		PS2N_IOP_OP uint32_t SLL(uint32_t t, uint32_t sa) { return t << (sa & 31); }
		PS2N_IOP_OP uint32_t SRL(uint32_t t, uint32_t sa) { return t >> (sa & 31); }
		PS2N_IOP_OP uint32_t SRA(uint32_t t, uint32_t sa) { return static_cast<uint32_t>(static_cast<int32_t>(t) >> (sa & 31)); }

		// HI and LO together: LO in the low half.
		PS2N_IOP_OP uint64_t MULT(uint32_t s, uint32_t t)
		{
			return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(s)) * static_cast<int64_t>(static_cast<int32_t>(t)));
		}
		PS2N_IOP_OP uint64_t MULTU(uint32_t s, uint32_t t) { return static_cast<uint64_t>(s) * static_cast<uint64_t>(t); }
		PS2N_IOP_OP uint64_t HiLo(uint32_t hi, uint32_t lo) { return (static_cast<uint64_t>(hi) << 32) | lo; }
		// rpsxDIVsuper: the smallest number by -1 gives itself and no
		// remainder; by zero, the dividend as the remainder and 1 or -1.
		PS2N_IOP_OP uint64_t DIV(uint32_t s, uint32_t t)
		{
			if (s == 0x80000000u && t == 0xFFFFFFFFu)
				return HiLo(0, 0x80000000u);
			if (t == 0)
				return HiLo(s, static_cast<int32_t>(s) < 0 ? 1u : 0xFFFFFFFFu);
			return HiLo(static_cast<uint32_t>(static_cast<int32_t>(s) % static_cast<int32_t>(t)),
				static_cast<uint32_t>(static_cast<int32_t>(s) / static_cast<int32_t>(t)));
		}
		PS2N_IOP_OP uint64_t DIVU(uint32_t s, uint32_t t)
		{
			if (t == 0)
				return HiLo(s, 0xFFFFFFFFu);
			return HiLo(s % t, s / t);
		}

		// The unaligned loads and stores (psxLWL, psxLWR, psxSWL, psxSWR, which
		// the recompiler calls): `old` is what the register holds, `mem` the
		// word of memory the address is in.
		PS2N_IOP_OP uint32_t LWL(uint32_t old, uint32_t mem, uint32_t address)
		{
			const uint32_t shift = (address & 3) << 3;
			return (old & (0x00FFFFFFu >> shift)) | (mem << (24 - shift));
		}
		PS2N_IOP_OP uint32_t LWR(uint32_t old, uint32_t mem, uint32_t address)
		{
			const uint32_t shift = (address & 3) << 3;
			return (old & (0xFFFFFF00u << (24 - shift))) | (mem >> shift);
		}
		PS2N_IOP_OP uint32_t SWL(uint32_t reg, uint32_t mem, uint32_t address)
		{
			const uint32_t shift = (address & 3) << 3;
			return (reg >> (24 - shift)) | (mem & (0xFFFFFF00u << shift));
		}
		PS2N_IOP_OP uint32_t SWR(uint32_t reg, uint32_t mem, uint32_t address)
		{
			const uint32_t shift = (address & 3) << 3;
			return (reg << shift) | (mem & (0x00FFFFFFu >> (24 - shift)));
		}

		// Whether a conditional branch is taken.
		PS2N_IOP_OP bool Taken(r3000a::Op op, uint32_t s, uint32_t t)
		{
			switch (op)
			{
				case r3000a::Op::BEQ: return s == t;
				case r3000a::Op::BNE: return s != t;
				case r3000a::Op::BLEZ: return static_cast<int32_t>(s) <= 0;
				case r3000a::Op::BGTZ: return static_cast<int32_t>(s) > 0;
				case r3000a::Op::BLTZ:
				case r3000a::Op::BLTZAL: return static_cast<int32_t>(s) < 0;
				default: return static_cast<int32_t>(s) >= 0; // BGEZ, BGEZAL
			}
		}
	} // namespace val

	// RFE (rpsxRFE): the interrupt bits of the status register go back one
	// place, and the core looks whether an interrupt is now due.
	PS2N_IOP_OP void RFE()
	{
		const uint32_t status = psxRegs.CP0.n.Status;
		psxRegs.CP0.n.Status = (status & 0xFFFFFFF0u) | ((status & 0x3C) >> 2);
		iopTestIntc();
	}

	// ---------------------------------------------------------------------
	// How a block ends.

	inline constexpr int kOver = 0; // the IOP's stretch of time is over
	inline constexpr int kOn = 1; // the IOP goes on at the block's target
	inline constexpr int kOnTested = 2; // it goes on after an event test, which may have sent it elsewhere (`psxRegs.pc`)

	// SUB [iopCycleEE], cost; JLE: taken when what it held was no more than
	// what was taken from it.
	PS2N_IOP_OP bool TakeEECycles(uint32_t cost)
	{
		const int32_t before = psxRegs.iopCycleEE;
		psxRegs.iopCycleEE = static_cast<int32_t>(static_cast<uint32_t>(before) - cost);
		return before <= static_cast<int32_t>(cost);
	}

	// Where a block ends at a branch or jump (iPsxBranchTest): its cycles are
	// added, the EE's clock is charged eight for each, and unless that ends
	// the stretch an event test follows when one is due.
	PS2N_IOP_OP int BranchTest(uint32_t cycles)
	{
		const uint64_t now = psxRegs.cycle + cycles;
		psxRegs.cycle = now;
		if (TakeEECycles(cycles * 8))
			return kOver;
		if (static_cast<int64_t>(now - psxRegs.iopNextEventCycle) < 0) [[likely]]
			return kOn;
		iopEventTest();
		return kOnTested;
	}

	// The same where a wait loop's branch is taken, with PCSX2's WaitLoop
	// setting: time passes to the next event, or as far as the EE's clock
	// lets it, and an event test follows whatever the time.
	PS2N_IOP_OP int WaitLoopTest()
	{
		g_counters.waits++;
		const uint64_t before = psxRegs.cycle;
		// MOV rdx, [iopCycleEE]: 64 bits, of which the upper half is the field after it.
		uint64_t give = static_cast<uint64_t>(static_cast<uint32_t>(psxRegs.iopCycleEE)) |
						(static_cast<uint64_t>(psxRegs.iopCycleEECarry) << 32);
		give = (give + 7) >> 3;
		uint64_t now = before + give;
		if (static_cast<int64_t>(now - psxRegs.iopNextEventCycle) >= 0)
			now = psxRegs.iopNextEventCycle;
		psxRegs.cycle = now;
		if (TakeEECycles(static_cast<uint32_t>((now - before) << 3)))
			return kOver;
		iopEventTest();
		return kOnTested;
	}

	// Where a block was cut short, and runs on into the next: its cycles are
	// added and charged, and nothing is tested.
	PS2N_IOP_OP void RunOn(uint32_t cycles)
	{
		psxRegs.cycle += cycles;
		psxRegs.iopCycleEE = static_cast<int32_t>(static_cast<uint32_t>(psxRegs.iopCycleEE) - cycles * 8);
	}

	// SYSCALL or BREAK at `pc` (rpsxSYSCALL, rpsxBREAK): the exception is
	// raised, and the block is left with the cycles of its instructions so
	// far, this one included. True: left.
	PS2N_IOP_OP bool Exception(uint32_t code, uint32_t pc, uint32_t cause, uint32_t cycles_so_far)
	{
		g_counters.exceptions++;
		psxRegs.code = code;
		psxRegs.pc = pc;
		psxException(cause, 0);
		if (psxRegs.pc == pc)
			return false;
		RunOn(cycles_so_far);
		return true;
	}

	// `addiu $zero, $zero, n` at `pc`, the block's import number `slot`
	// (psxRecompileIrxImport): where the core has a function for the import,
	// that function is called; true when it did the import's work itself and
	// the IOP goes on where it says (`psxRegs.pc`), nothing of the block's
	// time counted.
	PS2N_IOP_OP bool Import(const Block* b, uint32_t slot, uint32_t code, uint32_t pc)
	{
		const irxHLE hle = b->imports[slot];
		if (!hle) [[likely]]
			return false;
		g_counters.imports++;
		psxRegs.code = code;
		psxRegs.pc = pc + 4;
		if (hle() == 0)
			return false;
		g_counters.importsDone++;
		return true;
	}
} // namespace ps2n::iop
