// SPDX-License-Identifier: GPL-3.0-or-later
//
// The steps a micro-code block's plan is made of (ps2n/vu_plan.h): for each
// its name and how many operands it has. What each does is its function of
// the same name in runtime/include/ps2n/vu_ops.h, whose parameters are the
// operands in this order; the comments here name them.
//
// Included with PS2N_VU_STEP(name, operands) defined; and, by whoever treats
// them apart, PS2N_VU_STEP_CONTROL(name, operands) for the steps that may
// end the block where they stand (TBit). The order is the numbering.
//
// "h": one of the host registers microVU keeps VI registers in, by its
// number; "h | none": that, or ps2n::vu::kNone for VI00, which reads as zero.
// "vi": the VI register the host register holds, whose low half the result
// is written to as well; 0 for none. "pair": an instruction pair of the
// block, by its index. "rel": a byte address as its distance from the
// block's start.

#ifndef PS2N_VU_STEP_CONTROL
#define PS2N_VU_STEP_CONTROL(name, operands) PS2N_VU_STEP(name, operands)
#define PS2N_VU_STEP_CONTROL_DEFAULTED
#endif

// ---- VI registers in host registers: the moves microVU's allocator makes --------------
PS2N_VU_STEP(ViLoad, 2) // h, vi: the register, zero-extended
PS2N_VU_STEP(ViZero, 1) // h
PS2N_VU_STEP(ViCopy, 2) // h to, h from: all 32 bits
PS2N_VU_STEP(ViCopyZ, 2) // h to, h from: the low half
PS2N_VU_STEP(ViZext, 1) // h: its high half cleared
PS2N_VU_STEP(ViBackup, 1) // h: kept for a branch (VIbackup)

// ---- results in host registers --------------------------------------------------------
PS2N_VU_STEP(ViSetT, 2) // h, vi: the value on its way (t0)
PS2N_VU_STEP(ViAddK, 3) // h, vi, a constant to add
PS2N_VU_STEP(ViAddLive, 4) // h, vi, pair, kind: an immediate read from micro memory as the code runs (0 + imm15, 1 - imm15, 2 + imm5)
PS2N_VU_STEP(ViAlu, 4) // h of is and the result, h of it, vi, 0 add 1 subtract 2 and 3 or 4 is itself
PS2N_VU_STEP(ViMove, 3) // h to, vi, h from
PS2N_VU_STEP(ViSext, 1) // h: sign-extended in place from its low half (MFIR)

// ---- a value on its way to a host register, or to a branch (t0) -------------------------
PS2N_VU_STEP(T0Const, 1) // the value
PS2N_VU_STEP(T0Live, 2) // pair, kind: an immediate read from micro memory (0 imm15, 2 imm5)
PS2N_VU_STEP(T0Link, 1) // rel: the pair a BAL or JALR returns to
PS2N_VU_STEP(T0LinkEvil, 1) // 0 from badBranch, 1 from evilBranch: the same after a branch in a delay slot
PS2N_VU_STEP(T0VfLane, 2) // VF register, component (MTIR)
PS2N_VU_STEP(T0Vif, 1) // 0 TOP, 1 VU0's ITOP, 2 VU1's ITOP
PS2N_VU_STEP(T0Rd16, 1) // component: 16 bits of data memory at the address (ea)
PS2N_VU_STEP(T0Jump, 1) // h | none: the address a JR or JALR goes to
PS2N_VU_STEP(T0FromH, 1) // h | none: the low half
PS2N_VU_STEP(T0Backup, 0) // the value kept for a branch
PS2N_VU_STEP(T0XorH, 1) // h
PS2N_VU_STEP(T0XorBackup, 0)
PS2N_VU_STEP(T0Fceq, 2) // clip flag instance, immediate
PS2N_VU_STEP(T0Fcand, 2)
PS2N_VU_STEP(T0Fcor, 2)
PS2N_VU_STEP(T0Fcget, 1) // clip flag instance
PS2N_VU_STEP(T0Fseq, 2) // status flag instance, the immediate in microVU's layout
PS2N_VU_STEP(T0Fsand, 2) // status flag instance, the immediate
PS2N_VU_STEP(T0Fsor, 2)

// ---- the flags ----------------------------------------------------------------------------
PS2N_VU_STEP(Fcset, 2) // clip flag instance written, immediate
PS2N_VU_STEP(Fsset, 4) // status flag instance written, the one before it, whether that is copied first, the sticky bits
PS2N_VU_STEP(Fmop, 4) // h, vi, MAC flag instance, 0 FMEQ 1 FMAND 2 FMOR
PS2N_VU_STEP(DivSet, 3) // status flag instance written, the one before it, whether that is copied first

