// SPDX-License-Identifier: GPL-3.0-or-later
//
// How many cycles each instruction is counted as taking.
//
// The values are those of PCSX2's opcode table (the Cycles namespace and the
// MakeOpcode rows of pcsx2/R5900OpcodeTables.cpp, PCSX2 v2.9.170, Copyright
// 2002-2026 PCSX2 Dev Team, GPL-3.0+), in its unit of an eighth of a cycle.
// They are part of how the PCSX2 core keeps time, so PS2Native's recompiler
// must count the same. The host's self-test compares this function with
// PCSX2's table for every instruction.
#include "ps2n/r5900.h"

namespace ps2n::r5900 {

uint8_t cycles(Op op) {
    switch (op) {
    case Op::MULT: case Op::MULTU: case Op::MULT1: case Op::MULTU1:
    case Op::MADD: case Op::MADDU: case Op::MADD1: case Op::MADDU1:
        return 2 * 8;
    case Op::DIV: case Op::DIVU: case Op::DIV1: case Op::DIVU1:
        return 14 * 8;

    case Op::PMADDH: case Op::PHMADH: case Op::PMSUBH: case Op::PHMSBH: case Op::PMULTH:
    case Op::PMADDW: case Op::PMSUBW: case Op::PMFHI: case Op::PMFLO: case Op::PMULTW:
    case Op::PMADDUW: case Op::PMULTUW:
        return 3 * 8;
    case Op::PDIVUW: case Op::PDIVW: case Op::PDIVBW:
        return 22 * 8;

    case Op::MUL_S: case Op::MULA_S: case Op::MADD_S: case Op::MSUB_S: case Op::MADDA_S: case Op::MSUBA_S:
        return 4 * 8;
    case Op::DIV_S: case Op::SQRT_S:
        return 6 * 8;
    case Op::RSQRT_S:
        return 8 * 8;

    // Encoded among the multimedia instructions, counted as ordinary ones.
    case Op::MFHI1: case Op::MTHI1: case Op::MFLO1: case Op::MTLO1:
        return 9;

    case Op::Invalid: case Op::Count:
        return 9;
    default:
        break;
    }

    const uint32_t f = info(op).flags;
    if (f & (F_LOAD | F_STORE)) return 14;    // also the FPU's and VU0's
    if (f & F_VU0) return 9;                  // PCSX2 counts every COP2 instruction alike, its branches too
    if (f & (F_BRANCH | F_TRAP)) return 11;   // not the jumps
    if (f & (F_COP0 | F_FPU)) return 7;
    if (f & F_MMI) return 14;
    return 9;
}

} // namespace ps2n::r5900
