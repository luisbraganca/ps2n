// VU micro programs to C++ (see vu_codegen.h).
#include "vu_codegen.h"

#include "ps2n/util.h"
#include "ps2n/vu_micro.h"
#include "ps2n/vu_program.h"

#include <toml.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace ps2n {

namespace {

using namespace ps2n::vu;

constexpr uint64_t kBase = 1000;        // the cycle a block's walk starts at
constexpr int kMaxStates = 16;          // states compiled at one address; past that the interpreter takes over
constexpr int kMaxBlockPairs = 256;     // a long straight run is cut into blocks of this many pairs

std::string hex(uint64_t v, int digits = 8) {
    char b[32];
    std::snprintf(b, sizeof b, "0x%0*llx", digits, static_cast<unsigned long long>(v));
    return b;
}
std::string num(uint64_t v) { return std::to_string(v); }
const char* boolText(bool b) { return b ? "true" : "false"; }

// Where a pair compiled at `pc` sits at run time: the program runs wherever
// micro memory holds it, `base` bytes from where it was compiled.
std::string at(uint32_t pc, uint32_t mask) { return "((" + hex(pc, 4) + "u + base) & " + hex(mask, 4) + "u)"; }
// A jump's target, which the generated code keeps as a compiled address (`jump`), where it sits at run time.
std::string jumpAt(uint32_t mask) { return "((jump + base) & " + hex(mask, 4) + "u)"; }

// The pipeline between two pairs, relative to the cycle: what a block starts
// from. (Timing counts cycles from the start of the program.)
struct State {
    uint8_t count = 0;
    struct Entry {
        uint8_t done = 0;
        bool clip = false, fsset = false;
        auto operator<=>(const Entry&) const = default;
    };
    std::array<Entry, 4> fmac{};
    std::array<std::array<uint8_t, 4>, 32> lands{};
    uint8_t fdiv = 0, efu = 0, ialu = 0;
    uint16_t ialuRegs = 0;
    uint8_t viBackup = 0;
    bool start = false;   // the program's first pair: vf0 and vi0 are set after it
    auto operator<=>(const State&) const = default;
};

Timing timingOf(const State& s) {
    Timing t;
    t.cycle = kBase;
    t.head = 0;
    t.count = s.count;
    for (uint32_t i = 0; i < s.count; ++i) t.fmac[i] = {kBase + s.fmac[i].done, s.fmac[i].clip, s.fmac[i].fsset};
    for (int r = 0; r < 32; ++r)
        for (int c = 0; c < 4; ++c) {
            t.lands[r][c] = s.lands[r][c] ? kBase + s.lands[r][c] : 0;
            t.landsMax[r] = std::max(t.landsMax[r], t.lands[r][c]);
        }
    if (s.fdiv) t.fdiv = {true, kBase + s.fdiv};
    if (s.efu) t.efu = {true, kBase + s.efu};
    if (s.ialu) {
        t.ialu.done = kBase + s.ialu;
        t.ialu.regs = s.ialuRegs;
    }
    t.viBackupCycles = s.viBackup;
    return t;
}

// Between pairs everything in flight lands after the current cycle: each
// pair ends with whatever is due having landed.
State stateOf(const Timing& t) {
    State s;
    s.count = static_cast<uint8_t>(t.count);
    for (uint32_t i = 0; i < t.count; ++i) {
        const Timing::Entry& e = t.fmac[(t.head + i) & 7];
        s.fmac[i] = {static_cast<uint8_t>(e.done - t.cycle), e.clip, e.fsset};
    }
    for (int r = 0; r < 32; ++r)
        for (int c = 0; c < 4; ++c) s.lands[r][c] = t.lands[r][c] > t.cycle ? static_cast<uint8_t>(t.lands[r][c] - t.cycle) : 0;
    if (t.fdiv.busy) s.fdiv = static_cast<uint8_t>(t.fdiv.done - t.cycle);
    if (t.efu.busy) s.efu = static_cast<uint8_t>(t.efu.done - t.cycle);
    if (t.ialu.regs && t.ialu.done > t.cycle) {
        s.ialu = static_cast<uint8_t>(t.ialu.done - t.cycle);
        s.ialuRegs = static_cast<uint16_t>(t.ialu.regs);
    }
    s.viBackup = static_cast<uint8_t>(t.viBackupCycles);
    return s;
}

struct Image {
    int unit;
    const std::vector<uint8_t>& micro;
    uint32_t mask;   // byte mask of micro memory
    uint64_t words(uint32_t pc) const {
        uint64_t w;
        std::memcpy(&w, micro.data() + (pc & mask & ~7u), 8);
        return w;
    }
    Pair pair(uint32_t pc) const {
        const uint64_t w = words(pc);
        return decodePair(static_cast<uint32_t>(w), static_cast<uint32_t>(w >> 32), unit);
    }
    uint32_t next(uint32_t pc) const { return (pc + 8) & mask; }
    // A branch's target: TPC (the pair after it) plus imm11 pairs.
    uint32_t target(uint32_t pc, uint32_t lower) const {
        return static_cast<uint32_t>(static_cast<int32_t>(next(pc)) + imm11(lower) * 8) & mask & ~7u;
    }
    // What BAL and JALR leave in their link register, and the pair it names.
    uint32_t link(uint32_t pc) const { return (next(pc) + 8) / 8; }
    uint32_t linkAddress(uint32_t pc) const { return (link(pc) * 8) & mask; }
};

bool isBranch(const Pair& p) { return p.runLower && p.lower.branch; }
bool lowerRuns(const Pair& p) {
    return p.runLower && p.lower.op != LowerOp::Nop && p.lower.op != LowerOp::Waitq && p.lower.op != LowerOp::Waitp;
}
// Operations that report themselves (unknown(), which names TPC).
bool upperReports(const Pair& p) { return p.upper.op == UpperOp::Unknown; }
bool lowerReports(const Pair& p, int unit) {
    switch (p.lower.op) {
    case LowerOp::Unknown: return true;
    case LowerOp::Xgkick: case LowerOp::Esum: case LowerOp::Eatan2: case LowerOp::Escalar: return unit == 0;
    default: return false;
    }
}

const char* upperName(const Upper& u) {
    switch (u.op) {
    case UpperOp::Nop: return "nop";
    case UpperOp::Fmac: {
        static const char* const kFn[] = {"add", "sub", "mul", "madd", "msub"};
        return kFn[static_cast<int>(u.fn)];
    }
    case UpperOp::MinMax: return u.max ? "max" : "mini";
    case UpperOp::Itof: return "itof";
    case UpperOp::Ftoi: return "ftoi";
    case UpperOp::Abs: return "abs";
    case UpperOp::Clip: return "clipw";
    case UpperOp::Opmula: return "opmula";
    case UpperOp::Opmsub: return "opmsub";
    default: return "unknown";
    }
}
const char* lowerName(LowerOp op) {
    static const char* const kNames[] = {
        "nop", "lq", "sq", "ilw", "isw", "iaddiu", "isubiu", "fceq", "fcset", "fcand", "fcor", "fseq", "fsset", "fsand", "fsor",
        "fmeq", "fmand", "fmor", "fcget", "b", "bal", "jr", "jalr", "ibeq", "ibne", "ibltz", "ibgtz", "iblez", "ibgez",
        "iadd", "isub", "iaddi", "iand", "ior", "move", "mr32", "lqi", "sqi", "lqd", "sqd", "div", "sqrt", "rsqrt", "waitq",
        "mtir", "mfir", "ilwr", "iswr", "rnext", "rget", "rinit", "rxor", "mfp", "xtop", "xitop", "xgkick", "esum",
        "eatanxy", "escalar", "waitp", "unknown"};
    return kNames[static_cast<int>(op)];
}

// Where the straight runs of the image's programs can be entered: the
// entries, branch targets, and the pairs BAL and JALR return to. Blocks start
// there, so a loop entered from above and from its own branch runs the same
// block once its state settles. And where each JR returns to: the pairs after
// the calls whose routine reaches it (a JALR calls a routine the programs
// also start at).
struct Flow {
    std::set<uint32_t> starts;
    std::set<uint32_t> links;
    std::map<uint32_t, std::set<uint32_t>> returns;   // a JR's address -> where it returns to
};
Flow followFlow(const Image& img, const std::vector<uint32_t>& entries) {
    Flow f;
    struct Call {
        uint32_t link;
        std::vector<uint32_t> routines;
    };
    std::vector<Call> calls;
    std::set<uint32_t> walked;
    std::deque<uint32_t> work;
    auto reach = [&](uint32_t pc, bool start) {
        pc &= img.mask & ~7u;
        if (start) f.starts.insert(pc);
        if (walked.insert(pc).second) work.push_back(pc);
    };
    for (uint32_t e : entries) reach(e, true);
    while (!work.empty()) {
        uint32_t pc = work.front();
        work.pop_front();
        for (int n = 0; n <= static_cast<int>(img.mask / 8); ++n) {
            const Pair p = img.pair(pc);
            const uint32_t lower = static_cast<uint32_t>(img.words(pc));
            if (isBranch(p)) {
                const uint32_t after = img.next(img.next(pc));
                switch (p.lower.op) {
                case LowerOp::B: reach(img.target(pc, lower), true); break;
                case LowerOp::Bal:
                    reach(img.target(pc, lower), true);
                    f.links.insert(img.linkAddress(pc));
                    reach(img.linkAddress(pc), true);
                    calls.push_back({img.linkAddress(pc), {img.target(pc, lower)}});
                    break;
                case LowerOp::Jr: break;   // back after a call
                case LowerOp::Jalr:
                    f.links.insert(img.linkAddress(pc));
                    reach(img.linkAddress(pc), true);
                    calls.push_back({img.linkAddress(pc), entries});
                    break;
                default:   // conditional: the target, or on after the delay slot
                    reach(img.target(pc, lower), true);
                    reach(after, true);
                    break;
                }
                break;
            }
            if (p.ebit) break;
            pc = img.next(pc);
            if (walked.count(pc)) break;
            walked.insert(pc);
        }
    }
    // Each call's routine, stepping over the calls it makes itself, up to the
    // JRs that return from it. A routine that calls without saving its link
    // returns to where its own call returned (vi15 now holds that): its JRs
    // may go back after any call it makes as well.
    for (const Call& call : calls) {
        std::set<uint32_t> seen, jrs, inner;
        std::deque<uint32_t> todo(call.routines.begin(), call.routines.end());
        while (!todo.empty()) {
            uint32_t pc = todo.front() & img.mask & ~7u;
            todo.pop_front();
            for (int n = 0; n <= static_cast<int>(img.mask / 8) && seen.insert(pc).second; ++n) {
                const Pair p = img.pair(pc);
                const uint32_t lower = static_cast<uint32_t>(img.words(pc));
                if (isBranch(p)) {
                    if (p.ebit) break;   // the program ends there
                    switch (p.lower.op) {
                    case LowerOp::B: todo.push_back(img.target(pc, lower)); break;
                    case LowerOp::Bal:
                    case LowerOp::Jalr:
                        todo.push_back(img.linkAddress(pc));
                        inner.insert(img.linkAddress(pc));
                        break;
                    case LowerOp::Jr: jrs.insert(pc); break;
                    default:
                        todo.push_back(img.target(pc, lower));
                        todo.push_back(img.next(img.next(pc)));
                        break;
                    }
                    break;
                }
                if (p.ebit) break;
                pc = img.next(pc);
            }
        }
        for (uint32_t jr : jrs) {
            f.returns[jr].insert(call.link);
            f.returns[jr].insert(inner.begin(), inner.end());
        }
    }
    return f;
}

// Writes C++ as the timing calls its hooks (see stepPair): the same calls, in
// the same order, as the interpreter's values.
struct Emitter {
    std::string& out;
    Timing& t;
    std::array<std::string, 8>& slotVar;   // the local holding each ring slot's flags
    int& fresh;
    int unit;
    uint32_t pc = 0, lowerWord = 0, upperWord = 0, tpc = 0, mask = 0;
    const Pair* p = nullptr;

