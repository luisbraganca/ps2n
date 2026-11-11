// SPDX-License-Identifier: GPL-3.0-or-later
//
// What PCSX2's EE recompiler decides about an instruction as it compiles a
// block, as far as it follows from the instruction and from what the block
// has made constant. Most of it is about constants: which general registers
// the recompiler knows the value of.
//
// Two things it does about time depend on that: a FlushCache call whose
// number it knows is not made, and a read of an EE timer register through an
// address it knows ends the block. A third does too: a load or store whose
// address it knows is compiled as a call or a plain access, any other as a
// direct access to host memory that faults, once, if the memory is a
// device's.
//
// The rules are those of its instruction recompilers and their templates
// (GPR_SET_CONST and GPR_DEL_CONST in pcsx2/x86/iR5900.h, eeRecompileCodeRC0,
// RC1 and RC2 in pcsx2/x86/ix86-32/iR5900Templates.cpp, iFlushCall in
// pcsx2/x86/ix86-32/iR5900.cpp; PCSX2 v2.9.170, Copyright 2002-2026 PCSX2 Dev
// Team, GPL-3.0+), as far as they can be said without its register
// allocator: an instruction it folds makes its result a constant; any other
// write to a register ends that; a call into its interpreter makes it forget
// every constant it has not yet written to the register in memory. Where its
// allocator has put a constant into a host register the constant survives
// such a call; that is not modelled.
//
// The runtime uses this when it forms a block (runtime/rec_blocks.cpp), and
// the recompiler to find the loads after which a block may end.
#pragma once

#include "ps2n/r5900.h"

#include <cstdint>

namespace ps2n::r5900 {

struct RecConsts {
    uint32_t has = 1;   // one bit a register; $zero always
    uint64_t value[32] = {};

    bool is(uint32_t r) const { return (has >> r) & 1; }
    void set(uint32_t r, uint64_t v) {
        if (r) {
            has |= 1u << r;
            value[r] = v;
        }
    }
    void del(uint32_t r) {
        if (r) has &= ~(1u << r);
    }
    void forget() { has = 1; }

    // What `in`, which is not a branch, a jump or an instruction that ends a
    // block, does to this knowledge.
    void step(const Insn& in) {
        const uint32_t rs = in.rs(), rt = in.rt(), rd = in.rd(), sa = in.sa();
        const uint64_t s = value[rs], t = value[rt];
        const uint64_t simm = static_cast<uint64_t>(static_cast<int64_t>(in.simm()));
        const uint64_t uimm = in.uimm();
        const bool cs = is(rs), ct = is(rt);
        const auto sext = [](uint32_t v) { return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v))); };
        const auto lo = [](uint64_t v) { return static_cast<uint32_t>(v); };
        const auto sl = [](uint64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); };
        const auto sd = [](uint64_t v) { return static_cast<int64_t>(v); };

        // rt = rs op immediate (eeRecompileCodeRC1).
        const auto rc1 = [&](uint64_t v) {
            if (!rt) return;
            if (cs) set(rt, v);
            else del(rt);
        };
        // rd = rs op rt (eeRecompileCodeRC0).
        const auto rc0 = [&](uint64_t v) {
            if (!rd) return;
            if (cs && ct) set(rd, v);
            else del(rd);
        };
        // rd = rt shifted by sa (eeRecompileCodeRC2).
        const auto rc2 = [&](uint64_t v) {
            if (!rd) return;
            if (ct) set(rd, v);
            else del(rd);
        };

        switch (in.op) {
        case Op::LUI: set(rt, sext(in.uimm() << 16)); return;

        case Op::ADDI: case Op::ADDIU: rc1(sext(lo(s) + lo(simm))); return;
        case Op::DADDI: case Op::DADDIU: rc1(s + simm); return;
        case Op::SLTI: rc1(sd(s) < sd(simm)); return;
        case Op::SLTIU: rc1(s < simm); return;
        case Op::ANDI: rc1(s & uimm); return;
        case Op::ORI: rc1(s | uimm); return;
        case Op::XORI: rc1(s ^ uimm); return;

        case Op::ADD: case Op::ADDU: rc0(sext(lo(s) + lo(t))); return;
        case Op::SUB: case Op::SUBU: rc0(sext(lo(s) - lo(t))); return;
        case Op::DADD: case Op::DADDU: rc0(s + t); return;
        case Op::DSUB: case Op::DSUBU: rc0(s - t); return;
        case Op::AND: rc0(s & t); return;
        case Op::OR: rc0(s | t); return;
        case Op::XOR: rc0(s ^ t); return;
        case Op::NOR: rc0(~(s | t)); return;
        case Op::SLT: rc0(sd(s) < sd(t)); return;
        case Op::SLTU: rc0(s < t); return;
        case Op::SLLV: rc0(sext(lo(t) << (s & 31))); return;
        case Op::SRLV: rc0(sext(lo(t) >> (s & 31))); return;
        case Op::SRAV: rc0(sext(static_cast<uint32_t>(sl(t) >> (s & 31)))); return;
        case Op::DSLLV: rc0(t << (s & 63)); return;
        case Op::DSRLV: rc0(t >> (s & 63)); return;
        case Op::DSRAV: rc0(static_cast<uint64_t>(sd(t) >> (s & 63))); return;

        // The low word of the product also goes to rd, when there is one.
        case Op::MULT: case Op::MULT1:
            rc0(sext(static_cast<uint32_t>(static_cast<int64_t>(sl(s)) * static_cast<int64_t>(sl(t)))));
            return;
        case Op::MULTU: case Op::MULTU1:
            rc0(sext(static_cast<uint32_t>(static_cast<uint64_t>(lo(s)) * static_cast<uint64_t>(lo(t)))));
            return;

        case Op::SLL: rc2(sext(lo(t) << sa)); return;
        case Op::SRL: rc2(sext(lo(t) >> sa)); return;
        case Op::SRA: rc2(sext(static_cast<uint32_t>(sl(t) >> sa))); return;
        case Op::DSLL: rc2(t << sa); return;
        case Op::DSRL: rc2(t >> sa); return;
        case Op::DSRA: rc2(static_cast<uint64_t>(sd(t) >> sa)); return;
        case Op::DSLL32: rc2(t << (sa + 32)); return;
        case Op::DSRL32: rc2(t >> (sa + 32)); return;
        case Op::DSRA32: rc2(static_cast<uint64_t>(sd(t) >> (sa + 32))); return;

        // recMOVZ, recMOVN: nothing at all when the move is known not to
        // happen or would move a register onto itself.
        case Op::MOVZ:
            if (rs == rd || (ct && t != 0)) return;
            rc0(s);
            return;
        case Op::MOVN:
            if (rs == rd || (ct && t == 0)) return;
            rc0(s);
            return;

        // Compiled as calls into PCSX2's interpreter, or with a call in them.
        case Op::MFPC:
            if (rt) forget();
            break;
        case Op::MTC0:
            if (rd == 12 || rd == 16) forget();
            break;
        case Op::MTPS:
            if (in.perfReg() == 0) forget();
            break;
        case Op::TLBR: case Op::TLBWI: case Op::TLBWR: case Op::TLBP:
            forget();
            break;

        default:
            // VCALLMS, VCALLMSR.
            if (in.opcode() == 022 && rs >= 020 && (in.funct() == 070 || in.funct() == 071)) forget();
            break;
        }

        // Anything else that writes a general register.
        const uint32_t f = in.flags();
        if (f & F_WRITES_RT) del(rt);
        if (f & F_WRITES_RD) del(rd);
    }

    // Whether the recompiler would know the address of the load or store
    // `in`, and if so the address.
    bool address(const Insn& in, uint32_t* out) const {
        if (!is(in.rs())) return false;
        *out = static_cast<uint32_t>(value[in.rs()]) + static_cast<uint32_t>(in.simm());
        return true;
    }
};

