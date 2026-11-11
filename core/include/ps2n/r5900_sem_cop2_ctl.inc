// SPDX-License-Identifier: GPL-3.0-or-later
// Part of the semantic call table; see r5900_sem.inc for the format.
//
// COP2: the transfers between VU0's registers and the EE's general registers
// or memory. Each row is the move alone; what is done about VU0 first is
// decided for each instruction from its block (ps2n/ops_cop2.h), so generated
// code does not call these but hands the instruction to the runtime
// (PS2N_COP2). The rest of COP2 is in r5900_sem_cop2.inc.

S(QMFC2, QMFC2, (RT, FS))
S(CFC2, CFC2, (RT, FS))
S(QMTC2, QMTC2, (RT, FS))
S(CTC2, CTC2, (RT, FS))
S(LQC2, LQC2, (FT, RS, SIMM))
S(SQC2, SQC2, (FT, RS, SIMM))