    bool isolating() const { return p->isolate && lowerRuns(*p); }
    void setTpc() { out += "    u.vi[ps2::vu::TPC] = " + at(tpc, mask) + ";\n"; }

    void commit(uint32_t slot) {
        out += "    ps2::vu::commitFlags(u, " + slotVar[slot] + ", " + boolText(t.fmac[slot].clip) + ", " + boolText(t.fmac[slot].fsset) + ");\n";
    }
    void landQ() { out += "    ps2::vu::landQ(u);\n"; }
    void landP() { out += "    ps2::vu::landP(u);\n"; }
    void retire() { retireDue(t, *this); }
    void readQ() { out += "    u.qRead = u.vi[ps2::vu::Q];\n"; }
    void upper() {
        if (isolating() || p->upper.op == UpperOp::Nop) return;
        if (upperReports(*p)) setTpc();
        out += "    PS2_VU_UPPER(" + hex(upperWord) + "u);\n";
    }
    void lower() {
        if (isBranch(*p)) return branch();
        if (isolating()) {
            if (upperReports(*p) || lowerReports(*p, unit)) setTpc();
            out += "    PS2_VU_ISOLATED(" + num(p->isolate) + ", " + hex(upperWord) + "u, " + hex(lowerWord) + "u, " + num(unit) + ");\n";
            return;
        }
        if (!lowerRuns(*p)) return;
        if (lowerReports(*p, unit)) setTpc();
        out += "    PS2_VU_LOWER(" + hex(lowerWord) + "u, " + num(unit) + ");\n";
    }
    void loadI() { out += "    u.vi[ps2::vu::I] = " + hex(lowerWord) + "u;\n"; }
    void issued(uint32_t slot) {
        const std::string name = "f" + num(fresh++);
        out += "    const ps2::vu::Unit::Flags " + name + "{u.macNext, u.statusNext, u.clipNext};\n";
        slotVar[slot] = name;
    }
    // A branch's values: its link, its target or whether it is taken. The
    // block's exit reads `jump` (the target as a compiled address) and
    // `taken` after the delay slot.
    void branch() {
        const std::string link = "((" + at(tpc, mask) + " + 8u) >> 3)";   // the pair after the delay slot, as the interpreter counts it
        const std::string jump = "((static_cast<uint32_t>(ps2::vu::viRead(u, " + num(fFs(lowerWord)) + ")) * 8u) - base) & " + hex(mask & ~7u, 4) + "u";
        switch (p->lower.op) {
        case LowerOp::B: break;
        case LowerOp::Bal: out += "    ps2::vu::viWrite(u, " + num(fFt(lowerWord)) + ", " + link + ", false);\n"; break;
        case LowerOp::Jr: out += "    const uint32_t jump = " + jump + ";\n"; break;
        case LowerOp::Jalr:
            out += "    const uint32_t jump = " + jump + ";\n";
            out += "    ps2::vu::viWrite(u, " + num(fFt(lowerWord)) + ", " + link + ", false);\n";
            break;
        default: out += "    const bool taken = ps2::vu::branchTaken(u, " + hex(lowerWord >> 25, 2) + "u, " + hex(lowerWord) + "u);\n"; break;
        }
    }
};

class ImageCompiler {
public:
    ImageCompiler(const VuImage& vi, std::string fnName, VuCodegenStats& stats)
        : img_{vi.unit, vi.micro, static_cast<uint32_t>(vi.micro.size() - 1)}, fn_(std::move(fnName)), stats_(stats) {
        for (uint32_t e : vi.entries) entries_.insert(e & img_.mask & ~7u);
        for (uint32_t e : vi.targets) targets_.insert(e & img_.mask & ~7u);
        flow_ = followFlow(img_, std::vector<uint32_t>(entries_.begin(), entries_.end()));
        for (uint32_t e : targets_) flow_.starts.insert(e);
    }

