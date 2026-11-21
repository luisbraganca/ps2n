// SPDX-License-Identifier: GPL-3.0-or-later
//
// What PCSX2's EE recompiler decides about a COP2 instruction from the rest of
// its block: whether VU0 is brought up to date first (COP2MicroFinishPass),
// whether the instruction's flags reach VU0's status and MAC registers
// (COP2FlagHackPass, PCSX2's "mVU flag hack"), and from those whether the
// cycles of the block so far are added first; and where the scan for a
// block's end stops, the range both passes look at. The rules are those of
// pcsx2/x86/iR5900Analysis.cpp, the instruction recompilers of
// pcsx2/x86/microVU_Macro.inl and recRecompile() in
// pcsx2/x86/ix86-32/iR5900.cpp (PCSX2 v2.9.170, Copyright 2002-2026 PCSX2 Dev
// Team, GPL-3.0+).
//
// The passes look at a block's whole range, from its start to where the scan
// for its end stopped, and where that stops depends on which other blocks
// existed when the block was formed. The runtime runs them when it forms a
// block (runtime/rec_blocks.cpp). The recompiler runs them over every range a
// block of compiled code can have, and writes into the code what they decide
// where all of those agree (recompiler/src/codegen.cpp; docs/adr/0046); before
// compiled code runs a block, the runtime compares the two.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2n::r5900::cop2 {

// The bits of the runtime's record of an instruction (blocks::StepInfo in
// runtime/rec_blocks.h) that these decide for a COP2 instruction, and that
// compiled code acts on.
inline constexpr uint16_t kFlush = 1u << 2;    // the cycles of the block so far are added first
inline constexpr uint16_t kSync = 1u << 7;     // VU0 is brought up to date with the EE first
inline constexpr uint16_t kFinish = 1u << 8;   // VU0's micro program is run to its end first
inline constexpr uint16_t kStatus = 1u << 13;  // the instruction's flags reach VU0's status register
inline constexpr uint16_t kMac = 1u << 14;     // and its MAC register
inline constexpr uint16_t kDecided = kFlush | kSync | kFinish | kStatus | kMac;

// VU0's control registers the passes look for (pcsx2/VU.h).
inline constexpr uint32_t kRegStatus = 16;
inline constexpr uint32_t kRegMac = 17;
inline constexpr uint32_t kRegFbrst = 28;

// Which of VU0's flag registers the recompiler's code for a COP2 instruction
// can write: cop2flags() in pcsx2/x86/ix86-32/iR5900.cpp, as it is written.
inline constexpr int kWritesStatus = 1;
inline constexpr int kWritesMac = 2;
inline constexpr int kWritesClip = 4;
inline int flagsWritten(uint32_t code) {
    if ((code >> 26) != 022) return 0;          // not COP2
    if (((code >> 25) & 1) == 0) return 0;      // a branch or a transfer
    switch ((code >> 2) & 15) {
    case 15:
        switch ((code >> 6) & 0x1f) {
        case 4:   // ITOF
        case 5:   // FTOI
        case 12:  // MOVE, MR32
        case 13:  // LQI, SQI, LQD, SQD
        case 15:  // MTIR, MFIR, ILWR, ISWR
        case 16:  // RNEXT, RGET, RINIT, RXOR
            return 0;
        case 7:  // MULAq, ABS, MULAi, CLIP
            if ((code & 3) == 1) return 0;  // ABS
            if ((code & 3) == 3) return kWritesClip;  // CLIP
            return kWritesStatus | kWritesMac;
        case 11:  // SUBA, MSUBA, OPMULA, NOP
            if ((code & 3) == 3) return 0;  // NOP
            return kWritesStatus | kWritesMac;
        case 14:  // DIV, SQRT, RSQRT, WAITQ
            if ((code & 3) == 3) return 0;  // WAITQ
            return kWritesStatus;
        default: break;
        }
        break;
    case 4:   // MAXbc
    case 5:   // MINIbc
    case 12:  // IADD, ISUB, IADDI
    case 13:  // IAND, IOR
    case 14:  // VCALLMS, VCALLMSR
        return 0;
    case 7:
        if ((code & 1) == 1) return 0;  // MAXi, MINIi
        return kWritesStatus | kWritesMac;
    case 10:
        if ((code & 3) == 3) return 0;  // MAX
        return kWritesStatus | kWritesMac;
    case 11:
        if ((code & 3) == 3) return 0;  // MINI
        return kWritesStatus | kWritesMac;
    default: break;
    }
    return kWritesStatus | kWritesMac;
}