// ---- branches -----------------------------------------------------------------------------
PS2N_VU_STEP(BranchT, 1) // which of branch (1), badBranch (2), evilBranch (4), evilevilBranch (8) take t0
PS2N_VU_STEP(BranchRel, 2) // which of them, rel: a B's or BAL's target
PS2N_VU_STEP(CondSet, 4) // 0 branch takes t0; else where each of two branches goes: 1 badBranch, 2 evilevilBranch, 3 evilBranch; the branch's opcode; rel taken; rel not taken

// ---- data memory ---------------------------------------------------------------------------
PS2N_VU_STEP(Ea, 5) // h | none, VU1, 0 the immediate given 1 read from micro memory, immediate, pair: the address of a quadword
PS2N_VU_STEP(EaH, 3) // h, VU1, sign-extended
PS2N_VU_STEP(EaZero, 1) // VU1
PS2N_VU_STEP(EaFixed, 0) // where microVU has VU1's LQD and SQD of VI00 go
PS2N_VU_STEP(LoadQ, 2) // VF register, components
PS2N_VU_STEP(StoreQ, 2) // VF register, components
PS2N_VU_STEP(Wr32, 2) // h, components: all 32 bits (ISW, ISWR)

// ---- the upper instructions ------------------------------------------------------------------
// "word": the instruction; "mode", "flags": what the analysis and the settings decided (vu_ops.h).
PS2N_VU_STEP(FMACa, 3) // word, mode, flags: ADD SUB MUL MAX MINI, and the first three into ACC
PS2N_VU_STEP(FMACb, 3) // MADDA MSUBA
PS2N_VU_STEP(FMACc, 3) // MADD
PS2N_VU_STEP(FMACd, 3) // MSUB
PS2N_VU_STEP(Opmsub, 3)
PS2N_VU_STEP(Opmula, 3)
PS2N_VU_STEP(Itof, 2) // word, the scale's bits or 0
PS2N_VU_STEP(Ftoi, 2)
PS2N_VU_STEP(Abs, 1) // word
PS2N_VU_STEP(Clip, 3) // word, clip flag instance before, instance written
PS2N_VU_STEP(IbitLive, 1) // pair: I from the lower word in micro memory
PS2N_VU_STEP(IbitConst, 1) // the value

// ---- the lower instructions on VF registers, Q, P and R ---------------------------------------
PS2N_VU_STEP(Move, 1) // word
PS2N_VU_STEP(Mr32, 1)
PS2N_VU_STEP(Div, 2) // word, mode
PS2N_VU_STEP(Sqrt, 2)
PS2N_VU_STEP(Rsqrt, 2)
PS2N_VU_STEP(Mfir, 2) // h | none, word
PS2N_VU_STEP(Mfp, 2) // word, which P
PS2N_VU_STEP(Rnext, 2) // word, whether the VF register is left alone
PS2N_VU_STEP(Rget, 1)
PS2N_VU_STEP(Rinit, 1)
PS2N_VU_STEP(Rxor, 1)
PS2N_VU_STEP(Efu, 3) // which, word, mode

// ---- the rest -------------------------------------------------------------------------------
PS2N_VU_STEP(VfSave, 1) // VF register: kept as it was, for the upper instruction that runs second
PS2N_VU_STEP(VfSwap, 1) // VF register: the lower's result set aside, the register as it was
PS2N_VU_STEP(VfRestore, 1) // VF register: the lower's result
PS2N_VU_STEP(MFlag, 0) // the M bit: VU0's flag for the EE
PS2N_VU_STEP_CONTROL(TBit, 2) // which of the block's T bits, VU1: the program ends here if FBRST enables it
PS2N_VU_STEP(Xgkick, 0) // the GIF transfer of the XGKICK that was waiting
PS2N_VU_STEP(SetXgkick, 1) // h: the address of an XGKICK, to wait

// ---- the block's end: every plan's last step --------------------------------------------------
// How the program goes on: 0 it does not, the program stops or ends here (the E bit's end,
// an M bit) and the provider sees to it; 1 at the block a B or BAL leads to; 2 at one of two,
// by a conditional branch with this opcode; 3 where a JR or JALR goes; 4 where the branch
// went that had this block's pair in its delay slot's place.
// And what the block's link does first (mVUsetupBranch; step::Setup in vu_ops.h has the
// bits): the flags' instances put in the order the next block expects them in, P and Q
// exchanged with their other instances.
PS2N_VU_STEP_CONTROL(Leave, 3) // how, the conditional branch's opcode, what the link does first

#ifdef PS2N_VU_STEP_CONTROL_DEFAULTED
#undef PS2N_VU_STEP_CONTROL
#undef PS2N_VU_STEP_CONTROL_DEFAULTED
#endif