    // The function and the static data it uses.
    std::string compile() {
        for (uint32_t e : entries_) {
            State s;
            s.start = true;
            entryLabels_[e] = label(e, s);
        }
        std::string blocks;
        while (!work_.empty()) {
            const auto [pc, s] = work_.front();
            work_.pop_front();
            blocks += block(pc, s);
        }
        std::string out;
        for (const auto& [range, id] : ranges_) {
            // When micro memory last held the words, and where.
            out += "uint64_t " + fn_ + "_valid" + num(id) + " = ~uint64_t(0);\n";
            out += "uint32_t " + fn_ + "_base" + num(id) + " = 0;\n";
            out += "constexpr uint64_t " + fn_ + "_words" + num(id) + "[] = {";
            for (uint32_t i = 0; i < range.second; ++i) {
                if (i % 4 == 0) out += "\n   ";
                out += " " + hex(img_.words(range.first + i * 8), 16) + "u,";
            }
            out += "\n};\n";
        }
        out += "\nuint32_t " + fn_ + "(ps2::vu::Unit& u, uint32_t entry, uint32_t base) {\n";
        out += "    const uint64_t gen = ps2::vu::g_microGeneration[" + num(img_.unit) + "];\n";
        out += "    uint64_t cyc = u.t.cycle;\n";
        out += "    (void)gen;   // (entries that hand over at once use none of these)\n    (void)cyc;\n    (void)base;\n";
        for (int i = 0; i < ringUsed_; ++i) out += "    ps2::vu::Unit::Flags r" + num(i) + "{};\n";
        out += "    switch (entry) {\n";
        for (const auto& [e, id] : entryLabels_) out += "    case " + hex(e, 4) + "u: goto L" + num(id) + ";\n";
        out += "    default: return ps2::vu::kNotRun;\n    }\n";
        out += blocks;
        out += "}\n";
        stats_.entries += entries_.size();
        return out;
    }
    const std::set<uint32_t>& entries() const { return entries_; }

private:
    // The label of the block at `pc` in state `s`, queued if new; -1 if the
    // address has as many states as it may.
    int label(uint32_t pc, const State& s) {
        const auto key = std::make_pair(pc, s);
        if (auto it = labels_.find(key); it != labels_.end()) return it->second;
        if (statesAt_[pc] >= kMaxStates) return -1;
        ++statesAt_[pc];
        const int id = static_cast<int>(labels_.size());
        labels_.emplace(key, id);
        work_.push_back(key);
        return id;
    }
    int range(uint32_t pc, uint32_t pairs) {
        const auto key = std::make_pair(pc, pairs);
        if (auto it = ranges_.find(key); it != ranges_.end()) return it->second;
        const int id = static_cast<int>(ranges_.size());
        ranges_.emplace(key, id);
        return id;
    }

