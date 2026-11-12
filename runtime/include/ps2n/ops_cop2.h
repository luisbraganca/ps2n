// SPDX-License-Identifier: GPL-3.0-or-later
//
// COP2 (VU0 in macro mode): the transfers between VU0's registers and the
// EE's general registers or memory. CFC2 and CTC2 move a VU0 integer or
// control register; QMFC2 and QMTC2 move all 128 bits of a VU0 floating-point
// register; LQC2 and SQC2 load and store one. The VU0 instructions themselves
// are in ops_vu0.h.
//
// These functions are the move alone. Whether VU0 is brought up to date
// first, and how far, PCSX2's recompiler decides for each instruction from
// its whole block; PS2Native reproduces that decision where it carries the
// block out (runtime/rec_blocks.h, Cop2Prologue() in runtime/ee_provider.cpp),
// in the interpreter and for compiled code alike (PS2N_COP2).
//
// What they agree with, by the rule the other families follow (PCSX2's EE
// recompiler; the console where a recording shows the recompiler wrong and
// PCSX2's interpreter right). The recording is ps2autotests
// cpu/vu0_macro/transfer; the recompiler's code is recCFC2, recCTC2,
// recQMFC2, recQMTC2, recLQC2 and recSQC2 in pcsx2/x86/microVU_Macro.inl, the
// interpreter's functions of those names are in pcsx2/VU0.cpp (PCSX2
// v2.9.170, Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+).
//
// Following the recompiler, where PCSX2's interpreter differs:
//  - The integer registers VI1 to VI15 are 16 bits wide. CTC2 writes 16 bits
//    and CFC2 reads 16, zero-extended (the console agrees). The interpreter
//    writes and reads 32.
//  - CTC2 to the status flag register writes the six sticky flags (bits 6 to
//    11) and leaves the six current ones (the console agrees). The interpreter
//    replaces all 32 bits. As in the recompiler, the copies of the flags that
//    PCSX2's VU recompiler keeps for a micro program are rewritten as well.
//  - CFC2 of R gives 23 bits, zero-extended to 64. The interpreter leaves
//    bits 32 to 63 of the general register as they were.
//  - CTC2 to CMSAR1 starts the VU1 micro program at the address the 32-bit
//    value gives. The interpreter passes on its low 16 bits.
//  - LQC2 and SQC2 ignore the low four bits of the address. The interpreter
//    reads and writes sixteen bytes from the address as it is.
//
// Following the interpreter, where the recompiler differs:
//  - CTC2 to FBRST stores the value and then carries out the resets it asks
//    for, which clear the reset VU's half of the register (the console
//    agrees: it reads back 0 after writing all ones). The recompiler resets
//    first and stores afterwards.
//
// CTC2 to the clip flag register writes the register alone, as the
// recompiler does; PCSX2's interpreter also sets a copy of it that only its
// own VU0 functions read.
//
// Where neither matches the console, both do the same and so does this: the
// clip flag register keeps 32 bits (24 on the console), CMSAR0 keeps 32 (16),
// the reserved registers keep what was written.
//
// These are functions of the runtime library, not inline: they call into the
// core's VU0, and nothing in them folds away for constant register numbers
// except a switch.
#pragma once

#include <cstdint>

namespace ps2n::ee::ops
{
	// rt: general register. fs: VU0 register, 0 to 15 an integer register and
	// 16 to 31 a control register.
	void CFC2(uint32_t rt, uint32_t fs);
	void CTC2(uint32_t rt, uint32_t fs);

	// rt: general register. fs: VU0 floating-point register. A move to
	// register 0, which is constant, does nothing.
	void QMFC2(uint32_t rt, uint32_t fs);
	void QMTC2(uint32_t rt, uint32_t fs);

	// ft: VU0 floating-point register. rs: the general register that holds
	// the base address. A load into register 0 still reads memory.
	void LQC2(uint32_t ft, uint32_t rs, int32_t imm);
	void SQC2(uint32_t ft, uint32_t rs, int32_t imm);
} // namespace ps2n::ee::ops
