// SPDX-License-Identifier: GPL-3.0-or-later
// Part of the semantic call table; see r5900_sem.inc for the format.
//
// COP0: system control.

S(MFC0, MFC0, (RT, RD))
S(MTC0, MTC0, (RT, RD))
S(MFPS, MFPS, (RT, PERF))
S(MFPC, MFPC, (RT, PERF))
S(MTPS, MTPS, (RT, PERF))
S(MTPC, MTPC, (RT, PERF))
S(TLBR, TLBR, ())
S(TLBWI, TLBWI, ())
S(TLBWR, TLBWR, ())
S(TLBP, TLBP, ())
S(EI, EI, ())
S(DI, DI, ())
