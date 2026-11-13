// SPDX-License-Identifier: GPL-3.0-or-later
#include "codegen.h"

#include "ps2n/codehash.h"
#include "ps2n/rec_rules.h"
#include "ps2n/util.h"
#include "ps2n/waitloop.h"

#include <algorithm>
#include <bit>
#include <initializer_list>
#include <sstream>

namespace ps2n::recomp {

using namespace r5900;

const char* const kGeneratorName = "ps2n-recomp 0.8.0";

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
std::string midLabel(uint32_t a) { return strprintf("M_%08X", a); }
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
// r5900_sem_mmi.inc, r5900_sem_cop2_ctl.inc and r5900_sem_cop0.inc. The same
// tables are what ExecuteOwn() in runtime/ee_provider.cpp includes; an
// instruction of any other family is delegated by both. The VU0 instructions
// (r5900_sem_cop2.inc) are written as calls too, but inside PS2N_VU0
// (vu0Call below).
enum class Arg { RD, RS, RT, SA, SIMM, UIMM, FD, FS, FT, PERF, CREG, VUFLAGS, DEST, BC, FSF, FTF, IMM5 };

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
    case Arg::PERF: return std::to_string(in.perfReg());
    case Arg::CREG: return std::to_string(in.rd());
    case Arg::VUFLAGS: return "PS2N_VU0_FLAGS";
    case Arg::DEST: return std::to_string(in.dest());
    case Arg::BC: return std::to_string(in.bc());
    case Arg::FSF: return std::to_string(in.fsf());
    case Arg::FTF: return std::to_string(in.ftf());
    case Arg::IMM5: return std::to_string(in.imm5());
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
#define PERF Arg::PERF
#define CREG Arg::CREG
#define PS2N_EXPAND(...) __VA_ARGS__
#define S(op, fn, args) \
    case Op::op:        \
        return formatCall(#fn, in, {PS2N_EXPAND args});
    switch (in.op) {
#include "ps2n/r5900_sem_int.inc"
#include "ps2n/r5900_sem_cop1.inc"
#include "ps2n/r5900_sem_mmi.inc"
#include "ps2n/r5900_sem_cop2_ctl.inc"
#include "ps2n/r5900_sem_cop0.inc"
    default: return {};
    }
#undef S
#undef PS2N_EXPAND
#undef CREG
#undef PERF
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

// The call of ps2n::ee::vu0 that carries out the VU0 instruction `in`, with
// PS2N_VU0_FLAGS where its flags go; nothing for a COP2 instruction that is
// not one (a transfer, VCALLMS, VCALLMSR).
std::string vu0Call(const Insn& in) {
#define VUFLAGS Arg::VUFLAGS
#define DEST Arg::DEST
#define FD Arg::FD
#define FS Arg::FS
#define FT Arg::FT
#define BC Arg::BC
#define FSF Arg::FSF
#define FTF Arg::FTF
#define IMM5 Arg::IMM5
#define PS2N_EXPAND(...) __VA_ARGS__
#define S(op, fn, args) \
    case Op::op:        \
        return formatCall(#fn, in, {PS2N_EXPAND args});
    switch (in.op) {
#include "ps2n/r5900_sem_cop2.inc"
    default: return {};
    }
#undef S
#undef PS2N_EXPAND
#undef IMM5
#undef FTF
#undef FSF
#undef BC
#undef FT
#undef FS
#undef FD
#undef DEST
#undef VUFLAGS
}

// The general registers an instruction that is carried out through register
// numbers may read, and those it may write: the ones its fields name. The
// fields of some instructions name other things (a floating-point register,
// a shift amount), so both sets can be too large, which costs a little and
// breaks nothing.
uint32_t fieldMask(const Insn& in) { return ((1u << in.rs()) | (1u << in.rt()) | (1u << in.rd())) & ~1u; }
uint32_t writtenMask(const Insn& in) { return ((1u << in.rt()) | (1u << in.rd())) & ~1u; }

// What a loop that may reach a fixed point (FunctionEmitter::fixedLoopBranch)
// may hold: an instruction that computes a general register from others, a
// load or store of one (which compiled code makes on the locals, through the
// address table), a nop or SYNC. Each depends on nothing but the registers it
// reads and the memory it loads, and changes nothing but the register it
// writes and the memory it stores. The general registers `in` reads and
// writes ($zero left out); false for anything else.
bool fixedLoopEffect(const Insn& in, uint32_t& reads, uint32_t& writes) {
    const uint32_t rs = 1u << in.rs(), rt = 1u << in.rt(), rd = 1u << in.rd();
    reads = writes = 0;
    switch (in.op) {
    case Op::ADD: case Op::ADDU: case Op::SUB: case Op::SUBU:
    case Op::DADD: case Op::DADDU: case Op::DSUB: case Op::DSUBU:
    case Op::AND: case Op::OR: case Op::XOR: case Op::NOR: case Op::SLT: case Op::SLTU:
    case Op::SLLV: case Op::SRLV: case Op::SRAV: case Op::DSLLV: case Op::DSRLV: case Op::DSRAV:
        reads = rs | rt;
        writes = rd;
        break;
    case Op::ADDI: case Op::ADDIU: case Op::DADDI: case Op::DADDIU: case Op::SLTI: case Op::SLTIU:
    case Op::ANDI: case Op::ORI: case Op::XORI:
    case Op::LB: case Op::LBU: case Op::LH: case Op::LHU: case Op::LW: case Op::LWU: case Op::LD:
        reads = rs;
        writes = rt;
        break;
    case Op::SLL: case Op::SRL: case Op::SRA: case Op::DSLL: case Op::DSRL: case Op::DSRA:
    case Op::DSLL32: case Op::DSRL32: case Op::DSRA32:
        reads = rt;
        writes = rd;
        break;
    case Op::LUI: writes = rt; break;
    case Op::MOVZ: case Op::MOVN:   // the destination keeps its value unless the condition holds
        reads = rs | rt | rd;
        writes = rd;
        break;
    case Op::SB: case Op::SH: case Op::SW: case Op::SD: reads = rs | rt; break;
    case Op::SYNC: break;
    default:
        if (in.raw != 0) return false;
        break;
    }
    reads &= ~1u;
    writes &= ~1u;
    return true;
}

// A COP2 instruction, LQC2 or SQC2: what the runtime carries out whole,
// because when VU0 is brought up to date first depends on the block.
bool isCop2(const Insn& in) {
    const uint32_t opcode = in.opcode();
    return (opcode == 022 && !in.isControlFlow()) || opcode == 066 || opcode == 076;
}

// VU0's control registers, as CFC2 and CTC2 number them (VUMicroRegs in
// PCSX2's pcsx2/VU.h).
constexpr uint32_t kVu0RegStatus = 16;
constexpr uint32_t kVu0RegMac = 17;
constexpr uint32_t kVu0RegR = 20;
constexpr uint32_t kVu0RegTpc = 26;
constexpr uint32_t kVu0RegFbrst = 28;
constexpr uint32_t kVu0RegVpuStat = 29;
constexpr uint32_t kVu0RegCmsar1 = 31;

// What the generated code does not carry out in a delay slot, so that the
// branch before it is left to the interpreter: a system instruction, an
// unknown encoding, and the coprocessor branches (the other branches PCSX2's
// recompiler drops from a delay slot, and so does this). DI is carried out
// there, where it takes effect at once.
bool compilableInSlot(const Insn& in) {
    if (!in.valid()) return false;
    if (in.isControlFlow()) return droppedFromDelaySlot(in.raw);
    return !isSystem(in);
}

// The name of the C++ function for `f`, or for its variant `variant` (-1: itself).
std::string fnName(const Function& f, int variant) {
    return variant < 0 ? fnName(f.start) : fnName(f.start) + strprintf("_v%d", variant);
}

// Where each function's leaders begin in the image's list of all of them.
struct LeaderIndex {
    std::vector<uint32_t> first;   // by function

    explicit LeaderIndex(const Program& p) {
        uint32_t n = 0;
        for (const Function& f : p.functions) {
            first.push_back(n);
            n += static_cast<uint32_t>(f.leaders.size());
        }
    }

    // The index of leader `addr` of `f`.
    uint32_t of(const Program& p, const Function& f, uint32_t addr) const {
        const auto it = std::lower_bound(f.leaders.begin(), f.leaders.end(), addr);
        return first[p.indexOf(f)] + static_cast<uint32_t>(it - f.leaders.begin());
    }
};

// Which registers a function's local variables hold at some point of its code
// (`valid`), and which of those hold a value the core does not have yet
// (`dirty`): the general registers by number, and the FPU's (bit 32: its
// accumulator). recomp.h, "registers in local variables", says what the
// generated code does with them.
struct Locals {
    uint32_t valid = 0, dirty = 0;
    uint64_t fvalid = 0, fdirty = 0;
    bool operator==(const Locals&) const = default;
    bool empty() const { return !valid && !fvalid; }
};

// What the locals hold where each label of a function is entered, the same
// whichever way it is entered: by the code before it, by a jump, from outside
// the function. Labels not listed hold nothing.
using LabelStates = std::map<uint32_t, Locals>;

// The ways into each label a pass of the generator found, with what the
// locals held on each.
using Arrivals = std::map<uint32_t, std::vector<Locals>>;

// What a pass found, for the next one: the state of each label, and which
// registers the code from each label on reads before it writes them or the
// locals forget them (in `valid` and `fvalid`).
struct LabelInfo {
    LabelStates states;
    LabelStates needs;
    bool operator==(const LabelInfo&) const = default;
};

// The state of each label from the ways into it: what the code after the
// label reads, of what every way in holds. A way in that holds nothing (after
// a call of another function, which may have changed any register) is left
// out, and takes from the core what the label holds, as an entry from outside
// the function does. Nothing is held as changed: a way in puts what it
// changed back first. (Holding what any way in holds, the others taking it
// from the core, was measured too, and was slower: game log, G6.)
LabelStates statesFrom(const Arrivals& arrivals, const LabelStates& needs) {
    LabelStates states;
    for (const auto& [address, ways] : arrivals) {
        const auto need = needs.find(address);
        if (need == needs.end()) continue;
        Locals state;
        bool first = true;
        for (const Locals& way : ways) {
            if (way.empty()) continue;
            state.valid = first ? way.valid : (state.valid & way.valid);
            state.fvalid = first ? way.fvalid : (state.fvalid & way.fvalid);
            first = false;
        }
        state.valid &= need->second.valid;
        state.fvalid &= need->second.fvalid;
        if (!first && !state.empty()) states[address] = state;
    }
    return states;
}

// How many times a function is generated at most to find its labels' states
// before it is generated for good. Each pass starts from what the one before
// found, and what a label's code reads travels back one label a pass; any
// states make correct code, so where they have not settled by then the last
// ones are used.
constexpr int kStatePasses = 3;

class FunctionEmitter {
public:
    // `variant`: which of the function's variants to compile, or -1 for the
    // function as the executable has it. `info`: what the pass before found
    // of its labels.
    FunctionEmitter(const Program& p, const LeaderIndex& leaders, const Function& f, int variant, const GenerateOptions& o,
                    GenerateStats& s, const LabelInfo& info)
        : p_(p), leaders_(leaders), f_(f), variant_(variant), opt_(o), stats_(s), index_(p.indexOf(f)), atLabel_(info.states),
          needIn_(info.needs) {}

    // What this pass found: the ways into each label, and what the code from
    // each label on reads first.
    const Arrivals& arrivals() const { return arrivals_; }
    const LabelStates& needs() const { return needs_; }

    std::string emit() {
        emitBody();
        endRegion();

        std::string out;
        if (variant_ < 0)
            out = strprintf("// %s  %08X-%08X  (%u instructions, from %s)\n", f_.displayName().c_str(), f_.start, f_.end,
                            (f_.end - f_.start) / 4, sourceName(f_.source));
        else
            out = strprintf("// %s  %08X-%08X  variant %d: other code a run found there\n", f_.displayName().c_str(), f_.start, f_.end,
                            variant_);
        // Local to the translation unit when there is only one.
        out += std::string(opt_.parts > 1 ? "" : "static ") + "uint32_t PS2N_CC " + fnName(f_, variant_) + "(uint32_t pc)\n{\n";
        // The eighths of a cycle counted in the block so far; where the block
        // began; what the runtime said of it; the cycle count (recomp.h).
        out += "\tuint32_t acc = 0;\n";
        out += "\t[[maybe_unused]] uint32_t ps2n_start = 0;\n";
        out += "\t[[maybe_unused]] uint32_t ps2n_end = 0;\n";
        out += "\t[[maybe_unused]] uint64_t ps2n_cycle = cpuRegs.cycle;\n";
        // The general registers the function keeps in local variables for a
        // while, and what its loads and stores need (recomp.h, "registers in
        // local variables").
        if (declared_) {
            std::string locals;
            for (uint32_t r = 1; r < 32; ++r)
                if (declared_ & (1u << r)) locals += strprintf("%s g%u = 0", locals.empty() ? "" : ",", r);
            out += "\tuint64_t" + locals + ";\n";
        }
        if (fdeclared_) {
            std::string locals;
            for (uint32_t r = 0; r <= kAcc; ++r)
                if (fdeclared_ & (uint64_t{1} << r)) locals += (locals.empty() ? " " : ", ") + fname(r) + " = 0";
            out += "\tuint32_t" + locals + ";\n";
        }
        if (usesTable_) out += "\tconst vtlb_private::VTLBVirtual* const ps2n_vmap = addressTable();\n";
        if (usesUnused_) out += "\t[[maybe_unused]] uint64_t ps2n_unused = 0;\n";
        // Whether a time round of a loop that may reach a fixed point has
        // changed nothing so far (PS2N_BLOCK_END_FIXED).
        if (usesSame_) out += "\tbool ps2n_same = false;\n";
        if (needsDispatch_) out += "ps2n_dispatch:\n";
        // A call enters at the start; any other entry is the runtime's doing
        // and rare. Without this the switch searches its cases every time.
        // Marked likely, the compiler takes the rest of the function for
        // colder and inlines a few of the FPU's semantics less; measured, the
        // run is faster with the mark than without (game log, G6).
        out += "\tif (pc == " + hex(f_.start) + ") [[likely]]\n\t\tgoto " + entry(f_.start) + ";\n";
        out += "\tswitch (pc)\n\t{\n";
        for (uint32_t a : f_.leaders) out += "\t\tcase " + hex(a) + ": goto " + entry(a) + ";\n";
        if (needsDispatch_) out += "\t\tdefault: PS2N_JUMP(ps2n::ee::jump, pc);\n";
        else out += strprintf("\t\tdefault: ps2n::ee::badEntry(image, %u, pc);\n", index_);
        out += "\t}\n";
        out += body_;
        out += entries_;
        out += "}\n";
        return out;
    }

private:
    const Program& p_;
    const LeaderIndex& leaders_;
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
    // Reached from the code before it, this place is where a block has just
    // ended: `acc` and pending_ are both zero, and the next instruction
    // begins a block.
    bool blockStart_ = true;
    bool needsDispatch_ = false;

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

    // ---- registers in local variables ------------------------------------------
    // Within straight-line code the function keeps general registers in its
    // locals g1 to g31 (recomp.h says why, and what the generated code does
    // with them). Here is what the generator knows at the point it is
    // emitting: which locals hold their register's value, and which of those
    // hold a value the core does not have yet. After anything that may change
    // a register behind the function's back no local holds anything; at a
    // label they hold what the label's state says ("the state at labels",
    // below).
    uint32_t valid_ = 0;
    uint32_t dirty_ = 0;
    uint32_t declared_ = 0;      // every register that has a local in this function
    bool usesTable_ = false;     // ps2n_vmap, for loads and stores
    bool usesUnused_ = false;    // ps2n_unused, for a load into $zero
    bool usesSame_ = false;      // ps2n_same, for a loop that may reach a fixed point

    // The loop of one block that may reach a fixed point (fixedLoopBranch)
    // being emitted: its first instruction and the branch that closes it.
    uint32_t fixedHead_ = 0, fixedBranch_ = 0;
    bool inFixedLoop(uint32_t a) const { return fixedBranch_ != 0 && a >= fixedHead_ && a <= fixedBranch_ + 4; }

    // The FPU's registers the same way, in the locals f0 to f31 and, for the
    // accumulator, facc: bit 32 of these sets. A local holds the register's 32
    // bits.
    static constexpr uint32_t kAcc = 32;
    uint64_t fvalid_ = 0;
    uint64_t fdirty_ = 0;
    uint64_t fdeclared_ = 0;

    Locals locals() const { return {valid_, dirty_, fvalid_, fdirty_}; }
    void restore(const Locals& l) {
        valid_ = l.valid;
        dirty_ = l.dirty;
        fvalid_ = l.fvalid;
        fdirty_ = l.fdirty;
    }

    // Register `r` as a value: its local, taken from the core first when it
    // does not hold the register. $zero reads as a literal, so that the C++
    // compiler sees the constant.
    std::string use(uint32_t r) {
        if (r == 0) return "uint64_t{0}";
        const uint32_t bit = 1u << r;
        if (!(fresh_ & bit)) demand_ |= bit;
        if (!(valid_ & bit)) {
            line(strprintf("PS2N_GET(%u);", r));
            valid_ |= bit;
            declared_ |= bit;
        }
        return strprintf("g%u", r);
    }
    std::string useSigned(uint32_t r) { return r ? "static_cast<int64_t>(" + use(r) + ")" : std::string("int64_t{0}"); }

    // The local that takes a new value of register `r`, which is not $zero.
    std::string def(uint32_t r) {
        const uint32_t bit = 1u << r;
        fresh_ |= bit;
        valid_ |= bit;
        dirty_ |= bit;
        declared_ |= bit;
        return strprintf("g%u", r);
    }

    // FPU register `r` (kAcc: the accumulator) as a value, and the local that
    // takes a new value of it.
    static std::string fname(uint32_t r) { return r == kAcc ? std::string("facc") : strprintf("f%u", r); }
    std::string fuse(uint32_t r) {
        const uint64_t bit = uint64_t{1} << r;
        if (!(ffresh_ & bit)) fdemand_ |= bit;
        if (!(fvalid_ & bit)) {
            line(r == kAcc ? std::string("PS2N_GET_ACC();") : strprintf("PS2N_GET_F(%u);", r));
            fvalid_ |= bit;
            fdeclared_ |= bit;
        }
        return fname(r);
    }
    std::string fdef(uint32_t r) {
        const uint64_t bit = uint64_t{1} << r;
        ffresh_ |= bit;
        fvalid_ |= bit;
        fdirty_ |= bit;
        fdeclared_ |= bit;
        return fname(r);
    }

    // The statements that put back the changed general registers among `mask`
    // and the changed FPU registers among `fmask`.
    std::string putText(uint32_t mask = ~0u, uint64_t fmask = ~uint64_t{0}) const {
        std::string s;
        for (uint32_t r = 1; r < 32; ++r)
            if (dirty_ & mask & (1u << r)) s += strprintf("%sPS2N_PUT(%u);", s.empty() ? "" : " ", r);
        for (uint32_t r = 0; r < 32; ++r)
            if (fdirty_ & fmask & (uint64_t{1} << r)) s += strprintf("%sPS2N_PUT_F(%u);", s.empty() ? "" : " ", r);
        if (fdirty_ & fmask & (uint64_t{1} << kAcc)) s += std::string(s.empty() ? "" : " ") + "PS2N_PUT_ACC();";
        return s;
    }

    // Puts those back: the core has them then. Before anything outside the
    // function looks at a register.
    void put(uint32_t mask = ~0u, uint64_t fmask = ~uint64_t{0}) {
        const std::string s = putText(mask, fmask);
        if (!s.empty()) line(s);
        dirty_ &= ~mask;
        fdirty_ &= ~fmask;
    }

    // The same on a path that leaves from here (inside an `if` that ends in a
    // return or a jump): the path that goes on still has them to put back.
    void putOnTheWayOut() {
        const std::string s = putText();
        if (!s.empty()) line(s);
    }

    // The changed registers as the argument of a macro that puts them back
    // only on the way it seldom takes (PS2N_COP2_MOVE).
    std::string putArgument() const {
        std::string s = putText();
        if (s.empty()) return "(void)0";
        s.pop_back();   // the macro adds the last semicolon
        return s;
    }

    // The locals no longer hold the general registers among `mask`, nor the
    // FPU's among `fmask`: something outside the function may have changed
    // them. What was changed in them has been put back before.
    void forget(uint32_t mask = ~0u, uint64_t fmask = ~uint64_t{0}) {
        fresh_ |= mask;
        ffresh_ |= fmask;
        valid_ &= ~mask;
        dirty_ &= ~mask;
        fvalid_ &= ~fmask;
        fdirty_ &= ~fmask;
    }

    // ---- the state at labels ------------------------------------------------------
    // A label is entered in the same state whichever way it is entered
    // (LabelStates): every way in brings the locals to it first, and an entry
    // from outside the function takes what the label holds from the core
    // (`E_` before the label's address, after the function's code). Nothing
    // is held as changed at a label.
    const LabelStates& atLabel_;
    const LabelStates& needIn_;
    Arrivals arrivals_;
    std::string entries_;

    // What the code from the last label on reads before it writes it or the
    // locals forget it (`demand_`, `fdemand_`), and what it has written or
    // forgotten so far (`fresh_`, `ffresh_`). A label holds only what is
    // read there: anything else it held would be taken from the core for
    // nothing on the ways in that do not hold it.
    LabelStates needs_;
    uint32_t region_ = 0;
    bool inRegion_ = false;
    uint32_t demand_ = 0, fresh_ = 0;
    uint64_t fdemand_ = 0, ffresh_ = 0;

    Locals needAt(uint32_t a) const {
        const auto it = needIn_.find(a);
        return it == needIn_.end() ? Locals{} : it->second;
    }

    // The code from label `a` on begins.
    void beginRegion(uint32_t a) {
        endRegion();
        region_ = a;
        inRegion_ = true;
        demand_ = fresh_ = 0;
        fdemand_ = ffresh_ = 0;
    }
    void endRegion() {
        if (inRegion_) needs_[region_] = Locals{demand_, 0, fdemand_, 0};
        inRegion_ = false;
    }

    // What the locals hold where label `a` is entered.
    Locals stateAt(uint32_t a) const {
        const auto it = atLabel_.find(a);
        return it == atLabel_.end() ? Locals{} : it->second;
    }

    // The statements that take the registers among `mask` and `fmask` from
    // the core.
    std::string getText(uint32_t mask, uint64_t fmask) {
        std::string s;
        for (uint32_t r = 1; r < 32; ++r)
            if (mask & (1u << r)) s += strprintf("%sPS2N_GET(%u);", s.empty() ? "" : " ", r);
        for (uint32_t r = 0; r < 32; ++r)
            if (fmask & (uint64_t{1} << r)) s += strprintf("%sPS2N_GET_F(%u);", s.empty() ? "" : " ", r);
        if (fmask & (uint64_t{1} << kAcc)) s += std::string(s.empty() ? "" : " ") + "PS2N_GET_ACC();";
        declared_ |= mask & ~1u;
        fdeclared_ |= fmask;
        return s;
    }

    // Takes those the locals do not hold yet: they hold them then.
    void take(uint32_t mask, uint64_t fmask) {
        const std::string s = getText(mask & ~valid_ & ~1u, fmask & ~fvalid_);
        if (!s.empty()) line(s);
        valid_ |= mask & ~1u;
        fvalid_ |= fmask;
    }

    // Brings the locals from what they hold here to what label `a` holds, and
    // records this way into it: what was changed goes to the core, what the
    // label does not hold is no longer held, and what it holds and this way
    // does not is taken from the core.
    void arrive(uint32_t a) {
        arrivals_[a].push_back(locals());
        // What the code after that label reads first, this code reads first,
        // unless it has written it.
        const Locals need = needAt(a);
        demand_ |= need.valid & ~fresh_;
        fdemand_ |= need.fvalid & ~ffresh_;
        const Locals want = stateAt(a);
        const uint32_t fresh = fresh_;
        const uint64_t ffresh = ffresh_;
        put();
        forget(~want.valid, ~want.fvalid);
        take(want.valid, want.fvalid);
        fresh_ = fresh;     // (this way out is not the code that goes on here)
        ffresh_ = ffresh;
    }

    // Where an entry from outside at label `a` goes: the label itself, or the
    // way in that first takes from the core what the locals hold there.
    std::string entry(uint32_t a) const { return stateAt(a).empty() ? label(a) : strprintf("E_%08X", a); }

    // Whether the branch `in` is taken. The same conditions as RunRecBranch()
    // in runtime/ee_provider.cpp. Takes the registers it compares first.
    std::string branchCondition(const Insn& in) {
        const auto pair = [&](const char* comparison) {
            const std::string a = use(in.rs());
            const std::string b = use(in.rt());
            return a + comparison + b;
        };
        switch (in.op) {
        case Op::BEQ: case Op::BEQL: return pair(" == ");
        case Op::BNE: case Op::BNEL: return pair(" != ");
        case Op::BLEZ: case Op::BLEZL: return useSigned(in.rs()) + " <= 0";
        case Op::BGTZ: case Op::BGTZL: return useSigned(in.rs()) + " > 0";
        case Op::BLTZ: case Op::BLTZL: case Op::BLTZAL: case Op::BLTZALL: return useSigned(in.rs()) + " < 0";
        case Op::BGEZ: case Op::BGEZL: case Op::BGEZAL: case Op::BGEZALL: return useSigned(in.rs()) + " >= 0";
        case Op::BC0F: case Op::BC0FL: return "!cop0Condition()";
        case Op::BC0T: case Op::BC0TL: return "cop0Condition()";
        case Op::BC1F: case Op::BC1FL: return "!fpuCondition()";
        case Op::BC1T: case Op::BC1TL: return "fpuCondition()";
        case Op::BC2F: case Op::BC2FL: return "!cop2Condition()";
        case Op::BC2T: case Op::BC2TL: return "cop2Condition()";
        default: return {};
        }
    }

    // The return address a call leaves in register `r`.
    void link(uint32_t r, uint32_t address) {
        if (r) line(def(r) + " = " + hex(address) + ";");
    }

    // An instruction that only computes a general register from others, or
    // loads or stores one: carried out on the locals (ps2n::ee::val, and
    // PS2N_LOAD and PS2N_STORE in recomp.h). False when `in` is not one of
    // those; nothing has been emitted then. `after`: the eighths the end of a
    // block has to count if it ends after this instruction.
    bool emitOnLocals(const Insn& in, uint32_t after, bool delaySlot, const char* note) {
        const uint32_t rs = in.rs(), rt = in.rt(), rd = in.rd();

        // `value` becomes register `dest`. For $zero it is dropped: computing
        // it has no other effect.
        const auto assign = [&](uint32_t dest, const std::string& value) {
            if (dest == 0) line("", &in, note);
            else line(def(dest) + " = " + value + ";", &in, note);
        };
        const auto fromTwo = [&](const char* fn, uint32_t dest, uint32_t first, uint32_t second) {
            if (dest == 0) return assign(0, {});
            const std::string a = use(first);
            const std::string b = use(second);
            assign(dest, strprintf("val::%s(%s, %s)", fn, a.c_str(), b.c_str()));
        };
        const auto fromOne = [&](const char* fn, uint32_t dest, uint32_t source, const std::string& immediate) {
            if (dest == 0) return assign(0, {});
            const std::string a = use(source);
            assign(dest, strprintf("val::%s(%s, %s)", fn, a.c_str(), immediate.c_str()));
        };

        // A load of a `type` widened as an `as`, and a store of a `type`. The
        // registers the locals hold stay there even when the address turns
        // out not to be plain memory: nothing on that way reads the EE's
        // registers (recomp.h, PS2N_LOAD).
        const auto address = [&]() {
            const std::string base = use(rs);
            return strprintf("static_cast<uint32_t>(%s) + static_cast<uint32_t>(%d)", base.c_str(), in.simm());
        };
        // In a loop that may reach a fixed point, a load or store that turns
        // out not to be of plain memory keeps this time round from being one
        // (PS2N_BLOCK_END_FIXED), and so does a store of a value other than
        // the one memory holds.
        const bool fixed = inFixedLoop(in.addr);
        const auto load = [&](const char* as, const char* type) {
            const std::string at = address();
            const std::string slow = fixed ? std::string("ps2n_same = false") : std::string("(void)0");
            usesTable_ = true;
            if (rt == 0) usesUnused_ = true;
            // After a read that may be of an EE timer register the block may
            // end; such a read is followed by a leader.
            const bool mayEnd = !delaySlot && isNarrowLoad(in) && p_.entryAt(in.addr + 4);
            if (mayEnd) {
                open();
                line("bool ends = false;");
            }
            const std::string dest = rt ? def(rt) : std::string("ps2n_unused");
            line(strprintf("PS2N_LOAD%s(%s%s, %s, %s, %s, %s, %s);", mayEnd ? "_ENDS" : "", mayEnd ? "ends, " : "", dest.c_str(), as, type,
                           at.c_str(), hex(in.addr).c_str(), slow.c_str()),
                 &in, note);
            if (mayEnd) {
                endAfterLoad(after, in.addr + 4);
                close();
            }
        };
        const auto store = [&](const char* type) {
            const std::string at = address();
            const std::string value = use(rt);
            usesTable_ = true;
            line(strprintf("PS2N_STORE%s(%s, %s, %s, %s, (void)0);", fixed ? "_SAME" : "", type, at.c_str(), value.c_str(), hex(in.addr).c_str()),
                 &in, note);
        };

        const std::string simm = std::to_string(in.simm());
        const std::string uimm = strprintf("0x%Xu", in.uimm());
        const std::string sa = strprintf("%uu", in.sa());

        switch (in.op) {
#define PS2N_RD_RS_RT(op) \
    case Op::op:          \
        fromTwo(#op, rd, rs, rt); \
        return true;
#define PS2N_RT_RS_SIMM(op) \
    case Op::op:            \
        fromOne(#op, rt, rs, simm); \
        return true;
#define PS2N_RT_RS_UIMM(op) \
    case Op::op:            \
        fromOne(#op, rt, rs, uimm); \
        return true;
#define PS2N_RD_RT_SA(op) \
    case Op::op:          \
        fromOne(#op, rd, rt, sa); \
        return true;
#define PS2N_RD_RT_RS(op) \
    case Op::op:          \
        fromTwo(#op, rd, rt, rs); \
        return true;
            PS2N_RD_RS_RT(ADD) PS2N_RD_RS_RT(ADDU) PS2N_RD_RS_RT(SUB) PS2N_RD_RS_RT(SUBU)
            PS2N_RD_RS_RT(DADD) PS2N_RD_RS_RT(DADDU) PS2N_RD_RS_RT(DSUB) PS2N_RD_RS_RT(DSUBU)
            PS2N_RD_RS_RT(AND) PS2N_RD_RS_RT(OR) PS2N_RD_RS_RT(XOR) PS2N_RD_RS_RT(NOR)
            PS2N_RD_RS_RT(SLT) PS2N_RD_RS_RT(SLTU)
            PS2N_RT_RS_SIMM(ADDI) PS2N_RT_RS_SIMM(ADDIU) PS2N_RT_RS_SIMM(DADDI) PS2N_RT_RS_SIMM(DADDIU)
            PS2N_RT_RS_SIMM(SLTI) PS2N_RT_RS_SIMM(SLTIU)
            PS2N_RT_RS_UIMM(ANDI) PS2N_RT_RS_UIMM(ORI) PS2N_RT_RS_UIMM(XORI)
            PS2N_RD_RT_SA(SLL) PS2N_RD_RT_SA(SRL) PS2N_RD_RT_SA(SRA)
            PS2N_RD_RT_SA(DSLL) PS2N_RD_RT_SA(DSRL) PS2N_RD_RT_SA(DSRA)
            PS2N_RD_RT_SA(DSLL32) PS2N_RD_RT_SA(DSRL32) PS2N_RD_RT_SA(DSRA32)
            PS2N_RD_RT_RS(SLLV) PS2N_RD_RT_RS(SRLV) PS2N_RD_RT_RS(SRAV)
            PS2N_RD_RT_RS(DSLLV) PS2N_RD_RT_RS(DSRLV) PS2N_RD_RT_RS(DSRAV)
#undef PS2N_RD_RS_RT
#undef PS2N_RT_RS_SIMM
#undef PS2N_RT_RS_UIMM
#undef PS2N_RD_RT_SA
#undef PS2N_RD_RT_RS
        case Op::LUI:
            assign(rt, "val::LUI(" + uimm + ")");
            return true;
        case Op::MOVZ:
        case Op::MOVN: {
            // The destination keeps its value unless the condition holds.
            if (rd == 0) {
                assign(0, {});
                return true;
            }
            const std::string old = use(rd);
            const std::string a = use(rs);
            const std::string b = use(rt);
            assign(rd, strprintf("val::%s(%s, %s, %s)", in.op == Op::MOVZ ? "MOVZ" : "MOVN", old.c_str(), a.c_str(), b.c_str()));
            return true;
        }
        case Op::LB: load("int8_t", "mem8_t"); return true;
        case Op::LBU: load("uint8_t", "mem8_t"); return true;
        case Op::LH: load("int16_t", "mem16_t"); return true;
        case Op::LHU: load("uint16_t", "mem16_t"); return true;
        case Op::LW: load("int32_t", "mem32_t"); return true;
        case Op::LWU: load("uint32_t", "mem32_t"); return true;
        case Op::LD: load("uint64_t", "mem64_t"); return true;
        case Op::SB: store("mem8_t"); return true;
        case Op::SH: store("mem16_t"); return true;
        case Op::SW: store("mem32_t"); return true;
        case Op::SD: store("mem64_t"); return true;
        default: break;
        }

        // The FPU's instructions, on its registers' locals (ps2n::ee::val in
        // ops_fpu.h). The flags of FCR31 stay in the core: the functions that
        // have any write them there.
        const uint32_t fs = in.fs(), ft = in.ft(), fd = in.fd();
        const auto fassign = [&](uint32_t dest, const std::string& value) { line(fdef(dest) + " = " + value + ";", &in, note); };
        const auto fromRegisters = [&](const char* fn, uint32_t dest) {
            const std::string a = fuse(fs);
            const std::string b = fuse(ft);
            fassign(dest, strprintf("val::%s(%s, %s)", fn, a.c_str(), b.c_str()));
        };
        const auto fromRegister = [&](const char* fn, uint32_t dest, uint32_t source) {
            const std::string a = fuse(source);
            fassign(dest, strprintf("val::%s(%s)", fn, a.c_str()));
        };
        const auto withAccumulator = [&](const char* fn, uint32_t dest) {
            const std::string acc = fuse(kAcc);
            const std::string a = fuse(fs);
            const std::string b = fuse(ft);
            fassign(dest, strprintf("val::%s(%s, %s, %s)", fn, acc.c_str(), a.c_str(), b.c_str()));
        };
        const auto compare = [&](const char* fn) {
            const std::string a = fuse(fs);
            const std::string b = fuse(ft);
            line(strprintf("fpu::setCondition(val::%s(%s, %s));", fn, a.c_str(), b.c_str()), &in, note);
        };

        switch (in.op) {
        case Op::ADD_S: fromRegisters("ADD_S", fd); return true;
        case Op::SUB_S: fromRegisters("SUB_S", fd); return true;
        case Op::MUL_S: fromRegisters("MUL_S", fd); return true;
        case Op::DIV_S: fromRegisters("DIV_S", fd); return true;
        case Op::RSQRT_S: fromRegisters("RSQRT_S", fd); return true;
        case Op::MAX_S: fromRegisters("MAX_S", fd); return true;
        case Op::MIN_S: fromRegisters("MIN_S", fd); return true;
        // The same arithmetic, into the accumulator.
        case Op::ADDA_S: fromRegisters("ADD_S", kAcc); return true;
        case Op::SUBA_S: fromRegisters("SUB_S", kAcc); return true;
        case Op::MULA_S: fromRegisters("MUL_S", kAcc); return true;
        case Op::MADD_S: withAccumulator("MADD_S", fd); return true;
        case Op::MSUB_S: withAccumulator("MSUB_S", fd); return true;
        case Op::MADDA_S: withAccumulator("MADDA_S", kAcc); return true;
        case Op::MSUBA_S: withAccumulator("MSUB_S", kAcc); return true;
        case Op::SQRT_S: fromRegister("SQRT_S", fd, ft); return true;
        case Op::ABS_S: fromRegister("ABS_S", fd, fs); return true;
        case Op::NEG_S: fromRegister("NEG_S", fd, fs); return true;
        case Op::CVT_W_S: fromRegister("CVT_W_S", fd, fs); return true;
        case Op::CVT_S_W: fromRegister("CVT_S_W", fd, fs); return true;
        case Op::MOV_S: {
            const std::string a = fuse(fs);
            fassign(fd, a);
            return true;
        }
        case Op::C_F_S: line("fpu::setCondition(false);", &in, note); return true;
        case Op::C_EQ_S: compare("C_EQ_S"); return true;
        case Op::C_LT_S: compare("C_LT_S"); return true;
        case Op::C_LE_S: compare("C_LE_S"); return true;
        case Op::MFC1: {
            if (rt == 0) {
                assign(0, {});
                return true;
            }
            const std::string a = fuse(fs);
            assign(rt, "val::MFC1(" + a + ")");
            return true;
        }
        case Op::MTC1: {
            const std::string a = use(rt);
            fassign(fs, "val::MTC1(" + a + ")");
            return true;
        }
        case Op::LWC1: {
            const std::string at = address();
            usesTable_ = true;
            line(strprintf("PS2N_LOAD_F(%s, %s, %s, (void)0);", fdef(ft).c_str(), at.c_str(), hex(in.addr).c_str()), &in, note);
            return true;
        }
        case Op::SWC1: {
            const std::string at = address();
            const std::string value = fuse(ft);
            usesTable_ = true;
            line(strprintf("PS2N_STORE(mem32_t, %s, %s, %s, (void)0);", at.c_str(), value.c_str(), hex(in.addr).c_str()), &in, note);
            return true;
        }
        default: return false;
        }
    }

    // QMFC2, QMTC2, LQC2, SQC2, CFC2 and CTC2, after `cost` eighths not yet
    // in `acc` (theirs included): the move compiled code makes where the
    // block does nothing about VU0 first, or the runtime otherwise
    // (PS2N_COP2_MOVE). The changed registers are put back only on the
    // runtime's way, and on the way of a load or store that is not of plain
    // memory; a general register's local holds its bits 0 to 63, and the core
    // bits 64 to 127. The moves are ops_cop2.cpp's. False, with nothing
    // emitted, for the other COP2 instructions, and for a CTC2 whose writing
    // does more than store (to the status flag register, FBRST, CMSAR1).
    bool emitCop2Move(const Insn& in, uint32_t cost, const char* note) {
        const uint32_t rt = in.rt();
        const std::string pc = hex(in.addr);
        const auto emit = [&](const std::string& puts, const std::string& get, const std::string& move) {
            line(strprintf("PS2N_COP2_MOVE(acc, %uu, %s, %s, %s, %s);", cost, pc.c_str(), puts.c_str(), get.c_str(), move.c_str()), &in, note);
        };
        switch (in.op) {
        case Op::QMFC2: {
            // Into general register rt from VU0's register fs (the rd field).
            const std::string puts = putArgument();
            if (rt == 0) {
                emit(puts, "(void)0", "(void)0");
                return true;
            }
            const uint32_t fs = in.rd();
            const std::string to = def(rt);
            emit(puts, strprintf("PS2N_GET(%u)", rt),
                 strprintf("%s = VU0.VF[%u].UD[0]; PS2N_GPR_HIGH(%u) = VU0.VF[%u].UD[1]", to.c_str(), fs, rt, fs));
            return true;
        }
        case Op::QMTC2: {
            // From general register rt into VU0's register fs, of which 0 is constant.
            const uint32_t fs = in.rd();
            std::string move = "(void)0";
            if (fs != 0) {
                const std::string from = use(rt);
                move = strprintf("VU0.VF[%u].UD[0] = %s; VU0.VF[%u].UD[1] = PS2N_GPR_HIGH(%u)", fs, from.c_str(), fs, rt);
            }
            emit(putArgument(), "(void)0", move);
            return true;
        }
        case Op::LQC2:
        case Op::SQC2: {
            // VU0's register ft (the rt field) from or to memory.
            const std::string base = use(in.rs());
            const std::string at = strprintf("static_cast<uint32_t>(%s) + static_cast<uint32_t>(%d)", base.c_str(), in.simm());
            const std::string puts = putArgument();
            usesTable_ = true;
            emit(puts, "(void)0",
                 strprintf("%s(%u, %s, %s, %s)", in.op == Op::LQC2 ? "PS2N_LQC2" : "PS2N_SQC2", rt, at.c_str(), pc.c_str(), puts.c_str()));
            return true;
        }
        case Op::CFC2: {
            // Into bits 0 to 63 of general register rt from VU0's integer or
            // control register fs (the rd field); bits 64 to 127 stay. An
            // integer register gives 16 bits, R 23, the other control
            // registers 32, sign-extended.
            const std::string puts = putArgument();
            if (rt == 0) {
                emit(puts, "(void)0", "(void)0");
                return true;
            }
            const uint32_t fs = in.rd();
            std::string value;
            if (fs == 0)
                value = "uint64_t{0}";
            else if (fs == kVu0RegR)
                value = strprintf("uint64_t{VU0.VI[%u].UL & 0x7FFFFFu}", fs);
            else if (fs >= kVu0RegStatus)
                value = strprintf("static_cast<uint64_t>(static_cast<int64_t>(VU0.VI[%u].SL))", fs);
            else
                value = strprintf("uint64_t{VU0.VI[%u].US[0]}", fs);
            const std::string to = def(rt);
            emit(puts, strprintf("PS2N_GET(%u)", rt), strprintf("%s = %s", to.c_str(), value.c_str()));
            return true;
        }
        case Op::CTC2: {
            // From general register rt into VU0's integer or control register
            // fs (the rd field). VI0, the MAC flag register, TPC and VPU_STAT
            // take nothing; an integer register takes 16 bits; R 23, with the
            // exponent of 1.0 above them.
            const uint32_t fs = in.rd();
            if (fs == kVu0RegStatus || fs == kVu0RegFbrst || fs == kVu0RegCmsar1)
                return false;
            std::string move = "(void)0";
            if (fs != 0 && fs != kVu0RegMac && fs != kVu0RegTpc && fs != kVu0RegVpuStat) {
                const std::string from = use(rt);
                if (fs == kVu0RegR)
                    move = strprintf("VU0.VI[%u].UL = (static_cast<uint32_t>(%s) & 0x7FFFFFu) | 0x3F800000u", fs, from.c_str());
                else if (fs < kVu0RegStatus)
                    move = strprintf("VU0.VI[%u].US[0] = static_cast<uint16_t>(%s)", fs, from.c_str());
                else
                    move = strprintf("VU0.VI[%u].UL = static_cast<uint32_t>(%s)", fs, from.c_str());
            }
            emit(putArgument(), "(void)0", move);
            return true;
        }
        default: return false;
        }
    }

    // ---- pieces -------------------------------------------------------------
    // An instruction that is neither control flow nor one that ends a block,
    // after `before` eighths not yet in `acc`. Returns the eighths not in
    // `acc` after it: those and its own, or none if it had them added.
    uint32_t emitPlain(const Insn& in, uint32_t before, bool delaySlot) {
        const char* note = delaySlot ? "delay slot" : nullptr;
        const uint32_t cost = before + cycles(in.op);

        // A nop, or a hint to the processor's cache or pipeline: nothing to do.
        if (in.raw == 0 || in.is(F_HINT)) {
            line("", &in, note);
            return cost;
        }

        // When VU0 is brought up to date first, and whether the cycles so far
        // are added before that, depend on the block the instruction is in,
        // which only the runtime knows. A VU0 instruction asks it; where
        // neither is to be done, compiled code carries the instruction out
        // itself, and since it touches no register of the EE's, the locals
        // stay as they are.
        if (isCop2(in)) {
            const std::string vu0 = vu0Call(in);
            if (!vu0.empty()) {
                line(strprintf("PS2N_VU0(acc, %uu, %s, %s);", cost, hex(in.addr).c_str(), vu0.c_str()), &in, note);
                stats_.cop2++;
                stats_.vu0++;
                return 0;
            }

            // A transfer between VU0's registers and the general registers
            // or memory: the same, with the move made by compiled code.
            if (emitCop2Move(in, cost, note)) {
                stats_.cop2++;
                stats_.cop2Moves++;
                return 0;
            }

            // VCALLMS and VCALLMSR: PCSX2's function, which its recompiler
            // calls after adding the cycles of the block so far, in every block
            // (INTERPRETATE_COP2_FUNC). It starts a micro program on VU0 and
            // touches no register of the EE's, so the locals stay as they are.
            if ((in.op == Op::VCALLMS || in.op == Op::VCALLMSR) && !delaySlot) {
                line(strprintf("PS2N_VCALLMS(acc, %uu, %s, 0x%08Xu);", cost, hex(in.addr).c_str(), in.raw), &in, note);
                stats_.cop2++;
                return 0;
            }

            // Otherwise the runtime does all of it. It reads the registers it
            // needs from the core, and writes the one a transfer from VU0 is
            // for.
            put();
            line(strprintf("PS2N_COP2(acc, %uu, %s);", cost, hex(in.addr).c_str()), &in, note);
            forget(writtenMask(in), 0);
            stats_.cop2++;
            return 0;
        }

        uint32_t after = cost;
        if (flushesCycles(in)) {
            line(strprintf("PS2N_FLUSH(acc, %uu);", cost));
            after = 0;
        }

        const std::string call = semanticCall(in);
        if (call.empty()) {
            // The core's function may read and write any register.
            put();
            line(strprintf("%s(%s, %s);", delaySlot ? "PS2N_DELEGATE_IN_SLOT" : "PS2N_DELEGATE", hex(in.addr).c_str(), hex(in.raw).c_str()), &in,
                 note);
            forget();
            stats_.delegated++;
            stats_.delegatedByOp[in.op]++;
            return after;
        }

        if (emitOnLocals(in, after, delaySlot, note)) {
            stats_.onLocals++;
            return after;
        }

        // Carried out through register numbers, on the core's copy of the
        // registers: it has to have what the instruction may read, and the
        // locals no longer hold what it may write. For one of the FPU's
        // instructions that is every FPU register.
        const uint64_t fpu = in.is(F_FPU) ? ~uint64_t{0} : 0;
        put(fieldMask(in), fpu);
        if (in.is(F_LOAD | F_STORE)) {
            // The runtime is told when the access is about to touch memory
            // that is not plain. After a read that may be of an EE timer
            // register the block may end; such a read is followed by a leader.
            const std::string access = strprintf("%s, %d, %s, %s", kRegNames[in.rs()], in.simm(), hex(in.addr).c_str(), call.c_str());
            if (!delaySlot && isNarrowLoad(in) && p_.entryAt(in.addr + 4)) {
                open();
                line("bool ends = false;");
                line("PS2N_ACCESS_ENDS(ends, " + access + ");", &in, note);
                forget(writtenMask(in), fpu);
                endAfterLoad(after, in.addr + 4);
                close();
                return after;
            }
            line("PS2N_ACCESS(" + access + ");", &in, note);
            forget(writtenMask(in), fpu);
            return after;
        }
        line(call + ";", &in, note);
        forget(writtenMask(in), fpu);
        // The moves to and from COP0 call into the core, which is free to let
        // time pass (it does not, in PCSX2 v2.9.170).
        if (in.is(F_COP0)) line("PS2N_TIME();");
        return after;
    }

    // After a read that may have been of an EE timer register (`ends`, set by
    // the runtime): the block ends there, and goes on at `next`. The code
    // after this goes on with the locals as they were.
    void endAfterLoad(uint32_t eighths, uint32_t next) {
        line("if (ends) [[unlikely]]");
        open();
        const Locals here = locals();
        blockEnd(eighths, hex(next));
        transfer(next);
        restore(here);
        close();
    }

    // The interpreter carries out the rest of the block, from `at`.
    void leave(uint32_t at, const Insn* in, const char* why) {
        put();
        line(strprintf("return leave(%s, acc + %uu, ps2n_start);", hex(at).c_str(), pending_), in, why);
        forget();
        stats_.leaves++;
        pending_ = 0;
        reachable_ = false;
        blockStart_ = false;
    }

    // The end of a block. `loop`: the branch ending it goes back (when `back`
    // holds) to a place from which the code up to here may be an idle loop.
    // The core gets the changed registers first, for the event test that may
    // follow. The locals go on holding them: an event test after which the
    // code goes on at `next` has not changed a register (one that takes an
    // interrupt changes where the code goes on instead, and the function
    // returns).
    void blockEnd(uint32_t eighths, const std::string& next, const char* back = "true", bool loop = false) {
        put();
        if (loop) line(strprintf("PS2N_BLOCK_END_LOOP(acc, %uu, %s, %s);", eighths, next.c_str(), back));
        else line(strprintf("PS2N_BLOCK_END(acc, %uu, %s);", eighths, next.c_str()));
    }

    // The end of the block of a loop that may reach a fixed point, at the
    // branch that closes it (fixedLoopBranch).
    void fixedBlockEnd(uint32_t eighths, const std::string& next) {
        put();
        line(strprintf("PS2N_BLOCK_END_FIXED(acc, %uu, %s, %s, %u);", eighths, next.c_str(), hex(fixedHead_).c_str(),
                       leaders_.of(p_, f_, fixedHead_)));
    }

    // Whether the code from the target of `branch` up to it, with its delay
    // slot `ds`, may be an idle loop (ps2n/waitloop.h). The runtime decides
    // whether the block that is running is one (rec_blocks.cpp): it must
    // have begun at the target and not been cut short. This only spares the
    // test where it could never hold.
    bool mayCloseIdleLoop(const Insn& branch, const Insn& ds) const {
        if (!branch.is(F_BRANCH | F_JUMP)) return false;
        const uint32_t target = branch.staticTarget();
        if (target > branch.addr || target < f_.start || target >= f_.end || !f_.isLeader(target)) return false;

        WaitLoopScan scan;
        for (uint32_t a = target; a < branch.addr; a += 4) {
            const Insn in = insn(a);
            // Only code that runs straight through from the target to the branch.
            if (!in.valid() || in.hasDelaySlot() || isSystem(in)) return false;
            scan.add(in.raw);
        }
        scan.add(ds.raw);
        return scan.idle;
    }

    // Whether leader `head` begins a loop of one block that may reach a fixed
    // point: its code runs straight from `head` to a branch back to it, which
    // is neither likely nor a call and tests general registers, and nothing
    // enters it on the way; it holds only what fixedLoopEffect() allows, and
    // no load after which its block may end; and it writes no register it
    // reads before writing it. Once a time round has stored only what memory
    // held, every time round after it until an event test does the same
    // thing, because nothing else runs on the machine in between and the
    // registers it depends on are the same. Compiled code then lets their
    // cycles pass at once (PS2N_BLOCK_END_FIXED). Not for a loop that may be
    // one of PCSX2's idle loops, whose time the recompiler skips otherwise.
    // Returns the address of the branch, or 0.
    uint32_t fixedLoopBranch(uint32_t head) const {
        uint32_t reads = 0, writes = 0;
        const auto add = [&](const Insn& in) {
            uint32_t r = 0, w = 0;
            if (!fixedLoopEffect(in, r, w)) return false;
            reads |= r & ~writes;
            writes |= w;
            return true;
        };

        uint32_t a = head;
        for (;; a += 4) {
            if (a >= f_.end || (a != head && f_.isLeader(a))) return 0;
            const Insn in = insn(a);
            if (!in.valid()) return 0;
            if (in.hasDelaySlot()) break;
            if (isNarrowLoad(in) && p_.entryAt(a + 4)) return 0;
            if (!add(in)) return 0;
        }

        const Insn branch = insn(a);
        if (!branch.is(F_BRANCH) || branch.isLikely() || branch.isCall() || branch.branchTarget() != head) return 0;
        switch (branch.op) {
        case Op::BEQ: case Op::BNE: reads |= ((1u << branch.rs()) | (1u << branch.rt())) & ~writes & ~1u; break;
        case Op::BLEZ: case Op::BGTZ: case Op::BLTZ: case Op::BGEZ: reads |= (1u << branch.rs()) & ~writes & ~1u; break;
        default: return 0;
        }
        if (a + 4 >= f_.end || f_.isLeader(a + 4)) return 0;
        const Insn ds = insn(a + 4);
        if (!ds.valid() || ds.hasDelaySlot() || !add(ds)) return 0;
        if (reads & writes) return 0;
        if (mayCloseIdleLoop(branch, ds)) return 0;
        return a;
    }

    // Whether `after`, the instruction that follows a DI, can be compiled in
    // one piece with it: it is in the code, nothing enters at it, and it is
    // an instruction emitPlain() emits whole (after a load that may end its
    // block, DI's effect would be skipped).
    bool followsDisable(const Insn& after, bool tail) const {
        const uint32_t a = after.addr;
        if (tail ? (!p_.inCode(a) || p_.entryAt(a) != nullptr) : (a >= f_.end || f_.isLeader(a))) return false;
        if (!after.valid() || after.hasDelaySlot() || isSystem(after) || after.op == Op::DI) return false;
        if (isNarrowLoad(after) && p_.entryAt(a + 4)) return false;
        return true;
    }

    // A jump to `target`, where a block begins. The code after it is another
    // way, and goes on with the locals as they were.
    void transfer(uint32_t target) {
        if (target >= f_.start && target < f_.end && f_.isLeader(target)) {
            const Locals here = locals();
            arrive(target);
            line("goto " + label(target) + ";");
            restore(here);
            return;
        }
        // Out of the function: the core gets what was changed.
        putOnTheWayOut();
        if (const Function* g = p_.entryAt(target)) {
            line("PS2N_JUMP(" + fnName(g->start) + ", " + hex(target) + ");");
            return;
        }
        // Not an entry of anything compiled: the dispatcher interprets it.
        line("return " + hex(target) + ";");
    }

    // A call of `target` that returns to `ret`, after the end of a block. The
    // callee may change any register: afterwards the locals hold nothing.
    void call(uint32_t target, uint32_t ret) {
        put();
        if (const Function* g = p_.entryAt(target)) line("PS2N_CALL(" + fnName(g->start) + ", " + hex(target) + ", " + hex(ret) + ");");
        else line("PS2N_CALL_DYNAMIC(" + hex(target) + ", " + hex(ret) + ");");
        forget();
    }

    // What a block does when it begins at leader `a` of this function.
    void blockStart(uint32_t a) {
        const uint32_t index = leaders_.of(p_, f_, a);
        if (variant_ < 0) line(strprintf("PS2N_BLOCK_START(%u, %s);", index, hex(a).c_str()));
        else line(strprintf("PS2N_BLOCK_START_VARIANT(%u, %s, %d);", index, hex(a).c_str(), variant_));
    }

    // ---- instructions ---------------------------------------------------------
    // Emits the instruction at `a` and returns the address of the next one to
    // emit. `tail`: `a` is past the function's own instructions.
    uint32_t emitAt(uint32_t a, bool tail) {
        const Insn in = insn(a);

        if (!in.valid()) {
            leave(a, &in, "unknown encoding");
            return a + 4;
        }
        if (in.hasDelaySlot()) return emitControl(in, tail);

        if (in.op == Op::SYSCALL) {
            // The call is made and the block ends; or it is one PCSX2's
            // recompiler does not make, and the block ends all the same, cut
            // short after it with a fixed cost counted. The runtime does
            // either. The kernel reads and writes the registers in the core.
            put();
            line(strprintf("PS2N_SYSCALL(acc, %uu, %s, %s);", pending_ + cycles(in.op), hex(a).c_str(), hex(in.raw).c_str()), &in);
            forget();
            pending_ = 0;
            blockStart_ = true;
            return a + 4;
        }
        if (isSystem(in)) {
            put();
            line(strprintf("PS2N_SYSTEM(acc, %uu, %s, %s);", pending_ + cycles(in.op), hex(a).c_str(), hex(in.raw).c_str()), &in);
            forget();
            pending_ = 0;
            blockStart_ = true;
            return a + 4;
        }
        if (in.op == Op::DI) {
            // PCSX2's recompiler lets the instruction after it run first, as
            // part of DI's block wherever that block was to end. So the two
            // are compiled as one piece when nothing can separate them: the
            // instruction after is plain and no block begins at it.
            const Insn after = insn(a + 4);
            if (followsDisable(after, tail)) {
                line("", &in, "takes effect after the next instruction");
                pending_ = emitPlain(after, pending_ + cycles(in.op), false);
                line("DI();");
                stats_.delayedDisables++;
                blockStart_ = false;
                return a + 8;
            }
            leave(a, &in, "DI takes effect an instruction late");
            return a + 4;
        }

        pending_ = emitPlain(in, pending_, false);
        blockStart_ = false;
        return a + 4;
    }

    // A branch or jump at `in.addr` with its delay slot. Returns the address
    // of the next instruction to emit.
    uint32_t emitControl(const Insn& in, bool tail) {
        const uint32_t a = in.addr, next = a + 8;
        const Insn ds = insn(a + 4);

        const uint32_t cost = pending_ + cycles(in.op);         // up to and including the branch
        // Something enters at the delay slot: it then runs as an ordinary
        // instruction, from its own label, after this construct.
        const bool slotIsLeader = !tail && a + 4 < f_.end && f_.isLeader(a + 4);
        bool falls = true;   // whether execution can continue at `next`

        if (!p_.inCode(a + 4) || !compilableInSlot(ds)) {
            if (!in.isLikely() || in.isUnconditionalBranch()) {
                // The interpreter carries out the branch together with what follows it.
                leave(a, &in, "its delay slot is not compiled");
                return a + 4;
            }
            // A likely branch only reaches its delay slot when it is taken, so
            // only then is it left to the interpreter. This is what GCC makes
            // of a division: "beql divisor, $zero, 1f; break 7; 1:".
            open();
            if (in.isCall()) link(31, next);
            line("const bool taken = " + branchCondition(in) + ";", &in);
            line("if (taken)");
            open();
            putOnTheWayOut();
            line(strprintf("return leave(%s, acc + %uu, ps2n_start);", hex(a).c_str(), pending_), &ds, "delay slot, not compiled");
            close();
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
        // PCSX2's recompiler drops a branch from a delay slot: the branch
        // before it then has none.
        const bool noSlot = ds.isControlFlow();

        if (opt_.comments) {
            std::string l(static_cast<size_t>(depth_), '\t');
            l += strprintf("// %08X  %s", a, disassemble(in).c_str());
            if (in.is(F_BRANCH | F_JUMP)) l += "  -> " + describe(in.staticTarget());
            body_ += l + "\n";
        }

        // The delay slot, after the branch's `cost`. Returns what the end of
        // the block has left to count.
        const auto slot = [&]() -> uint32_t {
            if (noSlot) {
                line("", &ds, "a branch in a delay slot: dropped");
                return cost;
            }
            return emitPlain(ds, cost, true);
        };

        const bool loop = mayCloseIdleLoop(in, ds);
        if (loop) stats_.idleLoops++;
        // The branch that closes a loop that may reach a fixed point.
        const bool fixedEnd = fixedBranch_ == a;

        switch (in.op) {
        case Op::J: {
            const uint32_t rest = slot();
            blockEnd(rest, hex(in.jumpTarget()), "true", loop);
            transfer(in.jumpTarget());
            falls = false;
            break;
        }
        case Op::JAL: {
            link(31, next);
            const uint32_t rest = slot();
            blockEnd(rest, hex(in.jumpTarget()), "true", loop);
            call(in.jumpTarget(), next);
            break;
        }
        case Op::JR: {
            open();
            const std::string to = use(in.rs());
            line(strprintf("const uint32_t target = static_cast<uint32_t>(%s);", to.c_str()));
            const uint32_t rest = slot();
            blockEnd(rest, "target");
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
        }
        case Op::JALR: {
            open();
            const std::string to = use(in.rs());
            line(strprintf("const uint32_t target = static_cast<uint32_t>(%s);", to.c_str()));
            link(in.rd(), next);
            const uint32_t rest = slot();
            blockEnd(rest, "target");
            line("PS2N_CALL_DYNAMIC(target, " + hex(next) + ");");
            forget();
            close();
            break;
        }
        default: {   // the conditional branches
            const uint32_t target = in.branchTarget();
            const bool linked = in.isCall();
            // The link is written before the register is compared, as PCSX2's
            // recompiler does it.
            if (in.isUnconditionalBranch()) {
                if (linked) link(31, next);
                const uint32_t rest = slot();
                if (fixedEnd) fixedBlockEnd(rest, hex(target));
                else blockEnd(rest, hex(target), "true", loop);
                if (linked) {
                    call(target, next);
                } else {
                    transfer(target);
                    falls = false;
                }
            } else if (in.isLikely()) {
                // The delay slot only runs, and only counts, when the branch is
                // taken. Either way the block ends, and what the locals hold
                // is put back on both sides: on the taken side after the
                // delay slot, with what that changed.
                open();
                if (linked) link(31, next);
                line("const bool taken = " + branchCondition(in) + ";");
                const Locals before = locals();
                line("if (taken)");
                open();
                const uint32_t rest = slot();
                blockEnd(rest, hex(target), "true", loop);
                if (linked) call(target, next);
                else transfer(target);
                close();
                restore(before);
                line("else");
                open();
                blockEnd(cost, hex(next));
                close();
                close();
                // After a call on the taken side both sides go on at `next`,
                // and the callee may have changed any register.
                if (linked) forget();
            } else {
                open();
                if (linked) link(31, next);
                line("const bool taken = " + branchCondition(in) + ";");
                const uint32_t rest = slot();
                if (fixedEnd) fixedBlockEnd(rest, "taken ? " + hex(target) + " : " + hex(next));
                else blockEnd(rest, "taken ? " + hex(target) + " : " + hex(next), "taken", loop);
                line("if (taken)");
                open();
                if (linked) call(target, next);
                else transfer(target);
                close();
                close();
            }
            break;
        }
        }

        pending_ = 0;
        blockStart_ = true;
        fixedHead_ = fixedBranch_ = 0;
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
                // The label can be reached from elsewhere: the code before it,
                // if it goes on into it, first brings the locals to what the
                // label holds.
                const Locals want = stateAt(a);
                if (reachable_) arrive(a);
                beginRegion(a);
                if (reachable_ && !blockStart_) {
                    // Reached in the middle of a block. If this is where the
                    // runtime cut the block short it ends here and another
                    // begins; if not it goes on past the label.
                    line(strprintf("if ((ps2n_end & kBlockEndMask) != %s)", hex(a).c_str()));
                    open();
                    if (pending_) line(strprintf("acc += %uu;", pending_));
                    line("goto " + midLabel(a) + ";");
                    close();
                    line(strprintf("PS2N_SPLIT_END(acc, %uu, %s);", pending_, hex(a).c_str()));
                    body_ += label(a) + ":\n";
                    blockStart(a);
                    body_ += midLabel(a) + ":\n";
                } else {
                    body_ += label(a) + ":\n";
                    blockStart(a);
                }
                // A loop that may reach a fixed point begins here: each time
                // round starts out as one. (Not in a variant, whose blocks the
                // runtime is asked about every time.)
                fixedHead_ = fixedBranch_ = 0;
                if (variant_ < 0) {
                    if (const uint32_t branch = fixedLoopBranch(a)) {
                        fixedHead_ = a;
                        fixedBranch_ = branch;
                        usesSame_ = true;
                        line("ps2n_same = true;");
                        stats_.fixedLoops++;
                    }
                }
                restore(want);
                if (!want.empty()) {
                    // The way in from outside the function.
                    entries_ += strprintf("E_%08X:\n\t%s\n\tgoto %s;\n", a, getText(want.valid, want.fvalid).c_str(), label(a).c_str());
                    stats_.labelsHolding++;
                    stats_.registersAtLabels += static_cast<size_t>(std::popcount(want.valid) + std::popcount(want.fvalid));
                }
                pending_ = 0;
                reachable_ = true;
                blockStart_ = false;
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
        // a second time, until the block ends: at its branch, or at a leader
        // of that code where the runtime cut it short.
        stats_.tails++;
        if (opt_.comments) line("// runs on into the code that follows, to the end of the block");
        for (int n = 0;; ++n) {
            if (!p_.inCode(a) || n >= kMaxTail) {
                leave(a, nullptr, nullptr);
                return;
            }
            if (p_.entryAt(a)) {
                line(strprintf("if ((ps2n_end & kBlockEndMask) == %s)", hex(a).c_str()));
                open();
                const Locals here = locals();
                put();
                line(strprintf("PS2N_SPLIT_END(acc, %uu, %s);", pending_, hex(a).c_str()));
                transfer(a);
                restore(here);
                close();
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

// Function `f`, or its variant `variant`, as C++. It is generated a few times
// first, each time with the labels' states the time before found, to settle
// what the locals hold at each label; then once more, counted in `stats`.
std::string emitFunction(const Program& prog, const LeaderIndex& leaders, const Function& f, int variant, const GenerateOptions& options,
                         GenerateStats& stats) {
    LabelInfo info;
    for (int pass = 0; pass < kStatePasses; ++pass) {
        GenerateStats scratch;
        FunctionEmitter trial(prog, leaders, f, variant, options, scratch, info);
        trial.emit();
        LabelInfo next{statesFrom(trial.arrivals(), trial.needs()), trial.needs()};
        if (next == info) break;
        info = std::move(next);
    }
    FunctionEmitter emitter(prog, leaders, f, variant, options, stats, info);
    return emitter.emit();
}

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
    // A piece of the BIOS that nothing was found in, or an executable that
    // ran no code outside its own, makes an image without code; an
    // executable without code is a mistake.
    if (prog.functions.empty() && options.kind == 0) throw Error(prog.elf->name() + ": no functions to compile");
    if (options.parts < 1 || options.parts > 999) throw Error("the number of parts must be between 1 and 999");
    const bool split = options.parts > 1;
    if (split && options.headerName.empty()) throw Error("code split into parts needs a name for its header");

    GenerateStats stats;
    stats.functions = prog.functions.size();
    stats.instructions = prog.instructionCount;
    stats.leaders = prog.leaderCount;

    const uint32_t crc = options.crc ? *options.crc : pcsx2Crc(*prog.elf);
    const std::string name = options.name.empty() ? prog.elf->name() : options.name;
    const size_t count = prog.functions.size();
    stats.variants = prog.variantCount;

    const std::string banner =
        strprintf("// Generated by %s from %s (CRC %08X). Do not edit.\n", kGeneratorName, name.c_str(), crc) +
        (options.kind == 1 ? "//\n// This file is derived from the BIOS image it names and is under that\n"
                             "// image's terms, not PS2Native's. It may never leave the build tree.\n\n"
                           : "//\n// This file is derived from the executable it names and is under that\n// executable's terms, not PS2Native's.\n\n");
    const std::string usings = "using namespace ps2n::ee;\nusing namespace ps2n::ee::ops;\nusing namespace ps2n::ee::regs;\n\n";
    const std::string open = "namespace ps2n_image_" + options.id + "\n{\n\n";
    const std::string close = "} // namespace ps2n_image_" + options.id + "\n";

    // In one translation unit the functions are local to it. Spread over
    // several, they are declared in a header all of them include.
    std::string declarations;
    for (const Function& f : prog.functions) {
        const char* linkage = split ? "" : "static ";
        declarations += linkage + ("uint32_t PS2N_CC " + fnName(f, -1)) + "(uint32_t pc);\n";
        for (size_t v = 0; v < f.variants.size(); ++v)
            declarations += linkage + ("uint32_t PS2N_CC " + fnName(f, static_cast<int>(v))) + "(uint32_t pc);\n";
    }

    // The functions of each part: in address order, and of about the same
    // number of instructions each.
    std::vector<std::string> bodies(options.parts);
    uint64_t instructionsBefore = 0, instructionsInAll = 0;
    for (const Function& f : prog.functions) instructionsInAll += (f.end - f.start) / 4;

    // The fingerprint of the words from leader `i` of `f`, or of a variant of
    // it, to the next leader or the function's end: what the runtime compares
    // with memory before it lets compiled code run a block.
    const auto segmentHash = [&](const Function& f, int variant, size_t i) {
        const uint32_t end = i + 1 < f.leaders.size() ? f.leaders[i + 1] : f.end;
        CodeHash hash;
        for (uint32_t a = f.leaders[i]; a < end; a += 4) hash.add(prog.word(f, variant, a));
        return static_cast<unsigned long long>(hash.value);
    };

    const LeaderIndex leaderIndex(prog);

    std::string table = "static const FunctionInfo functions[] = {\n";
    std::string leaders = "static const uint32_t leaders[] = {\n";
    std::string hashes = "static const uint64_t segment_hashes[] = {\n";
    std::string variantHashes;
    std::string variants = "static const VariantInfo variants[] = {\n";
    uint32_t leaderCount = 0;
    uint32_t variantCount = 0;
    uint32_t textEnd = 0;
    for (const Function& f : prog.functions) {
        std::string& out = bodies[std::min<uint64_t>(options.parts - 1, instructionsBefore * options.parts / instructionsInAll)];
        instructionsBefore += (f.end - f.start) / 4;

        out += emitFunction(prog, leaderIndex, f, -1, options, stats);
        out += "\n";

        const uint32_t firstLeader = leaderCount;
        for (size_t i = 0; i < f.leaders.size(); ++i) {
            leaders += (leaderCount % 6 == 0 ? "\t" : " ") + hex(f.leaders[i]) + "," + (leaderCount % 6 == 5 ? "\n" : "");
            hashes += strprintf("%s0x%016llXull,%s", leaderCount % 4 == 0 ? "\t" : " ", segmentHash(f, -1, i), leaderCount % 4 == 3 ? "\n" : "");
            ++leaderCount;
        }

        const uint32_t firstVariant = variantCount;
        for (size_t v = 0; v < f.variants.size(); ++v) {
            // Counted apart: a variant is not more of the program, it is the
            // same place in it again.
            GenerateStats variantStats;
            out += emitFunction(prog, leaderIndex, f, static_cast<int>(v), options, variantStats);
            out += "\n";

            const std::string name = strprintf("variant_hashes_%u", variantCount);
            variantHashes += "static const uint64_t " + name + "[] = {\n";
            for (size_t i = 0; i < f.leaders.size(); ++i)
                variantHashes += strprintf("%s0x%016llXull,%s", i % 4 == 0 ? "\t" : " ", segmentHash(f, static_cast<int>(v), i), i % 4 == 3 ? "\n" : "");
            variantHashes += (f.leaders.size() % 4 ? "\n" : "") + std::string("};\n\n");
            variants += strprintf("\t{&%s, %s},\n", fnName(f, static_cast<int>(v)).c_str(), name.c_str());
            ++variantCount;
        }

        table += strprintf("\t{%s, %s, &%s, %u, %u, %u, %u},\n", hex(f.start).c_str(), hex(f.end).c_str(), fnName(f, -1).c_str(), firstLeader,
                           leaderCount - firstLeader, firstVariant, variantCount - firstVariant);
        textEnd = std::max(textEnd, f.end);
    }
    // C++ has no arrays of nothing: an image without code has one element
    // in each table, and counts none.
    if (count == 0) {
        table += "\t{0, 0, nullptr, 0, 0, 0, 0},   // (none)\n";
        leaders += "\t0,   // (none)";
        hashes += "\t0,   // (none)";
    }
    table += "};\n\n";
    if (leaderCount % 6 || count == 0) leaders += "\n";
    leaders += "};\n\n";
    if (leaderCount % 4 || count == 0) hashes += "\n";
    hashes += "};\n\n";
    if (variantCount == 0) variants += "\t{nullptr, nullptr},   // (none)\n";
    variants += "};\n\n";

    std::string tables = table + leaders + hashes + variantHashes + variants;
    tables += "const ImageInfo image = {\n";
    tables += "\t" + cstring(options.id) + ",\n";
    tables += "\t" + cstring(name) + ",\n";
    tables += "\t" + hex(crc) + ",\n";
    tables += "\t" + hex(count ? prog.functions.front().start : 0) + ", " + hex(textEnd) + ",\n";
    tables += strprintf("\tfunctions, %zu,\n", count);
    tables += strprintf("\tleaders, segment_hashes, %u,\n", leaderCount);
    tables += strprintf("\tvariants, %u,\n", variantCount);
    tables += "\tblocks,\n";
    tables += "\t" + cstring(kGeneratorName) + ",\n";
    if (options.kind == 1) tables += "\tkImageBios,\n";
    if (options.kind == 2) tables += "\tkImageOutside,\n";
    tables += "};\n\n";

    // What the runtime knows of the block that begins at each leader
    // (recomp.h); zero until it has said.
    const std::string blocks = strprintf("uint32_t blocks[%u];\n\n", std::max(leaderCount, 1u));

    std::vector<GeneratedFile> files;
    if (!split) {
        std::string out = banner;
        out += "#include \"ps2n/recomp.h\"\n\n";
        out += usings + open;
        out += "extern const ImageInfo image;\n";
        out += blocks;
        out += declarations + "\n";
        out += bodies[0];
        out += tables + close;
        files.push_back({".cpp", std::move(out)});
    } else {
        const std::string include = "#include " + cstring(options.headerName) + "\n\n";

        std::string main = banner + include + usings + open;
        main += blocks;
        main += tables + close;
        files.push_back({".cpp", std::move(main)});

        std::string header = banner + "#pragma once\n\n#include \"ps2n/recomp.h\"\n\n" + open;
        header += "extern const ps2n::ee::ImageInfo image;\n";
        header += strprintf("extern uint32_t blocks[%u];\n\n", std::max(leaderCount, 1u));
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
