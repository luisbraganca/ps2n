// SPDX-License-Identifier: GPL-3.0-or-later
//
// COP2 (VU0 in macro mode): the VU0 instructions in EE code, as PS2Native
// carries them out. The transfers between VU0's registers and the EE's are in
// ops_cop2.h.
//
// What they agree with, by the rule the other families follow (ADR 0005):
// PCSX2's EE recompiler, whose code for these instructions is microVU's in
// macro mode: REC_COP2_mVU0, setupMacroOp and endMacroOp in
// pcsx2/x86/microVU_Macro.inl, the instructions in microVU_Upper.inl and
// microVU_Lower.inl, the clamps in microVU_Clamp.inl, the arithmetic in
// microVU_Misc.inl, the register allocator in microVU_IR.h (PCSX2 v2.9.170,
// Copyright 2002-2026 PCSX2 Dev Team, GPL-3.0+). The host's SSE instructions
// are those microVU emits, in the same order on the same operands, under the
// same control register (the EE's), so that a result is the recompiler's to
// the bit. The self-test holds every instruction here to the recompiler, in
// each of PCSX2's VU clamp modes.
//
// What that means where it is not the console:
//  - Arithmetic rounds and flushes denormals as the host does under the EE's
//    control register, not as VU0 does.
//  - Overflow and underflow are never flagged (PCSX2's VU overflow game fix
//    flags results of the largest magnitude and above as overflowing, as
//    there).
//  - Operands and results are clamped to the largest float, or not, as the VU
//    clamp mode says (None, Normal, Extra, Extra + Preserve Sign), and not
//    where microVU does not: an operand read from VF00, and a result
//    computed into the register such an operand was read into.
//  - VSUB and VSUBA of a register from itself give +0 whatever it holds.
//  - VMAX and VMINI order floats as signed magnitudes, so -0 is below +0 and a
//    NaN is above every number of its sign; of two equal values, the one from
//    ft.
//  - VDIV, VSQRT and VRSQRT: a zero divisor gives the largest float with the
//    sign of the quotient, and the square root is of the magnitude. The
//    invalid and division flags are as microVU sets them: VSQRT of a negative
//    number, -0 included, is invalid; VRSQRT by a zero of either sign is a
//    division by zero, or invalid when the dividend is zero too, and the
//    invalid flag of a negative divisor is not kept then.
//  - VCLIP reads a denormal component as +0.
//  - VOPMSUB with a single component in its mask computes that component
//    from x's product; VOPMULA with a single component gives +0 there. (Its
//    result in the recompiler depends on which registers its allocator holds
//    from the instructions before; this is what it gives when it holds none.)
//  - The integer registers are 16 bits wide. VLQD and VSQD with VI00 as the
//    address register reach VU1's registers, as with any address from 0x400
//    up.
//  - With PCSX2's I-bit game fix, VIADDI adds the immediate of the first word
//    of VU0's micro memory instead of its own.
//
// Where the flags of an instruction reach VU0's flag registers is not decided
// here: PCSX2's recompiler decides it for each instruction from its block
// (runtime/rec_blocks.h), and the caller passes the decision on. The status
// flag register is kept as the console has it throughout, where microVU may
// leave it in a layout of its own between two instructions of a block (the
// execution model, "What is not reproduced", says when that could show).
//
// VCALLMS and VCALLMSR are not here: PCSX2's recompiler calls PCSX2's own
// function for them, and so does PS2Native (ADR 0016).
#pragma once

#include <cstdint>

namespace ps2n::ee::vu0
{
	// Where the flags an instruction computes go.
	enum : uint32_t
	{
		kStatus = 1, // the status flag register takes them
		kMac = 2, // the MAC flag register takes them
	};
} // namespace ps2n::ee::vu0

