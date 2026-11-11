// SPDX-License-Identifier: GPL-3.0-or-later
// Part of the semantic call table; see r5900_sem.inc for the format.
//
// COP1: the floating-point unit, with its load and store.

S(MFC1, MFC1, (RT, FS))
S(MTC1, MTC1, (RT, FS))
S(CFC1, CFC1, (RT, FS))
S(CTC1, CTC1, (RT, FS))
S(ADD_S, ADD_S, (FD, FS, FT))
S(SUB_S, SUB_S, (FD, FS, FT))
S(MUL_S, MUL_S, (FD, FS, FT))
S(DIV_S, DIV_S, (FD, FS, FT))
S(SQRT_S, SQRT_S, (FD, FT))
S(ABS_S, ABS_S, (FD, FS))
S(MOV_S, MOV_S, (FD, FS))
S(NEG_S, NEG_S, (FD, FS))
S(RSQRT_S, RSQRT_S, (FD, FS, FT))
S(ADDA_S, ADDA_S, (FS, FT))
S(SUBA_S, SUBA_S, (FS, FT))
S(MULA_S, MULA_S, (FS, FT))
S(MADD_S, MADD_S, (FD, FS, FT))
S(MSUB_S, MSUB_S, (FD, FS, FT))
S(MADDA_S, MADDA_S, (FS, FT))
S(MSUBA_S, MSUBA_S, (FS, FT))
S(CVT_W_S, CVT_W_S, (FD, FS))
S(CVT_S_W, CVT_S_W, (FD, FS))
S(MAX_S, MAX_S, (FD, FS, FT))
S(MIN_S, MIN_S, (FD, FS, FT))
S(C_F_S, C_F_S, (FS, FT))
S(C_EQ_S, C_EQ_S, (FS, FT))
S(C_LT_S, C_LT_S, (FS, FT))
S(C_LE_S, C_LE_S, (FS, FT))

// load / store
S(LWC1, LWC1, (FT, RS, SIMM))
S(SWC1, SWC1, (FT, RS, SIMM))
