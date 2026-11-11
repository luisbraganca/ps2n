// SPDX-License-Identifier: GPL-3.0-or-later
#include "codegen.h"

#include "ps2n/codehash.h"
#include "ps2n/util.h"
#include "ps2n/waitloop.h"

#include <algorithm>
#include <initializer_list>
#include <sstream>

namespace ps2n::recomp {

using namespace r5900;

const char* const kGeneratorName = "ps2n-recomp 0.2.2";

namespace {

const char* const kRegNames[32] = {
    "ZERO", "AT", "V0", "V1", "A0", "A1", "A2", "A3", "T0", "T1", "T2", "T3", "T4", "T5", "T6", "T7",
    "S0",   "S1", "S2", "S3", "S4", "S5", "S6", "S7", "T8", "T9", "K0", "K1", "GP", "SP", "FP", "RA",
};

// A function running on into the code after it is followed for this many
// instructions at most; if its block has not ended by then, the interpreter
// finishes it.
constexpr int kMaxTail = 64;

std::string hex(uint32_t v) { return strprintf("0x%08Xu", v); }
std::string label(uint32_t a) { return strprintf("L_%08X", a); }
std::string fnName(uint32_t start) { return strprintf("fn_%08X", start); }

std::string cstring(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) {
            out += strprintf("\\x%02x\" \"", static_cast<unsigned char>(c));
            continue;
        }
        out.push_back(c);
    }
    return out + "\"";
}

// ---- calls of PS2Native's own semantics ------------------------------------------
// The families that have them so far: r5900_sem_int.inc, r5900_sem_cop1.inc,
// r5900_sem_mmi.inc and r5900_sem_cop2_ctl.inc. The same tables are what
// ExecuteOwn() in runtime/ee_provider.cpp includes; an instruction of any
// other family is delegated by both.
enum class Arg { RD, RS, RT, SA, SIMM, UIMM, FD, FS, FT, INTERLOCK };