    // Hands the program to the interpreter at `pcExpr`, with the pipeline as
    // `t` has it (`names`: the locals holding the flags in flight, oldest
    // first), `delta` cycles after `cyc`; `why` goes in a comment.
    std::string handover(const Timing& t, const std::vector<std::string>& names, const std::string& pcExpr, uint64_t delta,
                         const std::string& why) {
        ++stats_.handovers;
        std::string o = "    {   // to the interpreter: " + why + "\n        const uint64_t c = cyc + " + num(delta) + ";\n        u.t.cycle = c;\n";
        o += "        u.t.head = 0;\n        u.t.count = " + num(t.count) + ";\n";
        for (uint32_t i = 0; i < t.count; ++i) {
            const Timing::Entry& e = t.fmac[(t.head + i) & 7];
            o += "        u.t.fmac[" + num(i) + "] = {c + " + num(e.done - t.cycle) + ", " + boolText(e.clip) + ", " + boolText(e.fsset) + "};\n";
            o += "        u.ring[" + num(i) + "] = " + names[i] + ";\n";
        }
        for (int r = 0; r < 32; ++r) {
            uint64_t latest = 0;
            for (int c = 0; c < 4; ++c)
                if (t.lands[r][c] > t.cycle) {
                    o += "        u.t.lands[" + num(r) + "][" + num(c) + "] = c + " + num(t.lands[r][c] - t.cycle) + ";\n";
                    latest = std::max(latest, t.lands[r][c] - t.cycle);
                }
            if (latest) o += "        u.t.landsMax[" + num(r) + "] = c + " + num(latest) + ";\n";
        }
        if (t.fdiv.busy) o += "        u.t.fdiv = {true, c + " + num(t.fdiv.done - t.cycle) + "};\n";
        if (t.efu.busy) o += "        u.t.efu = {true, c + " + num(t.efu.done - t.cycle) + "};\n";
        if (t.ialu.regs && t.ialu.done > t.cycle)
            o += "        u.t.ialu.done = c + " + num(t.ialu.done - t.cycle) + ";\n        u.t.ialu.regs = " + hex(t.ialu.regs, 4) + "u;\n";
        o += "        u.vi[ps2::vu::TPC] = " + pcExpr + ";\n        return " + pcExpr + ";\n    }\n";
        return o;
    }

