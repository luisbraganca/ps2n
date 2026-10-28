#include "codegen.h"

#include "version.h"

#include "ps2uc/util.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace ps2uc {

using namespace r5900;

namespace {

const char* const kRegNames[32] = {
    "ZERO", "AT", "V0", "V1", "A0", "A1", "A2", "A3", "T0", "T1", "T2", "T3", "T4", "T5", "T6", "T7",
    "S0",   "S1", "S2", "S3", "S4", "S5", "S6", "S7", "T8", "T9", "K0", "K1", "GP", "SP", "FP", "RA",
};

std::string hex(uint32_t v) { return strprintf("0x%08xu", v); }
std::string label(uint32_t a) { return strprintf("L_%08x", a); }

std::string cstring(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        if (static_cast<unsigned char>(c) < 0x20) {
            out += strprintf("\\x%02x", static_cast<unsigned char>(c));
            continue;
        }
        out.push_back(c);
    }
    return out + "\"";
}

// ---- semantic calls (r5900_sem.inc) ---------------------------------------------
enum class Arg { RD, RS, RT, SA, SIMM, UIMM, IMM5, FD, FS, FT, DEST, BC, FSF, FTF, MSADDR, PERF };

std::string argText(const Insn& in, Arg a) {
    switch (a) {
    case Arg::RD: return kRegNames[in.rd()];
    case Arg::RS: return kRegNames[in.rs()];
    case Arg::RT: return kRegNames[in.rt()];
    case Arg::SA: return std::to_string(in.sa());
    case Arg::SIMM: return std::to_string(in.simm());
    case Arg::UIMM: return strprintf("0x%x", in.uimm());
    case Arg::IMM5: return std::to_string(in.imm5());
    case Arg::FD: return std::to_string(in.fd());
    case Arg::FS: return std::to_string(in.fs());
    case Arg::FT: return std::to_string(in.ft());
    case Arg::DEST: return strprintf("0x%x", in.dest());
    case Arg::BC: return std::to_string(in.bc());
    case Arg::FSF: return std::to_string(in.fsf());
    case Arg::FTF: return std::to_string(in.ftf());
    case Arg::MSADDR: return strprintf("0x%x", in.imm15() * 8);
    case Arg::PERF: return std::to_string(in.perfReg());
    }
    return "?";
}

std::string formatCall(const char* fn, const Insn& in, std::initializer_list<Arg> args) {
    std::string s = fn;
    s.push_back('(');
    bool first = true;
    for (Arg a : args) {
        if (!first) s += ", ";
        s += argText(in, a);
        first = false;
    }
    return s + ")";
}