std::string argText(const Insn& in, Arg a) {
    switch (a) {
    case Arg::RD: return kRegNames[in.rd()];
    case Arg::RS: return kRegNames[in.rs()];
    case Arg::RT: return kRegNames[in.rt()];
    case Arg::SA: return std::to_string(in.sa());
    case Arg::SIMM: return std::to_string(in.simm());
    case Arg::UIMM: return strprintf("0x%X", in.uimm());
    case Arg::FD: return std::to_string(in.fd());
    case Arg::FS: return std::to_string(in.fs());
    case Arg::FT: return std::to_string(in.ft());
    case Arg::INTERLOCK: return in.interlock() ? "true" : "false";
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

// The call that carries out `in`, or nothing when PS2Native has no semantics
// of its own for it.
std::string semanticCall(const Insn& in) {
#define RD Arg::RD
#define RS Arg::RS
#define RT Arg::RT
#define SA Arg::SA
#define SIMM Arg::SIMM
#define UIMM Arg::UIMM
#define FD Arg::FD
#define FS Arg::FS
#define FT Arg::FT
#define INTERLOCK Arg::INTERLOCK
#define PS2N_EXPAND(...) __VA_ARGS__
#define S(op, fn, args) \
    case Op::op:        \
        return formatCall(#fn, in, {PS2N_EXPAND args});
    switch (in.op) {
#include "ps2n/r5900_sem_int.inc"
#include "ps2n/r5900_sem_cop1.inc"
#include "ps2n/r5900_sem_mmi.inc"
#include "ps2n/r5900_sem_cop2_ctl.inc"
    default: return {};
    }
#undef S
#undef PS2N_EXPAND
#undef INTERLOCK
#undef RD
#undef RS
#undef RT
#undef SA
#undef SIMM
#undef UIMM
#undef FD
#undef FS
#undef FT
}

// $zero reads as a literal, so that the C++ compiler sees the constant.
std::string gprU64(uint32_t r) { return r ? strprintf("ru64(%s)", kRegNames[r]) : "uint64_t{0}"; }
std::string gprS64(uint32_t r) { return r ? strprintf("rs64(%s)", kRegNames[r]) : "int64_t{0}"; }

// Whether the branch `in` is taken. The same conditions as ExecuteBranch() in
// runtime/ee_provider.cpp.
std::string branchCondition(const Insn& in) {
    switch (in.op) {
    case Op::BEQ: case Op::BEQL: return gprU64(in.rs()) + " == " + gprU64(in.rt());
    case Op::BNE: case Op::BNEL: return gprU64(in.rs()) + " != " + gprU64(in.rt());
    case Op::BLEZ: case Op::BLEZL: return gprS64(in.rs()) + " <= 0";
    case Op::BGTZ: case Op::BGTZL: return gprS64(in.rs()) + " > 0";
    case Op::BLTZ: case Op::BLTZL: case Op::BLTZAL: case Op::BLTZALL: return gprS64(in.rs()) + " < 0";
    case Op::BGEZ: case Op::BGEZL: case Op::BGEZAL: case Op::BGEZALL: return gprS64(in.rs()) + " >= 0";
    case Op::BC0F: case Op::BC0FL: return "!cop0Condition()";
    case Op::BC0T: case Op::BC0TL: return "cop0Condition()";
    case Op::BC1F: case Op::BC1FL: return "!fpuCondition()";
    case Op::BC1T: case Op::BC1TL: return "fpuCondition()";
    case Op::BC2F: case Op::BC2FL: return "!cop2Condition()";
    case Op::BC2T: case Op::BC2TL: return "cop2Condition()";
    default: return {};
    }
}

// The name of the C++ function for `f`, or for its variant `variant` (-1: itself).
std::string fnName(const Function& f, int variant) {
    return variant < 0 ? fnName(f.start) : fnName(f.start) + strprintf("_v%d", variant);
}

class FunctionEmitter {
public:
    // `variant`: which of the function's variants to compile, or -1 for the
    // function as the executable has it.
    FunctionEmitter(const Program& p, const Function& f, int variant, const GenerateOptions& o, GenerateStats& s)
        : p_(p), f_(f), variant_(variant), opt_(o), stats_(s), index_(p.indexOf(f)), extent_(f.end) {}

    std::string emit() {
        emitBody();

        std::string out;
        if (variant_ < 0)
            out = strprintf("// %s  %08X-%08X  (%u instructions, from %s)\n", f_.displayName().c_str(), f_.start, f_.end,
                            (f_.end - f_.start) / 4, sourceName(f_.source));
        else
            out = strprintf("// %s  %08X-%08X  variant %d: other code a run found there\n", f_.displayName().c_str(), f_.start, f_.end,
                            variant_);
        // Local to the translation unit when there is only one.
        out += std::string(opt_.parts > 1 ? "" : "static ") + "uint32_t " + fnName(f_, variant_) + "(uint32_t pc)\n{\n";
        // The state in which this is the code to run (FunctionState).
        const std::string mine = variant_ < 0 ? "kTrusted" : strprintf("kFirstVariant + %d", variant_);
        out += strprintf("\tPS2N_ENTER(state, image, %u, %s, pc);\n", index_, mine.c_str());
        out += "\tuint32_t acc = 0;\n";
        if (needsDispatch_) out += "ps2n_dispatch:\n";
        out += "\tswitch (pc)\n\t{\n";
        for (uint32_t a : f_.leaders) out += "\t\tcase " + hex(a) + ": goto " + label(a) + ";\n";
        if (needsDispatch_) out += "\t\tdefault: PS2N_JUMP(ps2n::ee::jump, pc);\n";
        else out += strprintf("\t\tdefault: ps2n::ee::badEntry(image, %u, pc);\n", index_);
        out += "\t}\n";
        out += body_;
        out += "}\n";
        return out;
    }

    // One past the last instruction the function was compiled from.
    uint32_t extent() const { return extent_; }

private:
    const Program& p_;
    const Function& f_;
    int variant_;
    const GenerateOptions& opt_;
    GenerateStats& stats_;
    uint32_t index_;

    // The instruction at `a` as this function, or this variant of it, has it.
    Insn insn(uint32_t a) const { return p_.insn(f_, variant_, a); }

    std::string body_;
    int depth_ = 1;              // indentation

    // The cost, in eighths of a cycle, of the instructions emitted since
    // `acc` was last brought up to date.
    uint32_t pending_ = 0;
    // Whether the code being emitted can be reached from the code before it.
    bool reachable_ = false;
    // No instruction has been emitted since the last end of a block: `acc`
    // and pending_ are both zero here, on every path.
    bool blockStart_ = true;
    bool needsDispatch_ = false;
    uint32_t extent_;

    // ---- text ---------------------------------------------------------------
    void line(const std::string& code, const Insn* in = nullptr, const char* note = nullptr) {
        std::string l(static_cast<size_t>(depth_), '\t');
        l += code;
        if (opt_.comments && in) {
            if (!code.empty()) {
                const size_t width = static_cast<size_t>(depth_) * 4 + code.size();
                l.append(width < 60 ? 60 - width : 1, ' ');
            }
            l += strprintf("// %08X  %s", in->addr, disassemble(*in).c_str());
            if (note) l += strprintf("  [%s]", note);
        } else if (code.empty()) {
            return;
        }
        body_ += l + "\n";
    }

    void open() {
        line("{");
        ++depth_;
    }
    void close() {
        --depth_;
        line("}");
    }

    // ---- pieces -------------------------------------------------------------
    // An instruction that is neither control flow nor a system instruction.
    void emitPlain(const Insn& in, bool delaySlot) {
        const char* note = delaySlot ? "delay slot" : nullptr;
        if (in.raw == 0) {
            line("", &in, note);
            return;
        }
        const std::string call = semanticCall(in);
        if (!call.empty()) {
            line(call + ";", &in, note);
            return;
        }
        line(strprintf("%s(%s, %s);", delaySlot ? "delegateInDelaySlot" : "delegate", hex(in.addr).c_str(), hex(in.raw).c_str()), &in, note);
        stats_.delegated++;
        stats_.delegatedByOp[in.op]++;
    }

    // The interpreter carries out the rest of the block, from `at`.
    void leave(uint32_t at, const Insn* in, const char* why) {
        line(strprintf("return leave(%s, acc + %uu);", hex(at).c_str(), pending_), in, why);
        stats_.leaves++;
        pending_ = 0;
        reachable_ = false;
        blockStart_ = false;
    }

    // The end of a block. `whole`, when not zero: the branch ending it goes
    // back (when `back` holds) to a place from which the code up to here is an
    // idle loop, and this is what the block costs when it began there.
    void blockEnd(uint32_t eighths, const std::string& next, const char* back = "true", uint32_t whole = 0) {
        if (whole) line(strprintf("PS2N_BLOCK_END_LOOP(acc, %uu, %s, %s, %uu);", eighths, next.c_str(), back, whole));
        else line(strprintf("PS2N_BLOCK_END(acc, %uu, %s);", eighths, next.c_str()));
    }

    // If the code from the target of `branch` up to it, with its delay slot
    // `ds`, is an idle loop (ps2n/waitloop.h): the cost of that code as one
    // block. Otherwise zero. The runtime's interpreter decides the same thing
    // as it goes (TakeBranch in runtime/ee_provider.cpp); the two must agree.
    uint32_t idleLoopCost(const Insn& branch, const Insn& ds) const {
        if (!branch.is(F_BRANCH | F_JUMP)) return 0;
        const uint32_t target = branch.staticTarget();
        if (target > branch.addr || target < f_.start || target >= f_.end || !f_.isLeader(target)) return 0;

        WaitLoopScan scan;
        uint32_t cost = 0;
        for (uint32_t a = target; a < branch.addr; a += 4) {
            const Insn in = insn(a);
            // Only code that runs straight through from the target to the branch.
            if (!in.valid() || in.hasDelaySlot() || isSystem(in) || writesConfig(in)) return 0;
            scan.add(in.raw);
            cost += cycles(in.op);
        }
        scan.add(ds.raw);
        return scan.idle ? cost + cycles(branch.op) + cycles(ds.op) : 0;
    }

    // If a block that begins at the label `a` is a timeout loop
    // (ps2n/waitloop.h), what the runtime does in its place comes first, when
    // a block does begin here: `acc` is zero then, and only then. The
    // runtime's interpreter makes the same test at the start of every block
    // (SkipTimeoutLoop in runtime/ee_provider.cpp); the two must agree.
    void emitTimeoutLoop(uint32_t a) {
        const TimeoutLoop loop = findTimeoutLoop(a, [this](uint32_t addr) { return p_.inCode(addr) ? insn(addr).raw : 0xFFFFFFFFu; });
        if (!loop.found) return;
        stats_.timeoutLoops++;

        if (opt_.comments) line("// A block that begins here is a timeout loop: its time passes without it being run.");
        line("if (acc == 0)");
        open();
        line(strprintf("const uint32_t next = timeoutLoop(%s, %s, %s);", kRegNames[loop.reg], hex(a).c_str(), hex(loop.end).c_str()));
        line("if (next == " + hex(loop.end) + ")");
        open();
        transfer(loop.end);
        close();
        line("if (next != " + hex(a) + ")");
        ++depth_;
        line("return next;");
        --depth_;
        close();
    }

    // A jump to `target`, after the end of a block.
    void transfer(uint32_t target) {
        if (target >= f_.start && target < f_.end && f_.isLeader(target)) {
            line("goto " + label(target) + ";");
            return;
        }
        if (const Function* g = p_.entryAt(target)) {
            line("PS2N_JUMP(" + fnName(g->start) + ", " + hex(target) + ");");
            return;
        }
        // Not an entry of anything compiled: the dispatcher interprets it.
        line("return " + hex(target) + ";");
    }

    // A call of `target` that returns to `ret`, after the end of a block.
    void call(uint32_t target, uint32_t ret) {
        if (const Function* g = p_.entryAt(target)) line("PS2N_CALL(" + fnName(g->start) + ", " + hex(target) + ", " + hex(ret) + ");");
        else line("PS2N_CALL_DYNAMIC(" + hex(target) + ", " + hex(ret) + ");");
    }

    // ---- instructions ---------------------------------------------------------
    // Emits the instruction at `a` and returns the address of the next one to
    // emit. `tail`: `a` is past the function's own instructions.
    uint32_t emitAt(uint32_t a, bool tail) {
        const Insn in = insn(a);
        extent_ = std::max(extent_, a + 4);

        if (!in.valid()) {
            leave(a, &in, "unknown encoding");
            return a + 4;
        }
        if (in.hasDelaySlot()) return emitControl(in, tail);

        if (isSystem(in)) {
            line(strprintf("PS2N_SYSTEM(acc, %uu, %s, %s);", pending_ + cycles(in.op), hex(a).c_str(), hex(in.raw).c_str()), &in);
            pending_ = 0;
            blockStart_ = true;
            return a + 4;
        }

        emitPlain(in, false);
        pending_ += cycles(in.op);
        blockStart_ = false;
        // Compiled code counts time as the EE's Config register stands when it
        // is entered. Once that register is written, the interpreter takes over.
        if (writesConfig(in)) leave(a + 4, nullptr, nullptr);
        return a + 4;
    }

    // A branch or jump at `in.addr` with its delay slot. Returns the address
    // of the next instruction to emit.
    uint32_t emitControl(const Insn& in, bool tail) {
        const uint32_t a = in.addr, next = a + 8;
        const Insn ds = insn(a + 4);
        extent_ = std::max(extent_, next);

        const uint32_t cost = pending_ + cycles(in.op);         // up to and including the branch
        const uint32_t costWithSlot = cost + cycles(ds.op);
        // Something enters at the delay slot: it then runs as an ordinary
        // instruction, from its own label, after this construct.
        const bool slotIsLeader = !tail && a + 4 < f_.end && f_.isLeader(a + 4);
        bool falls = true;   // whether execution can continue at `next`

        if (!p_.inCode(a + 4) || !compilableInDelaySlot(ds)) {
            if (!in.isLikely() || in.isUnconditionalBranch()) {
                // The interpreter carries out the branch together with what follows it.
                leave(a, &in, "its delay slot is not compiled");
                return a + 4;
            }
            // A likely branch only reaches its delay slot when it is taken, so
            // only then is it left to the interpreter. This is what GCC makes
            // of a division: "beql divisor, $zero, 1f; break 7; 1:".
            open();
            line("const bool taken = " + branchCondition(in) + ";", &in);
            line("if (taken)");
            ++depth_;
            line(strprintf("return leave(%s, acc + %uu);", hex(a).c_str(), pending_), &ds, "delay slot, not compiled");
            --depth_;
            if (in.isCall()) line("setLink(RA, " + hex(next) + ");");
            blockEnd(cost, hex(next));
            close();
            stats_.guards++;
            pending_ = 0;
            blockStart_ = true;
            if (slotIsLeader) {
                transfer(next);
                reachable_ = false;
                return a + 4;
            }
            reachable_ = true;
            return next;
        }

        if (opt_.comments) {
            std::string l(static_cast<size_t>(depth_), '\t');
            l += strprintf("// %08X  %s", a, disassemble(in).c_str());
            if (in.is(F_BRANCH | F_JUMP)) l += "  -> " + describe(in.staticTarget());
            body_ += l + "\n";
        }

        const uint32_t idle = idleLoopCost(in, ds);
        if (idle) stats_.idleLoops++;

        switch (in.op) {
        case Op::J:
            emitPlain(ds, true);
            blockEnd(costWithSlot, hex(in.jumpTarget()), "true", idle);
            transfer(in.jumpTarget());
            falls = false;
            break;
        case Op::JAL:
            line("setLink(RA, " + hex(next) + ");");
            emitPlain(ds, true);
            blockEnd(costWithSlot, hex(in.jumpTarget()), "true", idle);
            call(in.jumpTarget(), next);
            break;
        case Op::JR:
            open();
            line(strprintf("const uint32_t target = ru32(%s);", kRegNames[in.rs()]));
            emitPlain(ds, true);
            blockEnd(costWithSlot, "target");
            if (in.rs() == 31) {
                line("return target;");
            } else {
                // Perhaps one of this function's own blocks (a switch table);
                // the entry switch decides.
                line("pc = target;");
                line("goto ps2n_dispatch;");
                needsDispatch_ = true;
            }
            close();
            falls = false;
            break;
        case Op::JALR:
            open();
            line(strprintf("const uint32_t target = ru32(%s);", kRegNames[in.rs()]));
            if (in.rd()) line(strprintf("setLink(%s, %s);", kRegNames[in.rd()], hex(next).c_str()));
            emitPlain(ds, true);
            blockEnd(costWithSlot, "target");
            line("PS2N_CALL_DYNAMIC(target, " + hex(next) + ");");
            close();
            break;
        default: {   // the conditional branches
            const uint32_t target = in.branchTarget();
            const bool link = in.isCall();
            if (in.isUnconditionalBranch()) {
                if (link) line("setLink(RA, " + hex(next) + ");");
                emitPlain(ds, true);
                blockEnd(costWithSlot, hex(target), "true", idle);
                if (link) {
                    call(target, next);
                } else {
                    transfer(target);
                    falls = false;
                }
            } else if (in.isLikely()) {
                // The delay slot only runs, and only counts, when the branch is taken.
                open();
                line("const bool taken = " + branchCondition(in) + ";");
                if (link) line("setLink(RA, " + hex(next) + ");");
                line("if (taken)");
                open();
                emitPlain(ds, true);
                blockEnd(costWithSlot, hex(target), "true", idle);
                if (link) call(target, next);
                else transfer(target);
                close();
                line("else");
                open();
                blockEnd(cost, hex(next));
                close();
                close();
            } else {
                open();
                line("const bool taken = " + branchCondition(in) + ";");
                if (link) line("setLink(RA, " + hex(next) + ");");
                emitPlain(ds, true);
                blockEnd(costWithSlot, "taken ? " + hex(target) + " : " + hex(next), "taken", idle);
                line("if (taken)");
                open();
                if (link) call(target, next);
                else transfer(target);
                close();
                close();
            }
            break;
        }
        }

        pending_ = 0;
        blockStart_ = true;
        if (slotIsLeader) {
            // Step over the delay slot's own copy, which comes next.
            if (falls) transfer(next);
            reachable_ = false;
            return a + 4;
        }
        reachable_ = falls;
        return next;
    }

    std::string describe(uint32_t addr) const {
        if (const Function* g = p_.functionContaining(addr)) {
            if (addr == g->start) return g->displayName();
            return g->displayName() + strprintf("+0x%x", addr - g->start);
        }
        return strprintf("%08X (not compiled)", addr);
    }

    // ---- the function ---------------------------------------------------------
    void emitBody() {
        uint32_t a = f_.start;
        while (a < f_.end) {
            if (f_.isLeader(a)) {
                if (reachable_ && pending_) line(strprintf("acc += %uu;", pending_));
                // Entered through the label, `acc` is zero: a block has just ended.
                blockStart_ = !reachable_ || blockStart_;
                pending_ = 0;
                reachable_ = true;
                body_ += label(a) + ":\n";
                emitTimeoutLoop(a);
            } else if (!reachable_) {
                // Follows code that never continues here, and nothing is known
                // to jump here. If something does at run time, the interpreter
                // runs it and reports the address.
                const Insn in = insn(a);
                if (in.raw != 0) line("", &in, "not reachable");
                a += 4;
                continue;
            }
            a = emitAt(a, false);
        }

        if (!reachable_) return;
        if (blockStart_) {
            // Between two blocks: the code that follows is entered as usual.
            if (opt_.comments) line("// continues in the code that follows");
            transfer(a);
            return;
        }

        // In the middle of a block. The block is the unit of time, so the
        // function carries on through the code that follows it, compiled here
        // a second time, until the block ends.
        stats_.tails++;
        if (opt_.comments) line("// runs on into the code that follows, to the end of the block");
        for (int n = 0;; ++n) {
            if (!p_.inCode(a) || n >= kMaxTail) {
                leave(a, nullptr, nullptr);
                return;
            }
            const uint32_t before = a;
            a = emitAt(a, true);
            stats_.tailInstructions += (a - before) / 4;
            if (!reachable_) return;
            if (blockStart_) {
                transfer(a);
                return;
            }
        }
    }
};

} // namespace

uint32_t pcsx2Crc(const ElfFile& elf) {
    const std::vector<uint8_t>& b = elf.bytes();
    uint32_t crc = 0;
    for (size_t i = 0; i + 4 <= b.size(); i += 4)
        crc ^= static_cast<uint32_t>(b[i]) | static_cast<uint32_t>(b[i + 1]) << 8 | static_cast<uint32_t>(b[i + 2]) << 16 |
               static_cast<uint32_t>(b[i + 3]) << 24;
    return crc;
}

std::string identifierFor(const std::string& name) {
    std::string id;
    for (char c : name) {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        id.push_back(keep ? c : '_');
    }
    if (id.empty()) id = "image";
    return id;
}

std::vector<std::string> generatedSuffixes(unsigned parts) {
    std::vector<std::string> suffixes = {".cpp"};
    if (parts > 1) {
        suffixes.push_back(".h");
        for (unsigned i = 1; i <= parts; ++i) suffixes.push_back(strprintf("_part%03u.cpp", i));
    }
    return suffixes;
}

std::string generate(const Program& prog, const GenerateOptions& options, GenerateStats* statsOut) {
    if (options.parts != 1) throw Error("generate() makes one translation unit; use generateFiles() for parts");
    return generateFiles(prog, options, statsOut).front().text;
}

std::vector<GeneratedFile> generateFiles(const Program& prog, const GenerateOptions& options, GenerateStats* statsOut) {
    if (options.id.empty() || identifierFor(options.id) != options.id) throw Error("'" + options.id + "' cannot name a generated namespace");
    if (prog.functions.empty()) throw Error(prog.elf->name() + ": no functions to compile");
    if (options.parts < 1 || options.parts > 999) throw Error("the number of parts must be between 1 and 999");
    const bool split = options.parts > 1;
    if (split && options.headerName.empty()) throw Error("code split into parts needs a name for its header");

    GenerateStats stats;
    stats.functions = prog.functions.size();
    stats.instructions = prog.instructionCount;
    stats.leaders = prog.leaderCount;

    const uint32_t crc = pcsx2Crc(*prog.elf);
    const size_t count = prog.functions.size();
    stats.variants = prog.variantCount;

    const std::string banner =
        strprintf("// Generated by %s from %s (CRC %08X). Do not edit.\n", kGeneratorName, prog.elf->name().c_str(), crc) +
        "//\n// This file is derived from the executable it names and is under that\n// executable's terms, not PS2Native's.\n\n";
    const std::string usings = "using namespace ps2n::ee;\nusing namespace ps2n::ee::ops;\nusing namespace ps2n::ee::regs;\n\n";
    const std::string open = "namespace ps2n_image_" + options.id + "\n{\n\n";
    const std::string close = "} // namespace ps2n_image_" + options.id + "\n";

    // In one translation unit the functions are local to it. Spread over
    // several, they are declared in a header all of them include.
    std::string declarations;
    for (const Function& f : prog.functions) {
        const char* linkage = split ? "" : "static ";
        declarations += linkage + ("uint32_t " + fnName(f, -1)) + "(uint32_t pc);\n";
        for (size_t v = 0; v < f.variants.size(); ++v)
            declarations += linkage + ("uint32_t " + fnName(f, static_cast<int>(v))) + "(uint32_t pc);\n";
    }

    // The functions of each part: in address order, and of about the same
    // number of instructions each.
    std::vector<std::string> bodies(options.parts);
    uint64_t instructionsBefore = 0, instructionsInAll = 0;
    for (const Function& f : prog.functions) instructionsInAll += (f.end - f.start) / 4;

    // The hash of the words a function, or a variant of it, was compiled from.
    const auto hashOf = [&](const Function& f, int variant, uint32_t extent) {
        CodeHash hash;
        for (uint32_t a = f.start; a < extent; a += 4) hash.add(prog.word(f, variant, a));
        return static_cast<unsigned long long>(hash.value);
    };

    std::string table = "static const FunctionInfo functions[] = {\n";
    std::string resume = "static const uint32_t resume_points[] = {\n";
    std::string variants = "static const VariantInfo variants[] = {\n";
    uint32_t resumeCount = 0;
    uint32_t variantCount = 0;
    uint32_t textEnd = 0;
    for (const Function& f : prog.functions) {
        std::string& out = bodies[std::min<uint64_t>(options.parts - 1, instructionsBefore * options.parts / instructionsInAll)];
        instructionsBefore += (f.end - f.start) / 4;

        FunctionEmitter emitter(prog, f, -1, options, stats);
        out += emitter.emit();
        out += "\n";

        const uint32_t firstResume = resumeCount;
        for (uint32_t a : f.leaders) {
            if (a == f.start) continue;
            resume += (resumeCount % 6 == 0 ? "\t" : " ") + hex(a) + "," + (resumeCount % 6 == 5 ? "\n" : "");
            ++resumeCount;
        }

        const uint32_t firstVariant = variantCount;
        for (size_t v = 0; v < f.variants.size(); ++v) {
            // Counted apart: a variant is not more of the program, it is the
            // same place in it again.
            GenerateStats variantStats;
            FunctionEmitter variantEmitter(prog, f, static_cast<int>(v), options, variantStats);
            out += variantEmitter.emit();
            out += "\n";
            variants += strprintf("\t{&%s, 0x%016llXull, %s},\n", fnName(f, static_cast<int>(v)).c_str(),
                                  hashOf(f, static_cast<int>(v), variantEmitter.extent()), hex(variantEmitter.extent()).c_str());
            ++variantCount;
        }

        table += strprintf("\t{%s, %s, %s, &%s, 0x%016llXull, %u, %u, %u, %u},\n", hex(f.start).c_str(), hex(f.end).c_str(),
                           hex(emitter.extent()).c_str(), fnName(f, -1).c_str(), hashOf(f, -1, emitter.extent()), firstResume,
                           resumeCount - firstResume, firstVariant, variantCount - firstVariant);
        textEnd = std::max(textEnd, f.end);
    }
    table += "};\n\n";
    if (resumeCount == 0) resume += "\t0,   // (none)\n";
    else if (resumeCount % 6) resume += "\n";
    resume += "};\n\n";
    if (variantCount == 0) variants += "\t{nullptr, 0, 0},   // (none)\n";
    variants += "};\n\n";

    std::string tables = table + resume + variants;
    tables += "const ImageInfo image = {\n";
    tables += "\t" + cstring(options.id) + ",\n";
    tables += "\t" + cstring(prog.elf->name()) + ",\n";
    tables += "\t" + hex(crc) + ",\n";
    tables += "\t" + hex(prog.functions.front().start) + ", " + hex(textEnd) + ",\n";
    tables += strprintf("\tfunctions, %zu,\n", count);
    tables += strprintf("\tresume_points, %u,\n", resumeCount);
    tables += strprintf("\tvariants, %u,\n", variantCount);
    tables += "\tstate,\n";
    tables += "\t" + cstring(kGeneratorName) + ",\n";
    tables += "};\n\n";

    std::vector<GeneratedFile> files;
    if (!split) {
        std::string out = banner;
        out += "#include \"ps2n/recomp.h\"\n\n";
        out += usings + open;
        out += "extern const ImageInfo image;\n";
        out += strprintf("uint8_t state[%zu];\n\n", count);
        out += declarations + "\n";
        out += bodies[0];
        out += tables + close;
        files.push_back({".cpp", std::move(out)});
    } else {
        const std::string include = "#include " + cstring(options.headerName) + "\n\n";

        std::string main = banner + include + usings + open;
        main += strprintf("uint8_t state[%zu];\n\n", count);
        main += tables + close;
        files.push_back({".cpp", std::move(main)});

        std::string header = banner + "#pragma once\n\n#include \"ps2n/recomp.h\"\n\n" + open;
        header += "extern const ps2n::ee::ImageInfo image;\n";
        header += strprintf("extern uint8_t state[%zu];\n\n", count);
        header += declarations + "\n" + close;
        files.push_back({".h", std::move(header)});

        const std::vector<std::string> suffixes = generatedSuffixes(options.parts);
        for (unsigned i = 0; i < options.parts; ++i)
            files.push_back({suffixes[i + 2], banner + include + usings + open + bodies[i] + close});
    }

    if (statsOut) *statsOut = stats;
    return files;
}

std::string generateRegistry(const std::vector<std::string>& ids) {
    std::string out = strprintf("// Generated by %s. Do not edit.\n\n", kGeneratorName);
    out += "#include \"ps2n/recomp.h\"\n\n#include <cstddef>\n\n";
    for (const std::string& id : ids) {
        if (id.empty() || identifierFor(id) != id) throw Error("'" + id + "' cannot name a generated namespace");
        out += "namespace ps2n_image_" + id + " { extern const ps2n::ee::ImageInfo image; }\n";
    }
    out += "\n// The last element is null, so that the array is never empty.\n";
    out += "extern const ps2n::ee::ImageInfo* const ps2n_images[];\n";
    out += "extern const size_t ps2n_image_count;\n\n";
    out += "const ps2n::ee::ImageInfo* const ps2n_images[] = {\n";
    for (const std::string& id : ids) out += "\t&ps2n_image_" + id + "::image,\n";
    out += "\tnullptr,\n};\n";
    out += strprintf("const size_t ps2n_image_count = %zu;\n", ids.size());
    return out;
}

} // namespace ps2n::recomp