    std::vector<std::string> canonicalNames(uint32_t count) {
        ringUsed_ = std::max(ringUsed_, static_cast<int>(count));
        std::vector<std::string> n;
        for (uint32_t i = 0; i < count; ++i) n.push_back("r" + num(i));
        return n;
    }
    std::vector<std::string> currentNames(const Timing& t, const std::array<std::string, 8>& slotVar) {
        std::vector<std::string> n;
        for (uint32_t i = 0; i < t.count; ++i) n.push_back(slotVar[(t.head + i) & 7]);
        return n;
    }
    // Moves the flags in flight into r0.. (oldest first), as the next block expects them.
    std::string moves(const Timing& t, const std::array<std::string, 8>& slotVar) {
        std::string o;
        canonicalNames(t.count);
        for (uint32_t i = 0; i < t.count; ++i) {
            const std::string& from = slotVar[(t.head + i) & 7];
            if (from != "r" + num(i)) o += "    r" + num(i) + " = " + from + ";\n";
        }
        return o;
    }
    // Goes on at `pc` in state `s` (the flags in flight already in r0..), or
    // hands over there if that address has too many states.
    std::string jumpTo(uint32_t pc, const State& s, const Timing& t) {
        const int id = label(pc, s);
        if (id >= 0) return "    goto L" + num(id) + ";\n";
        return handover(t, canonicalNames(t.count), at(pc, img_.mask), 0, "too many pipeline states at " + hex(pc, 4));
    }