// COP2MicroFinishPass::Run over the words of a block's range, `n` of them:
// for each, kSync, kFinish or neither, in `out`. `full_vu0_sync` is PCSX2's
// FullVU0SyncHack (a gamefix of its GameDB's).
inline void microFinishPass(const uint32_t* words, size_t n, bool full_vu0_sync, uint16_t* out) {
    for (size_t i = 0; i < n; i++) out[i] = 0;

    bool needs_vu0_sync = true;
    bool needs_vu0_finish = true;
    bool block_interlocked = full_vu0_sync;

    for (size_t i = 0; i < n && !block_interlocked; i++) {
        const uint32_t code = words[i];
        const uint32_t rs = (code >> 21) & 31;
        if ((code >> 26) == 022 && (rs == 001 || rs == 002 || rs == 005 || rs == 006) && (code & 1)) block_interlocked = true;
    }

    for (size_t i = 0; i < n; i++) {
        const uint32_t code = words[i];
        const uint32_t opcode = code >> 26, rs = (code >> 21) & 31, rt = (code >> 16) & 31, funct = code & 63;

        // SB, SH, SW, SD may start VU0 through VIF0; VCALLMS and VCALLMSR do.
        if (opcode == 050 || opcode == 051 || opcode == 053 || opcode == 077 || (opcode == 022 && rs >= 020 && (funct == 070 || funct == 071))) {
            needs_vu0_sync = true;
            needs_vu0_finish = true;
            continue;
        }

        const bool is_lqc_sqc = (opcode == 066 || opcode == 076);
        const bool is_non_interlocked_move = (opcode == 022 && rs < 020 && ((code & 1) == 0));
        const bool likely_clear = opcode == 022 && rs < 020 && rs > 004 && rt == 000;
        if ((needs_vu0_sync && (is_lqc_sqc || is_non_interlocked_move)) || likely_clear) {
            bool following_needs_finish = false;
            for (size_t j = i + 1; j < n; j++) {
                const uint32_t code2 = words[j];
                if ((code2 >> 26) != 022) continue;
                const uint32_t rs2 = (code2 >> 21) & 31, funct2 = code2 & 63;
                if (rs2 >= 020 && (funct2 == 070 || funct2 == 071)) break;
                following_needs_finish = rs2 >= 020;
                if (following_needs_finish) break;
            }
            if (following_needs_finish && !block_interlocked) {
                out[i] |= kFinish;
                needs_vu0_sync = false;
                needs_vu0_finish = false;
            } else {
                out[i] |= kSync;
                needs_vu0_sync = block_interlocked || (is_non_interlocked_move && likely_clear);
                needs_vu0_finish = true;
            }
            continue;
        }

        if (opcode != 022) continue;

        if (rs >= 020 && needs_vu0_finish) {
            out[i] |= kFinish;
            needs_vu0_finish = false;
            needs_vu0_sync = false;
        } else if (needs_vu0_sync) {
            out[i] |= kSync;
            needs_vu0_sync = block_interlocked;
        }
    }
}

