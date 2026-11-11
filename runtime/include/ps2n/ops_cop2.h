// SPDX-License-Identifier: GPL-3.0-or-later
//
// COP2 (VU0 in macro mode): the two transfers between a general register and
// a VU0 integer or control register, CFC2 and CTC2. Every other COP2
// instruction is still carried out by PCSX2's interpreter function for it.
//
// What the two agree with, by the rule the other families follow (PCSX2's EE
// recompiler; the console where a recording shows the recompiler wrong and
// PCSX2's interpreter right). The recording is ps2autotests
// cpu/vu0_macro/transfer; the recompiler's code is recCFC2 and recCTC2 in
// pcsx2/x86/microVU_Macro.inl, the interpreter's CFC2 and CTC2 in
// pcsx2/VU0.cpp (PCSX2 v2.9.170, Copyright 2002-2026 PCSX2 Dev Team,
// GPL-3.0+).
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
//
// Following the interpreter, where the recompiler differs:
//  - CTC2 to FBRST stores the value and then carries out the resets it asks
//    for, which clear the reset VU's half of the register (the console
//    agrees: it reads back 0 after writing all ones). The recompiler resets
//    first and stores afterwards.
//  - CTC2 to the clip flag register also sets the copy of the clip flag that
//    PCSX2's interpreter functions work on, because the VU0 macro
//    instructions are still carried out by those functions.
//  - VU0 is brought up to date first, always: it runs until it has caught up
//    with the EE, and with the interlock bit set until its micro program has
//    ended (CFC2) or has ended or stopped at an M bit (CTC2). The recompiler
//    leaves this out wherever its analysis of the block shows that no micro
//    program can be running. With VU0 idle the two are the same.
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
	// 16 to 31 a control register. interlock: bit 0 of the instruction.
	void CFC2(uint32_t rt, uint32_t fs, bool interlock);
	void CTC2(uint32_t rt, uint32_t fs, bool interlock);
} // namespace ps2n::ee::ops