    void pair(std::string& code, Timing& t, std::array<std::string, 8>& slotVar, uint32_t pc) {
        const uint64_t w = img_.words(pc);
        const Pair p = img_.pair(pc);
        const auto lower = static_cast<uint32_t>(w), upper = static_cast<uint32_t>(w >> 32);
        code += "    // " + hex(pc, 4) + ": " + upperName(p.upper) + " " + hex(upper) + " | " +
                (p.ibit ? std::string("I ") + hex(lower) : std::string(lowerName(p.lower.op)) + " " + hex(lower)) +
                (p.ebit ? " [E]" : "") + "\n";
        // The timing counts down the cycles a branch still sees an integer
        // result's old value; viWrite() keeps that count too, at run time.
        if (t.viBackupCycles > 0) code += "    u.t.viBackupCycles = " + num(t.viBackupCycles - 1) + ";\n";
        Emitter e{code, t, slotVar, fresh_, img_.unit};
        e.pc = pc;
        e.lowerWord = lower;
        e.upperWord = upper;
        e.tpc = img_.next(pc);
        e.mask = img_.mask;
        e.p = &p;
        stepPair(t, p, e);
        if (p.runLower && p.lower.intWrite) t.viBackupCycles = 2;
        ++stats_.pairs;
    }

    std::string block(uint32_t start, const State& s0) {
        ++stats_.blocks;
        const int id = labels_.at({start, s0});
        Timing t = timingOf(s0);
        std::array<std::string, 8> slotVar;
        const std::vector<std::string> entryNames = canonicalNames(s0.count);
        for (uint32_t i = 0; i < s0.count; ++i) slotVar[i] = entryNames[i];
        std::string code, exit;
        uint32_t pc = start, pairs = 0;
        const uint32_t mask = img_.mask;
        auto handOverHere = [&](uint32_t where) {
            if (s0.start && pairs == 0) return std::string("    return ps2::vu::kNotRun;\n");
            return handover(t, currentNames(t, slotVar), at(where, mask), t.cycle - kBase, "a branch or E bit in a delay slot");
        };
        for (;;) {
            const Pair p = img_.pair(pc);
            const uint32_t lower = static_cast<uint32_t>(img_.words(pc));
            if (isBranch(p)) {
                const uint32_t slot = img_.next(pc);
                const Pair d = img_.pair(slot);
                if (isBranch(d) || d.ebit) {   // the interpreter follows these
                    exit = handOverHere(pc);
                    break;
                }
                pair(code, t, slotVar, pc);
                pair(code, t, slotVar, slot);
                pairs += 2;
                if (p.ebit) {
                    // The program ends after the delay slot, with TPC where the
                    // branch goes (MSCNT carries on from there).
                    Emitter e{code, t, slotVar, fresh_, img_.unit};
                    flushAll(t, e);
                    std::string to;
                    switch (p.lower.op) {
                    case LowerOp::B:
                    case LowerOp::Bal: to = at(img_.target(pc, lower), mask); break;
                    case LowerOp::Jr:
                    case LowerOp::Jalr: to = jumpAt(mask); break;
                    default: to = "taken ? " + at(img_.target(pc, lower), mask) + " : " + at(img_.next(slot), mask); break;
                    }
                    exit = "    u.t.cycle = cyc + " + num(t.cycle - kBase) + ";\n    u.vi[ps2::vu::TPC] = " + to + ";\n    return ps2::vu::kEnded;\n";
                    break;
                }
                const State s = stateOf(t);
                exit = "    cyc += " + num(t.cycle - kBase) + ";\n" + moves(t, slotVar);
                switch (p.lower.op) {
                case LowerOp::B:
                case LowerOp::Bal: exit += jumpTo(img_.target(pc, lower), s, t); break;
                case LowerOp::Jr:
                case LowerOp::Jalr: {
                    // A JR returns to the calls that reach it; a JALR calls a
                    // routine, which the programs also start at.
                    std::set<uint32_t> targets = targets_;   // recorded where programs went
                    if (p.lower.op == LowerOp::Jalr) targets.insert(entries_.begin(), entries_.end());
                    else if (auto it = flow_.returns.find(pc); it != flow_.returns.end()) targets.insert(it->second.begin(), it->second.end());
                    else targets.insert(flow_.links.begin(), flow_.links.end());
                    std::string cases;
                    for (uint32_t to : targets) {
                        const int target = label(to, s);
                        if (target >= 0) cases += "    case " + hex(to, 4) + "u: goto L" + num(target) + ";\n";
                    }
                    if (!cases.empty()) exit += "    switch (jump) {\n" + cases + "    default: break;\n    }\n";
                    exit += handover(t, canonicalNames(t.count), jumpAt(mask), 0,
                                     p.lower.op == LowerOp::Jr ? "a return to a pair the calls reaching it do not return to"
                                                               : "a call to a pair no program starts at");
                    break;
                }
                default: {
                    const std::string taken = jumpTo(img_.target(pc, lower), s, t);
                    exit += "    if (taken) {\n" + taken + "    }\n" + jumpTo(img_.next(slot), s, t);
                    break;
                }
                }
                break;
            }
            if (p.ebit) {
                const uint32_t slot = img_.next(pc);
                const Pair d = img_.pair(slot);
                if (isBranch(d) || d.ebit) {
                    exit = handOverHere(pc);
                    break;
                }
                pair(code, t, slotVar, pc);
                pair(code, t, slotVar, slot);
                pairs += 2;
                Emitter e{code, t, slotVar, fresh_, img_.unit};
                flushAll(t, e);
                exit = "    u.t.cycle = cyc + " + num(t.cycle - kBase) + ";\n    u.vi[ps2::vu::TPC] = " + at(img_.next(slot), mask) +
                       ";\n    return ps2::vu::kEnded;\n";
                break;
            }
            pair(code, t, slotVar, pc);
            ++pairs;
            pc = img_.next(pc);
            if (flow_.starts.count(pc) || pairs >= kMaxBlockPairs) {
                exit = "    cyc += " + num(t.cycle - kBase) + ";\n" + moves(t, slotVar) + jumpTo(pc, stateOf(t), t);
                break;
            }
        }
        std::string head = "L" + num(id) + ": {   // " + hex(start, 4) + (s0.start ? ", program start" : "") + "\n";
        if (pairs) {
            const int r = range(start, pairs);
            const std::string notHere =
                s0.start ? std::string("    return ps2::vu::kNotRun;\n") : handover(timingOf(s0), entryNames, at(start, mask), 0, "micro memory no longer holds this code");
            const std::string valid = fn_ + "_valid" + num(r), validBase = fn_ + "_base" + num(r);
            head += "    if (" + valid + " != gen || " + validBase + " != base) {\n        if (!ps2::vu::microMatches(u, " + at(start, mask) + ", " + fn_ +
                    "_words" + num(r) + ", " + num(pairs) + "u)) {\n" + notHere + "        }\n        " + valid + " = gen;\n        " + validBase +
                    " = base;\n    }\n";
            if (!s0.start)
                head += "    if (u.steps + " + num(pairs) + "u > ps2::vu::g_stepLimit) {\n" + handover(timingOf(s0), entryNames, at(start, mask), 0, "the step limit") +
                        "    }\n";
            head += "    u.steps += " + num(pairs) + ";\n";
        }
        return head + code + exit + "}\n";
    }