// The loads after which the recompiler ends a block when it knows the
// address to be an EE timer register's (recLoad).
inline bool isNarrowLoad(const Insn& in) {
    return in.op == Op::LB || in.op == Op::LBU || in.op == Op::LH || in.op == Op::LHU || in.op == Op::LW || in.op == Op::LWU;
}
inline bool isTimerAddress(uint32_t address) { return (address & 0xFFFFE000u) == 0x10000000u; }

// FlushCache and iFlushCache, by the low byte of the number in $v1: the
// system calls the recompiler does not make when it knows the number.
inline bool isSkippedSyscall(uint64_t v1) {
    const uint32_t number = static_cast<uint8_t>(v1);
    return number == 0x64 || number == 0x68;
}

// What the recompiler counts for such a call, in eighths of a cycle.
inline constexpr uint32_t kSkippedSyscallEighths = 5650;

// The COP0 instructions before which the recompiler adds the cycles of the
// block so far to the cycle count (recMFC0 and recMTC0 in pcsx2/x86/iCOP0.cpp):
// those that read or write the count register or a performance counter, and
// those that write the status register or the performance control register.
// Some COP2 instructions do as well, but which depends on the whole block
// (runtime/rec_blocks.cpp).
inline bool flushesCycles(const Insn& in) {
    switch (in.op) {
    case Op::MFC0: return in.rd() == 9;
    case Op::MFPC: return in.rt() != 0;
    case Op::MTC0: return in.rd() == 12 || in.rd() == 9;
    case Op::MTPS: return in.perfReg() == 0;
    case Op::MTPC: return true;
    default: return false;
    }
}

// A branch or jump in a delay slot that the recompiler drops, cost and all
// (recompileNextInstruction): every one but the coprocessor branches.
inline bool droppedFromDelaySlot(uint32_t code) {
    const uint32_t opcode = code >> 26, rt = (code >> 16) & 31, funct = code & 63;
    return (opcode == 0 && (funct == 8 || funct == 9)) || (opcode == 1 && (rt < 4 || (rt >= 0x10 && rt < 0x14))) || (opcode >= 2 && opcode <= 7) ||
           (opcode >= 0x14 && opcode <= 0x17);
}

} // namespace ps2n::r5900
