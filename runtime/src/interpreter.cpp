// Fallback interpreter.
//
// Runs guest code the recompiler did not produce: functions it missed,
// entries without a resume label, code loaded at run time (overlays), and
// everything when --interpret is given. It decodes with the same decoder as
// the recompiler and executes each instruction through the same
// ps2::ops functions as generated code (via r5900_sem.inc), so the two paths
// differ only in control flow.
#include "internal.h"
#include "kernel/kernel.h"
#include "log.h"

#include "ps2uc/r5900.h"

namespace ps2 {

namespace r5900 = ps2uc::r5900;
using r5900::Insn;
using r5900::Op;

namespace {

bool execSimple(const Insn& in) {
#define RD in.rd()
#define RS in.rs()
#define RT in.rt()
#define SA in.sa()
#define SIMM in.simm()
#define UIMM in.uimm()
#define IMM5 in.imm5()
#define FD in.fd()
#define FS in.fs()
#define FT in.ft()
#define DEST in.dest()
#define BC in.bc()
#define FSF in.fsf()
#define FTF in.ftf()
#define MSADDR (in.imm15() * 8)
#define PERF in.perfReg()
#define S(op, fn, args) \
    case Op::op:        \
        ops::fn args;   \
        return true;
    switch (in.op) {
#include "ps2uc/r5900_sem.inc"
    case Op::SYNC:
    case Op::CACHE:
    case Op::PREF:
        return true;
    default:
        return false;
    }
#undef S
#undef RD
#undef RS
#undef RT
#undef SA
#undef SIMM
#undef UIMM
#undef IMM5
#undef FD
#undef FS
#undef FT
#undef DEST
#undef BC
#undef FSF
#undef FTF
#undef MSADDR
#undef PERF
}

void unknownInstruction(const Insn& in) {
    PS2_UNIMPL("cpu", r5900::disassemble(in), "cannot execute 0x%08x (%s) at 0x%08x; treating it as a no-op", in.raw,
               r5900::disassemble(in).c_str(), in.addr);
}

bool trapCondition(const Insn& in) {
    switch (in.op) {
    case Op::TGE: return rs64(in.rs()) >= rs64(in.rt());
    case Op::TGEU: return ru64(in.rs()) >= ru64(in.rt());
    case Op::TLT: return rs64(in.rs()) < rs64(in.rt());
    case Op::TLTU: return ru64(in.rs()) < ru64(in.rt());
    case Op::TEQ: return ru64(in.rs()) == ru64(in.rt());
    case Op::TNE: return ru64(in.rs()) != ru64(in.rt());
    case Op::TGEI: return rs64(in.rs()) >= in.simm();
    case Op::TGEIU: return ru64(in.rs()) >= static_cast<uint64_t>(static_cast<int64_t>(in.simm()));
    case Op::TLTI: return rs64(in.rs()) < in.simm();
    case Op::TLTIU: return ru64(in.rs()) < static_cast<uint64_t>(static_cast<int64_t>(in.simm()));
    case Op::TEQI: return rs64(in.rs()) == in.simm();
    case Op::TNEI: return rs64(in.rs()) != in.simm();
    default: return false;
    }
}

bool branchTaken(const Insn& in) {
    switch (in.op) {
    case Op::BEQ:
    case Op::BEQL: return ru64(in.rs()) == ru64(in.rt());
    case Op::BNE:
    case Op::BNEL: return ru64(in.rs()) != ru64(in.rt());
    case Op::BLEZ:
    case Op::BLEZL: return rs64(in.rs()) <= 0;
    case Op::BGTZ:
    case Op::BGTZL: return rs64(in.rs()) > 0;
    case Op::BLTZ:
    case Op::BLTZL:
    case Op::BLTZAL:
    case Op::BLTZALL: return rs64(in.rs()) < 0;
    case Op::BGEZ:
    case Op::BGEZL:
    case Op::BGEZAL:
    case Op::BGEZALL: return rs64(in.rs()) >= 0;
    case Op::BC0F:
    case Op::BC0FL: return !cpcond0();
    case Op::BC0T:
    case Op::BC0TL: return cpcond0();
    case Op::BC1F:
    case Op::BC1FL: return !ops::fpuCond();
    case Op::BC1T:
    case Op::BC1TL: return ops::fpuCond();
    case Op::BC2F:
    case Op::BC2FL: return true;    // VU0 is never left running (micro programs complete synchronously)
    case Op::BC2T:
    case Op::BC2TL: return false;
    default: return false;
    }
}

// Executes a delay-slot instruction.
void execDelaySlot(uint32_t addr) {
    const Insn ds(read32(addr), addr);
    if (ds.isControlFlow() || ds.op == Op::SYSCALL) {
        PS2_UNIMPL("cpu", "control transfer in a delay slot", "control-flow instruction in the delay slot at 0x%08x is ignored", addr);
        return;
    }
    if (!execSimple(ds)) unknownInstruction(ds);
}

// A taken call: run the callee recompiled if possible (nested host call),
// otherwise keep interpreting into it. Returns false if the caller must stop.
bool enterCall(uint32_t target, uint32_t ret) {
    cpu.pc = target;
    if (!hasCompiledEntry(target)) return true;   // interpret the callee inline
    call(target);
    return cpu.pc == ret && !g_exitRequested;   // otherwise unwind: thread switch, longjmp, exit
}

} // namespace

void interpretOne(uint32_t pc) {
    const Insn in(read32(pc), pc);
    if (!execSimple(in)) unknownInstruction(in);
}

bool interpretStep() {
    const Insn in(read32(cpu.pc), cpu.pc);
    if (in.isControlFlow()) return false;
    if (!execSimple(in)) {
        unknownInstruction(in);
        return false;
    }
    cpu.pc += 4;
    return true;
}

void interpret() {
    bool first = true;
    for (;;) {
        if (g_exitRequested) return;
        const uint32_t pc = cpu.pc;
        if (!first && (isTrampoline(pc) || hasCompiledEntry(pc))) return;
        if (isTrampoline(pc)) return;
        first = false;
        if (--g_slice < 0 && !safepoint(pc)) return;

        const Insn in(read32(pc), pc);
        if (!in.isControlFlow()) {
            switch (in.op) {
            case Op::SYSCALL:
                cpu.pc = pc + 4;
                syscall(pc);
                if (cpu.pc != pc + 4) return;   // thread switch or exit
                continue;
            case Op::BREAK:
                cpu.pc = pc + 4;
                breakpoint(pc, in.code20());
                if (cpu.pc != pc + 4) return;
                continue;
            case Op::ERET:
                eret(pc);
                return;
            default:
                break;
            }
            if (in.is(r5900::F_TRAP)) {
                if (trapCondition(in)) trap(pc, in.code10());
            } else if (!execSimple(in)) {
                unknownInstruction(in);
            }
            cpu.pc = pc + 4;
            continue;
        }

        const uint32_t ret = pc + 8;
        switch (in.op) {
        case Op::J:
            execDelaySlot(pc + 4);
            cpu.pc = in.jumpTarget();
            break;
        case Op::JAL:
            setLink(regs::RA, ret);
            execDelaySlot(pc + 4);
            if (!enterCall(in.jumpTarget(), ret)) return;
            if (cpu.pc == ret) continue;   // returned from a recompiled callee
            break;
        case Op::JR: {
            const uint32_t target = ru32(in.rs());
            execDelaySlot(pc + 4);
            cpu.pc = target;
            break;
        }
        case Op::JALR: {
            const uint32_t target = ru32(in.rs());
            setLink(in.rd(), ret);
            execDelaySlot(pc + 4);
            if (!enterCall(target, ret)) return;
            if (cpu.pc == ret) continue;
            break;
        }
        default: {   // conditional branches, with or without link / likely
            const bool taken = branchTaken(in);
            const bool link = in.isCall();
            if (link) setLink(regs::RA, ret);
            if (in.isLikely() && !taken) {
                cpu.pc = ret;
                continue;
            }
            execDelaySlot(pc + 4);
            if (!taken) {
                cpu.pc = ret;
                continue;
            }
            if (link) {
                if (!enterCall(in.branchTarget(), ret)) return;
                if (cpu.pc == ret) continue;
            } else {
                cpu.pc = in.branchTarget();
            }
            break;
        }
        }
    }
}

} // namespace ps2