    Image img_;
    std::string fn_;
    VuCodegenStats& stats_;
    std::set<uint32_t> entries_, targets_;
    Flow flow_;
    std::map<std::pair<uint32_t, State>, int> labels_;
    std::map<uint32_t, int> statesAt_;
    std::deque<std::pair<uint32_t, State>> work_;
    std::map<std::pair<uint32_t, uint32_t>, int> ranges_;
    std::map<uint32_t, int> entryLabels_;
    int fresh_ = 0;
    int ringUsed_ = 0;
};

uint64_t fnv1a(const std::vector<uint8_t>& bytes) {
    uint64_t h = 14695981039346656037ull;
    for (uint8_t b : bytes) h = (h ^ b) * 1099511628211ull;
    return h;
}

} // namespace

std::string generateVuCode(const std::vector<VuImage>& images, const std::string& ns, VuCodegenStats* statsOut) {
    VuCodegenStats stats;
    std::string out = "// Generated by ps2n - do not edit; record the game's micro code again and recompile instead.\n"
                      "#include \"ps2/vu_recomp.h\"\n\n"
                      "namespace " + ns + " {\n\nnamespace {\n\n";
    std::string table;
    // The same micro memory recorded in several files (or runs) is one image,
    // with the entries and targets of all of them.
    std::map<uint64_t, VuImage> merged;
    for (const VuImage& vi : images) {
        if (vi.unit != 0 && vi.unit != 1) continue;
        if (vi.micro.size() != (vi.unit ? 16384u : 4096u)) continue;
        VuImage& m = merged[fnv1a(vi.micro) ^ static_cast<uint64_t>(vi.unit)];
        if (m.micro.empty()) {
            m.unit = vi.unit;
            m.micro = vi.micro;
        }
        m.entries.insert(m.entries.end(), vi.entries.begin(), vi.entries.end());
        m.targets.insert(m.targets.end(), vi.targets.begin(), vi.targets.end());
    }
    // A program recorded in several images (the game put it elsewhere, or
    // another run saw it again) is compiled once: it runs wherever micro
    // memory holds it.
    std::set<uint64_t> programs;
    for (auto& [hash, vi] : merged) {
        const auto mask = static_cast<uint32_t>(vi.micro.size() - 1);
        const auto words = [&micro = vi.micro, mask](uint32_t pc) {
            uint64_t w;
            std::memcpy(&w, micro.data() + (pc & mask & ~7u), 8);
            return w;
        };
        std::set<uint32_t> entries;
        for (uint32_t e : vi.entries) entries.insert(e & mask & ~7u);
        vi.entries.clear();
        for (uint32_t e : entries) {
            if (programs.insert(vu::programKey(words, e, mask, vi.unit)).second) vi.entries.push_back(e);
            else ++stats.repeats;
        }
        if (vi.entries.empty()) continue;
        const std::string fn = "vu" + num(vi.unit) + "_" + hex(hash, 16).substr(2);
        ImageCompiler c(vi, fn, stats);
        out += "// VU" + num(vi.unit) + " micro memory " + hex(hash, 16) + "\n" + c.compile() + "\n";
        for (uint32_t e : c.entries())
            table += "    {" + num(vi.unit) + ", " + hex(e, 4) + "u, &" + fn + ", " + hex(vu::entryKey(words, e, mask, vi.unit), 16) + "u},\n";
        ++stats.images;
    }
    out += "} // namespace\n\n";
    out += "extern const ps2::vu::CompiledProgram kVuPrograms[] = {\n" + table + "    {0, 0, nullptr, 0},\n};\n";
    out += "extern const size_t kVuProgramCount = sizeof(kVuPrograms) / sizeof(kVuPrograms[0]) - 1;\n\n";
    out += "} // namespace " + ns + "\n";
    if (statsOut) *statsOut = stats;
    return out;
}

