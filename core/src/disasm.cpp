// Disassembler driven by the operand strings in r5900_ops.inc.
// Output uses GNU assembler syntax so it can be fed back to the assembler.
#include "ps2n/r5900.h"
#include "ps2n/util.h"

namespace ps2n::r5900 {

namespace {

std::string gpr(uint32_t r) { return std::string("$") + gprName(r); }

std::string destSuffix(uint32_t dest) {
    std::string s;
    if (dest & 8) s += 'x';
    if (dest & 4) s += 'y';
    if (dest & 2) s += 'z';
    if (dest & 1) s += 'w';
    return s;
}

constexpr char kComp[4] = {'x', 'y', 'z', 'w'};

std::string formatOperand(const Insn& in, std::string_view spec) {
    if (spec == "d") return gpr(in.rd());
    if (spec == "s") return gpr(in.rs());
    if (spec == "t") return gpr(in.rt());
    if (spec == "<") return std::to_string(in.sa());
    if (spec == "i") return std::to_string(in.simm());
    if (spec == "u") return strprintf("0x%x", in.uimm());
    if (spec == "p") return hex32(in.branchTarget());
    if (spec == "a") return hex32(in.jumpTarget());
    if (spec == "o(b)") return strprintf("%d(%s)", in.simm(), gpr(in.rs()).c_str());
    if (spec == "D") return strprintf("$f%u", in.fd());
    if (spec == "S") return strprintf("$f%u", in.fs());
    if (spec == "T") return strprintf("$f%u", in.ft());
    if (spec == "g") return strprintf("$%u", in.rd());
    if (spec == "G") {
        const char* n = cop0Name(in.rd());
        return n ? std::string(n) : strprintf("$%u", in.rd());
    }
    if (spec == "B") return in.code20() ? strprintf("0x%x", in.code20()) : std::string();
    if (spec == "c") return in.code10() ? strprintf("0x%x", in.code10()) : std::string();
    if (spec == "k") return strprintf("0x%x", in.rt());
    if (spec == "h") return std::to_string(in.rt());
    if (spec == "r") return std::to_string(in.perfReg());
    if (spec == "V") return strprintf("$vf%u", in.fd());
    if (spec == "X") return strprintf("$vf%u", in.fs());
    if (spec == "Y") return strprintf("$vf%u", in.ft());
    if (spec == "Yb") return strprintf("$vf%u%c", in.ft(), kComp[in.bc()]);
    if (spec == "Xf") return strprintf("$vf%u%c", in.fs(), kComp[in.fsf()]);
    if (spec == "Yf") return strprintf("$vf%u%c", in.ft(), kComp[in.ftf()]);
    if (spec == "Yw") return strprintf("$vf%uw", in.ft());
    if (spec == "e") return strprintf("$vi%u", in.fd());
    if (spec == "f") return strprintf("$vi%u", in.fs());
    if (spec == "n") return strprintf("$vi%u", in.ft());
    if (spec == "E") return strprintf("$vi%u", in.rd());
    if (spec == "5") return std::to_string(in.imm5());
    if (spec == "M") return strprintf("0x%x", in.imm15() * 8);
    if (spec == "A") return "$ACC";
    if (spec == "Q") return "$Q";
    if (spec == "I") return "$I";
    if (spec == "R") return "$R";
    if (spec == "(f++)") return strprintf("($vi%u++)", in.fs());
    if (spec == "(--f)") return strprintf("(--$vi%u)", in.fs());
    if (spec == "(n++)") return strprintf("($vi%u++)", in.ft());
    if (spec == "(--n)") return strprintf("(--$vi%u)", in.ft());
    if (spec == "(f)") return strprintf("($vi%u)", in.fs());
    if (spec == "P") return {};
    return "?" + std::string(spec);
}

// GNU objdump-style pseudo-instructions, for readability of listings and of
// the comments in generated code.
bool formatAlias(const Insn& in, std::string& out) {
    switch (in.op) {
    case Op::SLL:
        if (in.raw == 0) { out = "nop"; return true; }
        return false;
    case Op::ADDU:
    case Op::DADDU:
    case Op::OR:
        if (in.rt() == 0) {
            out = "move " + gpr(in.rd()) + ", " + gpr(in.rs());
            return true;
        }
        return false;
    case Op::SUBU:
        if (in.rs() == 0) {
            out = "negu " + gpr(in.rd()) + ", " + gpr(in.rt());
            return true;
        }
        return false;
    case Op::NOR:
        if (in.rt() == 0) {
            out = "not " + gpr(in.rd()) + ", " + gpr(in.rs());
            return true;
        }
        return false;
    case Op::ADDIU:
        if (in.rs() == 0) {
            out = "li " + gpr(in.rt()) + ", " + std::to_string(in.simm());
            return true;
        }
        return false;
    case Op::ORI:
        if (in.rs() == 0 && in.uimm() > 0x7FFF) {   // smaller values would re-assemble as addiu
            out = strprintf("li %s, 0x%x", gpr(in.rt()).c_str(), in.uimm());
            return true;
        }
        return false;
    case Op::BEQ:
        if (in.rs() == 0 && in.rt() == 0) { out = "b " + hex32(in.branchTarget()); return true; }
        if (in.rt() == 0) { out = "beqz " + gpr(in.rs()) + ", " + hex32(in.branchTarget()); return true; }
        return false;
    case Op::BNE:
        if (in.rt() == 0) { out = "bnez " + gpr(in.rs()) + ", " + hex32(in.branchTarget()); return true; }
        return false;
    case Op::BEQL:
        if (in.rt() == 0 && in.rs() != 0) { out = "beqzl " + gpr(in.rs()) + ", " + hex32(in.branchTarget()); return true; }
        return false;
    case Op::BNEL:
        if (in.rt() == 0) { out = "bnezl " + gpr(in.rs()) + ", " + hex32(in.branchTarget()); return true; }
        return false;
    case Op::BGEZAL:
        if (in.rs() == 0) { out = "bal " + hex32(in.branchTarget()); return true; }
        return false;
    case Op::JALR:
        if (in.rd() == 31) { out = "jalr " + gpr(in.rs()); return true; }
        return false;
    case Op::MULT:
    case Op::MULTU:
    case Op::MADD:
    case Op::MADDU:
    case Op::MULT1:
    case Op::MULTU1:
    case Op::MADD1:
    case Op::MADDU1:
        if (in.rd() == 0) {
            out = std::string(mnemonic(in.op)) + " " + gpr(in.rs()) + ", " + gpr(in.rt());
            return true;
        }
        return false;
    case Op::SYNC:
        out = (in.sa() & 0x10) ? "sync.p" : "sync.l";
        return true;
    case Op::VCALLMSR:
        out = "vcallmsr $vi27";
        return true;
    default:
        return false;
    }
}

} // namespace

std::string disassemble(const Insn& in) {
    if (!in.valid()) return strprintf(".word 0x%08x", in.raw);
    std::string out;
    if (formatAlias(in, out)) return out;

    const OpInfo& oi = info(in.op);
    std::string mn = oi.mnemonic;
    if (oi.flags & F_VUBC) mn += kComp[in.bc()];
    if (oi.flags & F_VUDEST) {
        const std::string d = destSuffix(in.dest());
        if (!d.empty()) mn += "." + d;
    }
    if ((oi.flags & F_INTERLOCK) && in.interlock()) mn += ".i";

    out = mn;
    bool first = true;
    for (const std::string& spec : split(oi.operands, ',')) {
        if (spec.empty()) continue;
        const std::string op = formatOperand(in, spec);
        if (op.empty()) continue;
        out += first ? " " : ", ";
        out += op;
        first = false;
    }
    return out;
}

} // namespace ps2n::r5900
