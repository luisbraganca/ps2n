// SPDX-License-Identifier: GPL-3.0-or-later
//
// Idle loops ("wait loops").
//
// A program that waits for an interrupt often spins in a loop that does
// nothing else: the BIOS's idle thread, a game polling a flag. PCSX2's EE
// recompiler recognizes a block that jumps back to its own start and "does
// the same thing on every iteration", and instead of running it again and
// again moves the cycle count to the next event (its WaitLoop speed hack, on
// by default). PS2Native does the same, with the same test, for two reasons:
// time then passes as it does under the recompiler, and the BIOS, which is
// interpreted, does not burn host time idling.
//
// This is the test, transcribed from recRecompile() in
// pcsx2/x86/ix86-32/iR5900.cpp (PCSX2 v2.9.170, Copyright 2002-2026 PCSX2 Dev
// Team, GPL-3.0+), on the raw fields as it is there. The recompiler and the
// runtime's interpreter both use this one definition, so that compiled code
// and interpreted code skip at exactly the same places.
#pragma once

#include <cstdint>

namespace ps2n::r5900 {

// Feed it every instruction of a block except the branch that ends it (the
// delay slot included). `idle` then says whether the block may be skipped
// when that branch goes back to the block's first instruction.
struct WaitLoopScan {
    uint32_t reads = 0;   // registers read before the loop wrote them
    uint32_t loads = 1;   // registers holding a constant or a value loaded from memory; $zero is one
    bool idle = true;

    void add(uint32_t code) {
        if (!idle || code == 0) return;   // nop

        const uint32_t opcode = code >> 26;
        const uint32_t rs = 1u << ((code >> 21) & 31), rt = 1u << ((code >> 16) & 31), rd = 1u << ((code >> 11) & 31);
        const uint32_t funct = code & 63;

        if (opcode == 057 || (opcode == 0 && funct == 017)) return;   // CACHE, SYNC

        if ((opcode & 070) == 010 || (opcode & 076) == 030) {   // arithmetic with an immediate
            if (loads & rs) {
                loads |= rt;
                return;
            }
            reads |= rs;
            if (reads & rt) idle = false;
            return;
        }
        if (opcode == 0 && (funct & 060) == 040 && (funct & 076) != 050) {   // arithmetic on registers
            if ((loads & rs) && (loads & rt)) {
                loads |= rd;
                return;
            }
            reads |= rs | rt;
            if (reads & rd) idle = false;
            return;
        }
        if ((opcode & 070) == 040 || (opcode & 076) == 032 || opcode == 067) {   // loads
            if (loads & rs) {
                loads |= rt;
                return;
            }
            reads |= rs;
            if (reads & rt) idle = false;
            return;
        }
        if ((opcode & 074) == 020 && ((code >> 21) & 31) < 4) {   // moves from a coprocessor
            loads |= rt;
            return;
        }
        idle = false;   // anything else changes the machine
    }
};

} // namespace ps2n::r5900
