// SPDX-License-Identifier: GPL-3.0-or-later
// Semantic call table: which runtime function implements each non-control-
// flow instruction, and which decoded fields it takes, in order.
//
// S(Op, function, (args...))
//
// The recompiler turns a row into source text ("ADDIU(SP, SP, -32)") and the
// runtime interpreter into a call (ps2::ops::ADDIU(in.rt(), in.rs(), in.simm())),
// so both execute exactly the same code for every instruction.
//
// Argument tokens:
//   RD RS RT SA        GPR fields (SA is the 5-bit shift amount)
//   SIMM UIMM IMM5     sign-extended 16-bit / zero-extended 16-bit / signed 5-bit immediates
//   FD FS FT           FPU or VU register numbers (sa / rd / rt fields)
//   DEST BC FSF FTF    VU dest mask, broadcast component, fs/ft component selectors
//   VUFLAGS            where a VU0 instruction's flags reach VU0's flag registers (ps2n/ops_vu0.h)
//   PERF               perf-counter register selector
//   CREG               the number of a COP0 register (rd field)
//
// Instructions without a row (branches, jumps, SYSCALL, BREAK, traps, ERET,
// SYNC/CACHE/PREF) are handled by the control-flow code of each consumer;
// VCALLMS and VCALLMSR by PCSX2's own function (r5900_sem_cop2.inc).


#include "ps2n/r5900_sem_int.inc"
#include "ps2n/r5900_sem_mmi.inc"
#include "ps2n/r5900_sem_cop0.inc"
#include "ps2n/r5900_sem_cop1.inc"
#include "ps2n/r5900_sem_cop2_ctl.inc"
#include "ps2n/r5900_sem_cop2.inc"
