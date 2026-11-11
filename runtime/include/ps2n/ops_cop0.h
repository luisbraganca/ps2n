// SPDX-License-Identifier: GPL-3.0-or-later
//
// COP0, the system control coprocessor: the moves between a general register
// and a COP0 register or a performance counter. Also the three instructions
// that only advise the processor's cache and pipeline (SYNC, PREF, CACHE),
// which do nothing here.
//
// What they agree with, by the rule the other families follow (PCSX2's EE
// recompiler; the console where a recording shows the recompiler wrong and
// PCSX2's interpreter right). No console recording covers these. The
// recompiler's code is recMFC0 and recMTC0 in pcsx2/x86/iCOP0.cpp and recSYNC,
// recPREF and recCACHE in pcsx2/x86/iR5900Misc.cpp; the interpreter's is in
// pcsx2/COP0.cpp, pcsx2/R5900OpcodeImpl.cpp and pcsx2/Cache.cpp (PCSX2
// v2.9.170, Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+).
//
// Following the recompiler, where PCSX2's interpreter differs:
//  - MFC0 of the status register gives the register as it was written. The
//    interpreter clears the bits the console does not implement (it gives
//    the value and 0xF0C79C1F).
//  - CACHE does nothing. The interpreter's function works on a model of the
//    processor's cache, which nothing else uses unless PCSX2's cache emulation
//    is switched on (no configuration here does that), and for two of the
//    cache operations it loads COP0's TagLo register from that model.
//  - MFC0 of the count register brings the register up to date with a 64-bit
//    addition, of which the register is the low half. The high half is the
//    register after it, EntryHi: when the count overflows, about every
//    fourteen seconds, EntryHi goes up by one. That is a defect of the
//    recompiler (xADD(ptr[&cpuRegs.CP0.n.Count], rax) in recMFC0), not of
//    the console, and the interpreter does not have it. It is reproduced
//    because games are tested against it, and so that a run can be compared
//    with a stock run register for register (ADR 0011).
//  - The interpreter never lets MFC0 find the count register unchanged: read
//    twice at one cycle count, it has moved on by one. The recompiler has no
//    such rule, and needs none: see below.
//
// The recompiler adds the cycles of the block so far to the cycle count before
// it reads or writes the count register or a performance counter and before it
// writes the status register or the performance control register. That is not
// done here but by whoever calls these functions (ps2n::r5900::flushesCycles,
// PS2N_FLUSH): they find the cycle count already brought up to date.
//
// TLBR, TLBWI, TLBWR and TLBP are PCSX2's own functions in both of its
// providers and are called here as well. ERET and EI are not in this header:
// they end a block (ps2n::r5900::isSystem).
//
// DI() is what the instruction does, which PCSX2's interpreter and recompiler
// agree on. When it does it is another matter: under the recompiler DI takes
// effect after the instruction that follows it, unless it is itself in a
// delay slot, and not at all if that instruction is a branch (recDI). That is
// the business of whoever calls this: runtime/rec_blocks.cpp for the
// interpreter, emitAt() in recompiler/src/codegen.cpp for compiled code.
#pragma once

#include "ps2n/ee_state.h"

#include "COP0.h"

#include <cstdint>

namespace ps2n::ee::ops
{
	// SYNC, PREF and CACHE.
	inline void HINT() {}

	// DI: clears the status register's EIE bit, if the processor is in kernel
	// mode or one of EXL, ERL and EDI is set. See above for when.
	inline void DI()
	{
		const uint32_t status = cpuRegs.CP0.n.Status.val;
		if ((status & 0x20006u) || !(status & 0x18u))
			cpuRegs.CP0.n.Status.val = status & ~0x10000u;
	}

	// rt: general register. rd: COP0 register, never 25 (the decoder gives the
	// performance counters their own instructions).
	inline void MFC0(uint32_t rt, uint32_t rd)
	{
		if (rd == 9)
		{
			// The count register is brought up to date when it is read, whether
			// or not the value goes anywhere. The recompiler: a 64-bit addition
			// over the count register and EntryHi, which follows it.
			const uint64_t sum = (static_cast<uint64_t>(cpuRegs.CP0.n.EntryHi) << 32 | cpuRegs.CP0.n.Count) + (cpuRegs.cycle - cpuRegs.lastCOP0Cycle);
			cpuRegs.CP0.n.Count = static_cast<uint32_t>(sum);
			cpuRegs.CP0.n.EntryHi = static_cast<uint32_t>(sum >> 32);
			cpuRegs.lastCOP0Cycle = cpuRegs.cycle;
		}

		// 24: the breakpoint registers, which PCSX2 does not have.
		if (rt == 0 || rd == 24)
			return;
		cpuRegs.GPR.r[rt].SD[0] = static_cast<int32_t>(cpuRegs.CP0.r[rd]); // the recompiler: unmasked
	}

	inline void MTC0(uint32_t rt, uint32_t rd)
	{
		const uint32_t value = cpuRegs.GPR.r[rt].UL[0];
		switch (rd)
		{
			case 9:
				cpuRegs.lastCOP0Cycle = cpuRegs.cycle;
				cpuRegs.CP0.n.Count = value;
				break;
			case 12:
				// Brings the performance counters up to date, and has the events
				// tested within four cycles: an interrupt may now be enabled.
				WriteCP0Status(value);
				break;
			case 16:
				WriteCP0Config(value);
				break;
			case 24:
				break;
			default:
				cpuRegs.CP0.r[rd] = value;
				break;
		}
	}

	// The performance counters. `reg`: bits 1 to 5 of the instruction. Bit 0 of
	// the instruction, which the decoder has already used, tells the control
	// register (MFPS, MTPS) from the two counters (MFPC, MTPC).
	inline void MFPS(uint32_t rt, uint32_t)
	{
		if (rt)
			cpuRegs.GPR.r[rt].SD[0] = static_cast<int32_t>(cpuRegs.PERF.n.pccr.val);
	}

	inline void MFPC(uint32_t rt, uint32_t reg)
	{
		if (rt == 0)
			return;
		COP0_UpdatePCCR();
		cpuRegs.GPR.r[rt].SD[0] = static_cast<int32_t>((reg & 1) ? cpuRegs.PERF.n.pcr1 : cpuRegs.PERF.n.pcr0);
	}

	inline void MTPS(uint32_t rt, uint32_t reg)
	{
		// Only with register number 0 does it do anything.
		if (reg != 0)
			return;
		COP0_UpdatePCCR();
		cpuRegs.PERF.n.pccr.val = cpuRegs.GPR.r[rt].UL[0];
		COP0_DiagnosticPCCR();
	}

	inline void MTPC(uint32_t rt, uint32_t reg)
	{
		const uint32_t value = cpuRegs.GPR.r[rt].UL[0];
		if (reg & 1)
		{
			cpuRegs.PERF.n.pcr1 = value;
			cpuRegs.lastPERFCycle[1] = cpuRegs.cycle;
		}
		else
		{
			cpuRegs.PERF.n.pcr0 = value;
			cpuRegs.lastPERFCycle[0] = cpuRegs.cycle;
		}
	}
} // namespace ps2n::ee::ops