// The functions of the semantic call table (ps2n/r5900_sem_cop2.inc). Register
// numbers are as the instruction holds them: fd, fs and ft are VU0
// floating-point registers, and for the integer instructions the integer
// registers id, is and it. `dest`: the component mask, bit 3 for x to bit 0
// for w. `bc`: the broadcast component, 0 for x to 3 for w. `fsf`, `ftf`: a
// single component, likewise. `flags`: where the flags reach (vu0::kStatus,
// vu0::kMac).
namespace ps2n::ee::ops
{
	void VADD(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VSUB(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMUL(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMADD(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMSUB(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMAX(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VMINI(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);
	void VOPMSUB(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft);

	void VADDbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VSUBbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMULbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMADDbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMSUBbc(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMAXbc(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMINIbc(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc);

	void VADDq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VSUBq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMULq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMADDq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMSUBq(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);

	void VADDi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VSUBi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMULi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMADDi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMSUBi(uint32_t flags, uint32_t dest, uint32_t fd, uint32_t fs);
	void VMAXi(uint32_t dest, uint32_t fd, uint32_t fs);
	void VMINIi(uint32_t dest, uint32_t fd, uint32_t fs);

	void VADDA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VSUBA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VMULA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VMADDA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VMSUBA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);
	void VOPMULA(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft);

	void VADDAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);
	void VSUBAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMULAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMADDAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);
	void VMSUBAbc(uint32_t flags, uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc);

	void VADDAq(uint32_t flags, uint32_t dest, uint32_t fs);
	void VSUBAq(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMULAq(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMADDAq(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMSUBAq(uint32_t flags, uint32_t dest, uint32_t fs);

	void VADDAi(uint32_t flags, uint32_t dest, uint32_t fs);
	void VSUBAi(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMULAi(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMADDAi(uint32_t flags, uint32_t dest, uint32_t fs);
	void VMSUBAi(uint32_t flags, uint32_t dest, uint32_t fs);

	void VABS(uint32_t dest, uint32_t ft, uint32_t fs);
	void VMOVE(uint32_t dest, uint32_t ft, uint32_t fs);
	void VMR32(uint32_t dest, uint32_t ft, uint32_t fs);
	void VFTOI0(uint32_t dest, uint32_t ft, uint32_t fs);
	void VFTOI4(uint32_t dest, uint32_t ft, uint32_t fs);
	void VFTOI12(uint32_t dest, uint32_t ft, uint32_t fs);
	void VFTOI15(uint32_t dest, uint32_t ft, uint32_t fs);
	void VITOF0(uint32_t dest, uint32_t ft, uint32_t fs);
	void VITOF4(uint32_t dest, uint32_t ft, uint32_t fs);
	void VITOF12(uint32_t dest, uint32_t ft, uint32_t fs);
	void VITOF15(uint32_t dest, uint32_t ft, uint32_t fs);
	void VCLIPw(uint32_t dest, uint32_t fs, uint32_t ft);

	void VDIV(uint32_t flags, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf);
	void VSQRT(uint32_t flags, uint32_t ft, uint32_t ftf);
	void VRSQRT(uint32_t flags, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf);
	void VWAITQ();
	void VNOP();

	void VIADD(uint32_t id, uint32_t is, uint32_t it);
	void VISUB(uint32_t id, uint32_t is, uint32_t it);
	void VIAND(uint32_t id, uint32_t is, uint32_t it);
	void VIOR(uint32_t id, uint32_t is, uint32_t it);
	void VIADDI(uint32_t it, uint32_t is, int32_t imm5);
	void VMTIR(uint32_t it, uint32_t fs, uint32_t fsf);
	void VMFIR(uint32_t dest, uint32_t ft, uint32_t is);
	void VLQI(uint32_t dest, uint32_t ft, uint32_t is);
	void VLQD(uint32_t dest, uint32_t ft, uint32_t is);
	void VSQI(uint32_t dest, uint32_t fs, uint32_t it);
	void VSQD(uint32_t dest, uint32_t fs, uint32_t it);
	void VILWR(uint32_t dest, uint32_t it, uint32_t is);
	void VISWR(uint32_t dest, uint32_t it, uint32_t is);
	void VRINIT(uint32_t fs, uint32_t fsf);
	void VRXOR(uint32_t fs, uint32_t fsf);
	void VRGET(uint32_t dest, uint32_t ft);
	void VRNEXT(uint32_t dest, uint32_t ft);
} // namespace ps2n::ee::ops