// COP2FlagHackPass::Run over the same words: kStatus and kMac, ORed into
// `out`. The recompiler's code for a VU0 instruction that writes the status
// or the MAC flags brings the register up to date only where something may
// read it afterwards: before a transfer of that register, before a micro
// program is called, before a store that may start one, and at the block's
// end. So of a run of such instructions only the last counts. (The clip
// flags are written by every instruction that writes them.)
inline void flagHackPass(const uint32_t* words, size_t n, uint16_t* out) {
    constexpr size_t kNone = ~size_t{0};
    size_t last_status = kNone, last_mac = kNone;
    size_t cfc2 = 0;  // the CFC2 of the status register that a CTC2 to it is paired with

    const auto commit_status = [&]() {
        if (last_status != kNone) out[last_status] |= kStatus;
    };
    const auto commit_mac = [&]() {
        if (last_mac != kNone) out[last_mac] |= kMac;
    };

    for (size_t i = 0; i < n; i++) {
        const uint32_t code = words[i];
        const uint32_t opcode = code >> 26, rs = (code >> 21) & 31, rd = (code >> 11) & 31, funct = code & 63;

        // SB, SH, SW: may start VU0 through VIF0.
        if (opcode == 050 || opcode == 051 || opcode == 053) {
            commit_status();
            commit_mac();
            continue;
        }
        if (opcode != 022) continue;

        // CTC2 to the status register, then CFC2 of it: every instruction in
        // between has to have its sticky flags right.
        if (rs == 006 && rd == kRegStatus) {
            cfc2 = i;
            for (size_t j = i; j < n; j++) {
                const uint32_t other = words[j];
                if ((other >> 26) == 022 && ((other >> 21) & 31) == 002 && ((other >> 11) & 31) == kRegStatus) {
                    cfc2 = j;
                    break;
                }
            }
        }

        // CFC2 and CTC2 of a flag register. (For FBRST the pass tests for CFC2
        // where its comment says CTC2; as it is written.)
        if (rs == 006 || rs == 002) {
            if (rd == kRegStatus) {
                commit_status();
            } else if (rd == kRegMac) {
                commit_mac();
            } else if (rd == kRegFbrst && rs == 002) {
                commit_status();
                commit_mac();
            }
        }

        // VCALLMS, VCALLMSR.
        if (((code >> 25) & 1) == 1 && ((code >> 2) & 15) == 14) {
            commit_status();
            commit_mac();
        }

        const int writes = flagsWritten(code);
        if (writes & kWritesStatus) {
            // Between a CTC2 and its CFC2, and always for VDIV, VSQRT and VRSQRT.
            const uint32_t sub_opcode = (code & 3) | ((code >> 4) & 0x7c);
            if (i < cfc2 || (rs >= 020 && funct >= 074 && sub_opcode >= 070 && sub_opcode <= 072)) out[i] |= kStatus;
            last_status = i;
        }
        if (writes & kWritesMac) last_mac = i;
    }

    commit_status();
    commit_mac();
}

// Whether the passes decide something about `code`: a COP2 instruction that
// is not a branch (BC2x), LQC2 or SQC2.
inline bool decided(uint32_t code) {
    const uint32_t opcode = code >> 26;
    return (opcode == 022 && ((code >> 21) & 31) != 010) || opcode == 066 || opcode == 076;
}

