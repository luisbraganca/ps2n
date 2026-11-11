// SPDX-License-Identifier: GPL-3.0-or-later
// Part of the semantic call table; see r5900_sem.inc for the format.
//
// COP2: the transfers between a general register and a VU0 integer or control
// register. The rest of COP2 is in r5900_sem_cop2.inc.

S(CFC2, CFC2, (RT, FS, INTERLOCK))
S(CTC2, CTC2, (RT, FS, INTERLOCK))