std::string semanticCall(const Insn& in) {
#define RD Arg::RD
#define RS Arg::RS
#define RT Arg::RT
#define SA Arg::SA
#define SIMM Arg::SIMM
#define UIMM Arg::UIMM
#define IMM5 Arg::IMM5
#define FD Arg::FD
#define FS Arg::FS
#define FT Arg::FT
#define DEST Arg::DEST
#define BC Arg::BC
#define FSF Arg::FSF
#define FTF Arg::FTF
#define MSADDR Arg::MSADDR
#define PERF Arg::PERF
#define PS2UC_EXPAND(...) __VA_ARGS__
#define S(op, fn, args) \
    case Op::op:        \
        return formatCall(#fn, in, {PS2UC_EXPAND args});
    switch (in.op) {
#include "ps2uc/r5900_sem.inc"
    default: return {};
    }
#undef S
#undef PS2UC_EXPAND
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

bool writesHiLo(Op op) {
    switch (op) {
    case Op::MULT: case Op::MULTU: case Op::MULT1: case Op::MULTU1: case Op::MADD: case Op::MADDU: case Op::MADD1:
    case Op::MADDU1: case Op::PMULTW: case Op::PMULTUW: case Op::PMADDW: case Op::PMADDUW: case Op::PMSUBW: case Op::PMULTH:
    case Op::PMADDH: case Op::PMSUBH: case Op::PHMADH: case Op::PHMSBH:
        return true;
    default:
        return false;
    }
}

// Instructions whose only effect would be a write to $zero (nop, hints).
bool isDeadWrite(const Insn& in) {
    if (in.raw == 0) return true;
    if (in.is(F_LOAD | F_COP0 | F_FPU | F_VU0) || writesHiLo(in.op)) return false;
    if (in.is(F_WRITES_RD)) return in.rd() == 0;
    if (in.is(F_WRITES_RT)) return in.rt() == 0;
    return false;
}

std::string gpr64(uint32_t r) { return r ? strprintf("ru64(%s)", kRegNames[r]) : "0"; }
std::string gprS64(uint32_t r) { return r ? strprintf("rs64(%s)", kRegNames[r]) : "0"; }

std::string branchCondition(const Insn& in) {
    switch (in.op) {
    case Op::BEQ: case Op::BEQL: return gpr64(in.rs()) + " == " + gpr64(in.rt());
    case Op::BNE: case Op::BNEL: return gpr64(in.rs()) + " != " + gpr64(in.rt());
    case Op::BLEZ: case Op::BLEZL: return gprS64(in.rs()) + " <= 0";
    case Op::BGTZ: case Op::BGTZL: return gprS64(in.rs()) + " > 0";
    case Op::BLTZ: case Op::BLTZL: case Op::BLTZAL: case Op::BLTZALL: return gprS64(in.rs()) + " < 0";
    case Op::BGEZ: case Op::BGEZL: case Op::BGEZAL: case Op::BGEZALL: return gprS64(in.rs()) + " >= 0";
    case Op::BC0F: case Op::BC0FL: return "!ps2::cpcond0()";
    case Op::BC0T: case Op::BC0TL: return "ps2::cpcond0()";
    case Op::BC1F: case Op::BC1FL: return "!fpuCond()";
    case Op::BC1T: case Op::BC1TL: return "fpuCond()";
    case Op::BC2F: case Op::BC2FL: return "true /* VU0 never left running */";
    case Op::BC2T: case Op::BC2TL: return "false /* VU0 never left running */";
    default: return "false";
    }
}

std::string trapCondition(const Insn& in) {
    const std::string s = gprS64(in.rs()), t = gprS64(in.rt()), us = gpr64(in.rs()), ut = gpr64(in.rt());
    const std::string imm = std::to_string(in.simm());
    switch (in.op) {
    case Op::TGE: return s + " >= " + t;
    case Op::TGEU: return us + " >= " + ut;
    case Op::TLT: return s + " < " + t;
    case Op::TLTU: return us + " < " + ut;
    case Op::TEQ: return us + " == " + ut;
    case Op::TNE: return us + " != " + ut;
    case Op::TGEI: return s + " >= " + imm;
    case Op::TGEIU: return us + " >= uint64_t(int64_t(" + imm + "))";
    case Op::TLTI: return s + " < " + imm;
    case Op::TLTIU: return us + " < uint64_t(int64_t(" + imm + "))";
    case Op::TEQI: return s + " == " + imm;
    case Op::TNEI: return s + " != " + imm;
    default: return "false";
    }
}

class FunctionEmitter {
public:
    FunctionEmitter(const Program& p, const Function& f, bool comments) : p_(p), f_(f), comments_(comments) {}

    std::string emit() {
        const Config& cfg = *p_.config;
        out_ += strprintf("// %s  0x%08x-0x%08x  (%zu instructions, from %s)\n", f_.name.c_str(), f_.start, f_.end,
                          static_cast<size_t>((f_.end - f_.start) / 4), sourceName(f_.source));
        out_ += "void " + f_.cname + "() {\n";
        if (std::any_of(f_.resume.begin(), f_.resume.end(), [&](uint32_t r) { return r != f_.start; })) {
            out_ += "    switch (cpu.pc) {\n";
            out_ += "    case " + hex(f_.start) + ": break;\n";
            for (uint32_t r : f_.resume)
                if (r != f_.start) out_ += "    case " + hex(r) + ": goto " + label(r) + ";\n";
            out_ += "    default: ps2::badEntry(" + hex(f_.start) + "); return;\n";
            out_ += "    }\n";
        }
        (void)cfg;
        bool fallsThrough = true;
        const size_t body = (f_.end - f_.start) / 4;
        for (size_t k = 0; k < body;) {
            const Insn& in = f_.insns[k];
            if (needsLabel(in.addr)) out_ += label(in.addr) + ":\n";
            if (in.hasDelaySlot()) {
                fallsThrough = emitControl(k);
                k += 2;
            } else {
                emitSimple(in, false);
                fallsThrough = in.op != Op::ERET;
                k += 1;
            }
        }
        if (extraLabels_.count(f_.end)) {
            out_ += label(f_.end) + ":\n";
            fallsThrough = true;
        }
        if (fallsThrough) {
            out_ += "    // falls through into the next function\n";
            emitTailTransfer(f_.end, "    ");
        }
        out_ += "}\n";
        return out_;
    }

private:
    const Program& p_;
    const Function& f_;
    bool comments_;
    std::string out_;
    std::set<uint32_t> extraLabels_;

    bool inside(uint32_t a) const { return a >= f_.start && a < f_.end; }
    bool needsLabel(uint32_t a) const { return f_.labels.count(a) || extraLabels_.count(a); }

    void line(const std::string& indent, const std::string& code, const Insn* in, const char* note = nullptr) {
        std::string l = indent + code;
        if (comments_ && in) {
            if (l.size() < 52) l.append(52 - l.size(), ' ');
            else l.push_back(' ');
            l += strprintf("// %08x  %s", in->addr, disassemble(*in).c_str());
            if (note) l += strprintf("  [%s]", note);
        }
        out_ += l + "\n";
    }

    void commentLine(const std::string& indent, const Insn& in, const std::string& extra = {}) {
        if (!comments_) return;
        std::string target;
        if (in.is(F_JUMP) || in.isBranch()) target = "  -> " + p_.describe(in.staticTarget());
        out_ += indent + strprintf("// %08x  %s%s%s\n", in.addr, disassemble(in).c_str(), target.c_str(), extra.c_str());
    }

    void emitSimple(const Insn& in, bool delay, const std::string& indent = "    ") {
        const char* note = delay ? "delay slot" : nullptr;
        const std::string pc = hex(in.addr);
        if (!in.valid()) {
            line(indent, "ps2::interpretOne(" + pc + ");", &in, "unknown instruction");
            return;
        }
        switch (in.op) {
        case Op::SYSCALL: line(indent, "PS2_SYSCALL(" + pc + ");", &in, note); return;
        case Op::BREAK: line(indent, "ps2::breakpoint(" + pc + ", " + std::to_string(in.code20()) + ");", &in, note); return;
        case Op::ERET: line(indent, "ps2::eret(" + pc + "); return;", &in, note); return;
        case Op::SYNC:
        case Op::CACHE:
        case Op::PREF:
            if (comments_) line(indent, "", &in, "no effect");
            return;
        default: break;
        }
        if (in.is(F_TRAP)) {
            line(indent, "if (" + trapCondition(in) + ") ps2::trap(" + pc + ", " + std::to_string(in.code10()) + ");", &in, note);
            return;
        }
        if (isDeadWrite(in)) {
            if (comments_) line(indent, "", &in, in.raw ? "writes $zero: no effect" : note);
            return;
        }
        const std::string call = semanticCall(in);
        if (call.empty()) {
            line(indent, "ps2::interpretOne(" + pc + ");", &in, "no translation");
            return;
        }
        line(indent, call + ";", &in, note);
    }

    void emitDelaySlot(const Insn& ds, const std::string& indent) {
        if (ds.isControlFlow()) {
            if (comments_) line(indent, "", &ds, "branch in delay slot: ignored");
            return;
        }
        if (ds.op == Op::SYSCALL) {
            if (comments_) line(indent, "", &ds, "syscall in delay slot: ignored");
            return;
        }
        if (ds.raw == 0) return;
        emitSimple(ds, true, indent);
    }

    // Jump (no link) to `target` from the instruction at `from`.
    void emitJump(uint32_t target, uint32_t from, const std::string& indent) {
        if (inside(target)) {
            if (target <= from && p_.config->preemptLoops) out_ += indent + "PS2_BACKEDGE(" + hex(target) + ");\n";
            out_ += indent + "goto " + label(target) + ";\n";
            return;
        }
        emitTailTransfer(target, indent);
    }

    void emitTailTransfer(uint32_t target, const std::string& indent) {
        if (const Function* g = p_.functionAt(target)) {
            out_ += indent + "cpu.pc = " + hex(target) + "; " + g->cname + "(); return;\n";
            return;
        }
        if (const Function* h = p_.functionContaining(target); h && h->resume.count(target)) {
            out_ += indent + "cpu.pc = " + hex(target) + "; " + h->cname + "(); return;\n";
            return;
        }
        out_ += indent + "ps2::call(" + hex(target) + "); return;\n";
    }

    void emitCall(uint32_t target, uint32_t ret, const std::string& indent) {
        if (const Function* g = p_.functionAt(target)) {
            out_ += indent + "PS2_CALL(" + hex(target) + ", " + g->cname + ", " + hex(ret) + ");\n";
            return;
        }
        if (const Function* h = p_.functionContaining(target); h && (h->resume.count(target) || h == &f_)) {
            out_ += indent + "PS2_CALL(" + hex(target) + ", " + h->cname + ", " + hex(ret) + ");\n";
            return;
        }
        out_ += indent + "PS2_CALL_DYNAMIC(" + hex(target) + ", " + hex(ret) + ");\n";
    }

    // Emits a branch/jump and its delay slot. Returns whether execution can
    // continue at the instruction after the delay slot.
    bool emitControl(size_t k) {
        const Insn& in = f_.insns[k];
        static const Insn kNop(0, 0);
        const Insn& ds = k + 1 < f_.insns.size() ? f_.insns[k + 1] : kNop;
        const uint32_t a = in.addr, ret = a + 8;
        bool continues = true;
        commentLine("    ", in);
        switch (in.op) {
        case Op::J:
            emitDelaySlot(ds, "    ");
            emitJump(in.jumpTarget(), a, "    ");
            continues = false;
            break;
        case Op::JAL:
            out_ += "    setLink(RA, " + hex(ret) + ");\n";
            emitDelaySlot(ds, "    ");
            emitCall(in.jumpTarget(), ret, "    ");
            break;
        case Op::JR:
            out_ += "    {\n";
            out_ += std::string("        const uint32_t target = ru32(") + kRegNames[in.rs()] + ");\n";
            emitDelaySlot(ds, "        ");
            if (in.rs() == 31) {
                out_ += "        cpu.pc = target;\n        return;\n";
            } else {
                std::set<uint32_t> cands;
                if (auto it = f_.jumpTables.find(a); it != f_.jumpTables.end()) cands.insert(it->second.begin(), it->second.end());
                cands.insert(f_.switchCandidates.begin(), f_.switchCandidates.end());
                if (!cands.empty()) {
                    out_ += "        switch (target) {\n";
                    for (uint32_t t : cands) out_ += "        case " + hex(t) + ": goto " + label(t) + ";\n";
                    out_ += "        default: break;\n        }\n";
                }
                out_ += "        ps2::call(target);\n        return;\n";
            }
            out_ += "    }\n";
            continues = false;
            break;
        case Op::JALR:
            out_ += "    {\n";
            out_ += std::string("        const uint32_t target = ru32(") + kRegNames[in.rs()] + ");\n";
            if (in.rd()) out_ += std::string("        setLink(") + kRegNames[in.rd()] + ", " + hex(ret) + ");\n";
            emitDelaySlot(ds, "        ");
            out_ += "        PS2_CALL_DYNAMIC(target, " + hex(ret) + ");\n";
            out_ += "    }\n";
            break;
        default: {   // conditional branches
            const uint32_t t = in.branchTarget();
            const bool link = in.isCall();
            const bool likely = in.isLikely();
            auto transfer = [&](const std::string& indent) {
                if (link) emitCall(t, ret, indent);
                else emitJump(t, a, indent);
            };
            if (in.isUnconditionalBranch() && !likely) {
                if (link) out_ += "    setLink(RA, " + hex(ret) + ");\n";
                emitDelaySlot(ds, "    ");
                transfer("    ");
                continues = link;
            } else if (likely) {
                out_ += "    {\n";
                out_ += "        const bool taken = " + branchCondition(in) + ";\n";
                if (link) out_ += "        setLink(RA, " + hex(ret) + ");\n";
                out_ += "        if (taken) {\n";
                emitDelaySlot(ds, "            ");
                transfer("            ");
                out_ += "        }\n    }\n";
            } else {
                out_ += "    {\n";
                out_ += "        const bool taken = " + branchCondition(in) + ";\n";
                if (link) out_ += "        setLink(RA, " + hex(ret) + ");\n";
                emitDelaySlot(ds, "        ");
                out_ += "        if (taken) {\n";
                transfer("            ");
                out_ += "        }\n    }\n";
            }
            break;
        }
        }
        // Someone jumps straight to the delay slot: give it a standalone copy.
        if (needsLabel(a + 4) && ds.addr == a + 4 && inside(a + 4)) {
            if (continues) out_ += "    goto " + label(ret) + ";\n";
            out_ += label(a + 4) + ":\n";
            emitDelaySlot(ds, "    ");
            extraLabels_.insert(ret);
            continues = true;
        }
        return continues;
    }
};

// ---- wrappers for stub / skip / override bindings -------------------------------------
std::string emitWrapper(const Function& f) {
    std::string s = strprintf("// %s  0x%08x-0x%08x  ", f.name.c_str(), f.start, f.end);
    switch (f.binding) {
    case Binding::Stub:
        s += "replaced by runtime stub \"" + f.handler + "\"\n";
        s += "void " + f.cname + "() { cpu.pc = ru32(RA); ps2::stub::" + f.handler + "_(); }\n";
        break;
    case Binding::Skip:
        s += "skipped by config\n";
        s += "void " + f.cname + "() { cpu.pc = ru32(RA); ps2::skippedFunction(" + hex(f.start) + ", " + cstring(f.name) + "); }\n";
        break;
    case Binding::Override:
        s += "overridden by " + f.handler + "()\n";
        s += "void " + f.cname + "() { cpu.pc = ru32(RA); ::" + f.handler + "(); }\n";
        break;
    default: break;
    }
    return s;
}

const char* kGenNote = "// Generated by " PS2UC_VERSION_STRING " - do not edit; change the config and recompile instead.\n";

std::string fileHeader(const std::string& ns) {
    return std::string(kGenNote) +
           "#include \"ps2/recomp.h\"\n#include \"functions.h\"\n\n"
           "using namespace ps2;\nusing namespace ps2::ops;\nusing namespace ps2::regs;\n\n"
           "namespace " + ns + " {\n\n";
}

std::vector<std::vector<const Function*>> partition(const Program& prog, int files) {
    std::vector<std::vector<const Function*>> out(static_cast<size_t>(files));
    double total = 0;
    for (const auto& f : prog.functions) total += 20.0 + (f.end - f.start) / 4.0;
    const double perFile = total / files;
    double acc = 0;
    for (const auto& f : prog.functions) {
        auto idx = static_cast<size_t>(std::min<double>(files - 1, std::floor(acc / perFile)));
        out[idx].push_back(&f);
        acc += 20.0 + (f.end - f.start) / 4.0;
    }
    return out;
}

std::string pathForCmake(const std::filesystem::path& p) {
    std::string s = p.generic_string();
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// A function's instruction words as the ELF has them, patched ones left out:
// RAM holds those patched or not, depending on who loaded the program.
uint32_t codeChecksum(const Program& prog, const Function& f) {
    CodeChecksum sum;
    for (uint32_t a = f.start; a < f.end; a += 4)
        if (!prog.patched.count(a)) sum.add(prog.word(a));
    return sum.value;
}

using PutFn = std::function<void(const std::filesystem::path&, const std::string&)>;

// One program's functions.h, recomp_funcs_NNN.cpp and recomp_table.cpp, in
// `dir` of the output with the code in namespace `ns`. The boot program's
// table also lists `others`, the game's other executables. Returns the number
// of recomp_funcs files.
int writeProgram(const Program& prog, const Config& cfg, const std::filesystem::path& dir, const std::string& ns,
                 const std::vector<ExtraProgram>& others, const PutFn& put) {
    namespace fs = std::filesystem;

    // functions.h
    {
        std::ostringstream h;
        h << kGenNote << "#pragma once\n\n#include \"ps2/runtime.h\"\n\n";
        std::set<std::string> overrides;
        for (const auto& f : prog.functions)
            if (f.binding == Binding::Override) overrides.insert(f.handler);
        if (!overrides.empty()) {
            h << "// User-provided replacements ([functions] override in the config).\n";
            for (const auto& o : overrides) h << "void " << o << "();\n";
            h << "\n";
        }
        h << "namespace " << ns << " {\n\n";
        for (const auto& f : prog.functions) h << "void " << f.cname << "();\n";
        h << "\nextern const ps2::GameInfo kGameInfo;\n\n} // namespace " << ns << "\n";
        put(dir / "functions.h", h.str());
    }

    // recomp_funcs_NNN.cpp
    const int files = functionFileCount(prog, cfg);
    const auto parts = partition(prog, files);
    for (int i = 0; i < files; ++i) {
        std::string s = fileHeader(ns);
        for (const Function* f : parts[static_cast<size_t>(i)]) s += emitFunction(prog, *f, cfg.comments) + "\n";
        s += "} // namespace " + ns + "\n";
        put(dir / strprintf("recomp_funcs_%03d.cpp", i), s);
    }

    // recomp_table.cpp
    std::ostringstream t;
    t << kGenNote << "#include \"functions.h\"\n\n";
    for (const auto& o : others) t << "namespace ps2game_" << o.id << " { extern const ps2::GameInfo kGameInfo; }\n";
    if (!others.empty()) t << "\n";
    t << "namespace " << ns << " {\n\nnamespace {\n\n";
    t << "const ps2::FunctionInfo kFunctions[] = {\n";
    for (const auto& f : prog.functions)
        t << "    {" << hex(f.start) << ", " << hex(f.end) << ", &" << f.cname << ", " << cstring(f.name) << ", "
          << (f.binding == Binding::Recompile ? "false" : "true") << ", " << hex(codeChecksum(prog, f)) << "},\n";
    t << "};\n\n";
    const std::map<uint32_t, uint32_t> patches(prog.patched.begin(), prog.patched.end());
    t << "const ps2::PatchInfo kPatches[] = {\n";
    if (patches.empty()) t << "    {0, 0},   // (none)\n";
    for (const auto& [addr, value] : patches) t << "    {" << hex(addr) << ", " << hex(value) << "},\n";
    t << "};\n\n";
    std::vector<uint32_t> resume;
    for (const auto& f : prog.functions) resume.insert(resume.end(), f.resume.begin(), f.resume.end());
    std::sort(resume.begin(), resume.end());
    t << "const uint32_t kResumePoints[] = {\n";
    if (resume.empty()) t << "    0xffffffffu,   // (none)\n";
    for (size_t i = 0; i < resume.size(); ++i) t << (i % 6 == 0 ? "    " : " ") << hex(resume[i]) << "," << (i % 6 == 5 ? "\n" : "");
    if (!resume.empty() && resume.size() % 6) t << "\n";
    t << "};\n\n";
    if (!others.empty()) {
        t << "const ps2::GameInfo* const kPrograms[] = {\n";
        for (const auto& o : others) t << "    &::ps2game_" << o.id << "::kGameInfo,\n";
        t << "};\n\n";
    }
    t << "} // namespace\n\n";
    t << "extern const ps2::GameInfo kGameInfo = {\n";
    t << "    " << cstring(cfg.name) << ",\n";
    t << "    " << cstring(fs::absolute(cfg.elf).generic_string()) << ",\n";
    t << "    " << strprintf("0x%08xu", prog.elf->crc32()) << ",\n";
    t << "    " << hex(prog.elf->entry) << ",\n";
    t << "    kFunctions, sizeof(kFunctions) / sizeof(kFunctions[0]),\n";
    t << "    kResumePoints, " << (resume.empty() ? "0" : "sizeof(kResumePoints) / sizeof(kResumePoints[0])") << ",\n";
    t << "    " << cstring(PS2UC_VERSION_STRING) << ",\n";
    t << "    kPatches, " << (patches.empty() ? "0" : "sizeof(kPatches) / sizeof(kPatches[0])") << ",\n";
    t << "    " << (others.empty() ? "nullptr, 0" : "kPrograms, sizeof(kPrograms) / sizeof(kPrograms[0])") << ",\n";
    t << "};\n\n} // namespace " << ns << "\n";
    put(dir / "recomp_table.cpp", t.str());
    return files;
}

} // namespace

std::string programId(const std::filesystem::path& elf) {
    const std::string name = elf.filename().string();
    std::string id = !name.empty() && name[0] >= '0' && name[0] <= '9' ? "_" : "";
    for (char c : name) {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        id.push_back(keep ? c : '_');
    }
    return id;
}

std::string emitFunction(const Program& prog, const Function& f, bool comments) {
    if (f.binding != Binding::Recompile) return emitWrapper(f);
    return FunctionEmitter(prog, f, comments).emit();
}

int functionFileCount(const Program& prog, const Config& cfg) {
    if (cfg.singleFile) return 1;
    if (cfg.files > 0) return cfg.files;
    size_t insns = 0;
    for (const auto& f : prog.functions) insns += (f.end - f.start) / 4;
    return static_cast<int>(std::max<size_t>(1, (insns + 15999) / 16000));
}

GenerateResult generate(const Program& prog, const Config& cfg, const std::vector<ExtraProgram>& extras) {
    namespace fs = std::filesystem;
    GenerateResult res;
    fs::create_directories(cfg.outDir);
    auto put = [&](const fs::path& name, const std::string& content) {
        const fs::path p = cfg.outDir / name;
        fs::create_directories(p.parent_path());
        if (writeFileIfChanged(p, content)) res.written.push_back(p);
        if (p.extension() == ".cpp") res.sources.push_back(p);
    };

    // The boot program at the top of the output, each other executable in its own folder.
    const int files = writeProgram(prog, cfg, {}, "ps2game", extras, put);
    res.functionFiles = static_cast<size_t>(files);
    for (const auto& e : extras) writeProgram(*e.prog, *e.cfg, e.id, "ps2game_" + e.id, {}, put);

    // recomp_main.cpp
    put("recomp_main.cpp", std::string(kGenNote) +
                               "#include \"functions.h\"\n\n"
                               "int main(int argc, char** argv) { return ps2::run(argc, argv, ps2game::kGameInfo); }\n");

    // A standalone CMake project around the generated sources.
    if (cfg.project) {
        fs::path runtimeDir = cfg.runtimeDir.empty() ? fs::path(PS2UC_SOURCE_DIR) : cfg.runtimeDir;
        std::ostringstream c;
        c << "# " << (kGenNote + 3);
        c << "cmake_minimum_required(VERSION 3.20)\nproject(" << cfg.name << " LANGUAGES C CXX)\n\n"
          << "set(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
          << "if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)\n  set(CMAKE_BUILD_TYPE Release CACHE STRING \"\" FORCE)\nendif()\n\n"
          << "set(PS2UC_ROOT \"" << pathForCmake(runtimeDir) << "\" CACHE PATH \"ps2-uncompiler source tree\")\n"
          << "list(APPEND CMAKE_MODULE_PATH \"${PS2UC_ROOT}/cmake\")\ninclude(Ps2ucWarnings)\n"
          << "if(NOT TARGET ps2uc_runtime)\n"
          << "  add_subdirectory(\"${PS2UC_ROOT}/core\" ps2uc_core)\n"
          << "  add_subdirectory(\"${PS2UC_ROOT}/runtime\" ps2uc_runtime)\n"
          << "endif()\n\n"
          << "add_executable(" << cfg.name << "\n";
        for (int i = 0; i < files; ++i) c << "    recomp_funcs_" << strprintf("%03d", i) << ".cpp\n";
        c << "    recomp_table.cpp\n    recomp_main.cpp\n";
        std::vector<fs::path> userSources;
        auto addUserSources = [&](const Config& from) {
            for (const auto& e : from.extraSources)
                if (std::find(userSources.begin(), userSources.end(), fs::absolute(e)) == userSources.end()) userSources.push_back(fs::absolute(e));
        };
        addUserSources(cfg);
        for (const auto& e : extras) {
            for (int i = 0; i < functionFileCount(*e.prog, *e.cfg); ++i) c << "    " << e.id << strprintf("/recomp_funcs_%03d.cpp\n", i);
            c << "    " << e.id << "/recomp_table.cpp\n";
            addUserSources(*e.cfg);
        }
        for (const auto& s : userSources) c << "    \"" << pathForCmake(s) << "\"\n";
        c << ")\n"
          << "target_include_directories(" << cfg.name << " PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})\n"
          << "target_link_libraries(" << cfg.name << " PRIVATE ps2uc_runtime)\n"
          << "ps2uc_set_generated_warnings(" << cfg.name << ")\n";
        put("CMakeLists.txt", c.str());
    }
    return res;
}

} // namespace ps2uc