// What the recompiler decides about the COP2 instruction `code` (decided()),
// given what the passes said of it (`pass`): the kDecided bits of its record,
// as runtime/rec_blocks.cpp's Former::Compile sets them. `flag_hack` is
// PCSX2's vuFlagHack; without it the passes' kStatus and kMac are not looked
// at, and every VU0 instruction's flags reach the registers.
inline uint16_t stepInfo(uint32_t code, uint16_t pass, bool flag_hack) {
    const uint32_t opcode = code >> 26, rs = (code >> 21) & 31, rt = (code >> 16) & 31, rd = (code >> 11) & 31;
    uint16_t info = pass & (kSync | kFinish);
    if (opcode == 022 && rs < 020) {
        // The four transfers.
        if (code & 1) {
            // COP2_Interlock, which comes before anything else and acts only
            // where the instruction is marked for a sync.
            if (pass & kSync)
                info |= kFlush;
            else
                info &= ~kFinish;
        } else {
            // mVUSyncVU0 or mVUFinishVU0, which CFC2 and QMFC2 to $zero and
            // CTC2 and QMTC2 to register 0 return before.
            const bool to_nothing = (rs == 002 || rs == 001) ? (rt == 0) : (rd == 0);
            if (to_nothing)
                info &= ~(kSync | kFinish);
            else if (pass & kSync)
                info |= kFlush;
        }
    } else if (opcode != 022) {
        // LQC2, SQC2.
        if (pass & kSync) info |= kFlush;
    } else {
        // The VU0 instructions: recCOP2_SPEC1 runs VU0's micro program to its
        // end for either, and adds no cycles.
        if (pass & (kSync | kFinish)) info = static_cast<uint16_t>((info & ~kSync) | kFinish);
        // Whether its flags reach the registers (setupMacroOp).
        info |= flag_hack ? static_cast<uint16_t>(pass & (kStatus | kMac)) : static_cast<uint16_t>(kStatus | kMac);
        // VCALLMS, VCALLMSR: INTERPRETATE_COP2_FUNC.
        const uint32_t funct = code & 63;
        if (funct == 070 || funct == 071) info |= kFlush;
    }
    return info;
}

// Where the scan for the end of a block that begins at `start` stops
// (recRecompile's look-ahead), and where its branch goes: never across a 4 KB
// boundary, never into a block that exists already (`block_at(addr)`), not
// past memory it cannot read (`read(addr, &word)` false); after a branch or
// jump and its delay slot, or at the branch's target where a conditional
// branch goes back inside the block; after SYSCALL, BREAK and ERET.
// `visit(addr, word)` sees each word read.
struct BlockEnd {
    uint32_t end;
    uint32_t branch_to;  // where its branch or jump goes; 0xFFFFFFFF where it has none
};
template <class Read, class BlockAt, class Visit>
BlockEnd scanBlockEnd(uint32_t start, Read&& read, BlockAt&& block_at, Visit&& visit) {
    uint32_t i = start;
    uint32_t end = 0xffffffffu;
    uint32_t branch_to = 0xffffffffu;
    for (;;) {
        if (i != start && ((i & 0xffc) == 0 || block_at(i))) {
            end = i;
            break;
        }

        uint32_t code = 0;
        if (!read(i, &code)) {
            end = i;
            break;
        }
        visit(i, code);
        const uint32_t opcode = code >> 26, rs = (code >> 21) & 31, rt = (code >> 16) & 31, funct = code & 63;
        const int32_t imm = static_cast<int16_t>(code);

        bool found = false;
        const auto branch = [&]() {
            branch_to = static_cast<uint32_t>(imm * 4) + i + 4;
            end = (branch_to > start && branch_to < i) ? branch_to : i + 8;
            found = true;
        };
        switch (opcode) {
        case 0:  // special
            if (funct == 8 || funct == 9) {  // JR, JALR
                end = i + 8;
                found = true;
            } else if (funct == 12 || funct == 13) {  // SYSCALL, BREAK
                end = i + 4;
                found = true;
            }
            break;
        case 1:  // regimm
            if (rt < 4 || (rt >= 16 && rt < 20)) branch();
            break;
        case 2:  // J
        case 3:  // JAL
            branch_to = ((code & 0x03FFFFFFu) << 2) | ((i + 4) & 0xf0000000u);
            end = i + 8;
            found = true;
            break;
        case 4: case 5: case 6: case 7:
        case 20: case 21: case 22: case 23:
            branch();
            break;
        case 16:  // cp0
            if (rs == 16 && funct == 24) {  // eret
                end = i + 4;
                found = true;
                break;
            }
            [[fallthrough]];
        case 17:  // cp1
        case 18:  // cp2
            if (rs == 8) branch();
            break;
        default: break;
        }
        if (found) break;
        i += 4;
    }
    return {end, branch_to};
}

}  // namespace ps2n::r5900::cop2
