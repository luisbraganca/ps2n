// SPDX-License-Identifier: GPL-3.0-or-later
// Part of the semantic call table; see r5900_sem.inc for the format.
//
// COP0: the moves between a general register and a COP0 register or a
// performance counter; and the hints to the processor's cache and pipeline,
// which do nothing.
//
// The TLB instructions have no row: they are PCSX2's own functions in both of
// its providers. ERET and EI end a block and are handled by each consumer.
// DI's row is what it does; each consumer sees to when (ops_cop0.h).

S(MFC0, MFC0, (RT, CREG))
S(MTC0, MTC0, (RT, CREG))
S(MFPS, MFPS, (RT, PERF))
S(MFPC, MFPC, (RT, PERF))
S(MTPS, MTPS, (RT, PERF))
S(MTPC, MTPC, (RT, PERF))
S(SYNC, HINT, ())
S(PREF, HINT, ())
S(CACHE, HINT, ())
S(DI, DI, ())