std::vector<VuImage> loadVuCode(const std::filesystem::path& file) {
    toml::table root;
    try {
        root = toml::parse_file(file.string());
    } catch (const toml::parse_error& e) {
        throw Error(file.string() + ":" + std::to_string(e.source().begin.line) + ": " + std::string(e.description()));
    }
    const std::string where = file.string() + ": ";
    std::vector<VuImage> images;
    const toml::array* list = root["image"].as_array();
    if (!list) return images;
    for (const toml::node& n : *list) {
        const toml::table* t = n.as_table();
        if (!t) throw Error(where + "[[image]] entries must be tables");
        VuImage img;
        const auto unit = (*t)["unit"].value<int64_t>();
        if (!unit || (*unit != 0 && *unit != 1)) throw Error(where + "an image's unit must be 0 or 1");
        img.unit = static_cast<int>(*unit);
        img.micro.assign(img.unit ? 16384 : 4096, 0);
        if (const toml::array* entries = (*t)["entries"].as_array())
            for (const toml::node& e : *entries) {
                const auto v = e.value<int64_t>();
                if (!v || *v < 0 || static_cast<uint64_t>(*v) >= img.micro.size() || (*v & 7))
                    throw Error(where + "an entry must be a pair's address in micro memory");
                img.entries.push_back(static_cast<uint32_t>(*v));
            }
        if (const toml::array* targets = (*t)["targets"].as_array())
            for (const toml::node& e : *targets) {
                const auto v = e.value<int64_t>();
                if (!v || *v < 0 || static_cast<uint64_t>(*v) >= img.micro.size() || (*v & 7))
                    throw Error(where + "a target must be a pair's address in micro memory");
                img.targets.push_back(static_cast<uint32_t>(*v));
            }
        const auto code = (*t)["code"].value<std::string>();
        if (!code) throw Error(where + "an image needs its code");
        std::istringstream lines(*code);
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream fields(line);
            std::string at;
            if (!(fields >> at)) continue;
            const unsigned long long base = std::stoull(at, nullptr, 16);
            std::string word;
            for (unsigned long long i = 0; fields >> word; ++i) {
                const unsigned long long offset = base + 8 * i;
                if (offset + 8 > img.micro.size()) throw Error(where + "code past the end of micro memory at " + at);
                const uint64_t pair = std::stoull(word, nullptr, 16);
                std::memcpy(img.micro.data() + offset, &pair, 8);
            }
        }
        images.push_back(std::move(img));
    }
    return images;
}

} // namespace ps2n
