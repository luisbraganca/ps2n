// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysis.h"

#include "ps2n/rec_rules.h"
#include "ps2n/util.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <sstream>

namespace ps2n::recomp {

using namespace r5900;

const char* sourceName(Source s) {
    switch (s) {
    case Source::Symbol: return "ELF symbol";
    case Source::Entry: return "entry point";
    case Source::Jal: return "JAL target";
    case Source::DataRef: return "data pointer";
    case Source::CodePointer: return "code pointer (lui/addiu)";
    case Source::Prologue: return "prologue heuristic";
    case Source::Captured: return "captured entry";
    case Source::Gap: return "unclaimed code";
    }
    return "?";
}

bool Function::isLeader(uint32_t addr) const { return std::binary_search(leaders.begin(), leaders.end(), addr); }

std::string Function::displayName() const { return name.empty() ? strprintf("sub_%08x", start) : name; }

bool Program::inCode(uint32_t addr) const {
    if (addr & 3) return false;
    for (const auto& r : ranges)
        if (r.contains(addr)) return true;
    return false;
}

uint32_t Program::word(uint32_t addr) const {
    uint32_t w = 0;
    elf->read32(addr, w);
    return w;
}

uint32_t Program::word(const Function& f, int variant, uint32_t addr) const {
    if (variant >= 0 && addr >= f.start && addr < f.end) return f.variants[static_cast<size_t>(variant)][(addr - f.start) / 4];
    return word(addr);
}

void Capture::add(const Capture& other) {
    entries.insert(entries.end(), other.entries.begin(), other.entries.end());
    for (const auto& [crc, list] : other.programEntries) {
        std::vector<uint32_t>& mine = programEntries[crc];
        mine.insert(mine.end(), list.begin(), list.end());
    }
    code.insert(code.end(), other.code.begin(), other.code.end());
    blocks.insert(blocks.end(), other.blocks.begin(), other.blocks.end());
}

const Function* Program::functionAt(uint32_t start) const {
    auto it = std::lower_bound(functions.begin(), functions.end(), start, [](const Function& f, uint32_t a) { return f.start < a; });
    return it != functions.end() && it->start == start ? &*it : nullptr;
}

const Function* Program::functionContaining(uint32_t addr) const {
    auto it = std::upper_bound(functions.begin(), functions.end(), addr, [](uint32_t a, const Function& f) { return a < f.start; });
    if (it == functions.begin()) return nullptr;
    --it;
    return addr < it->end ? &*it : nullptr;
}

const Function* Program::entryAt(uint32_t addr) const {
    const Function* f = functionContaining(addr);
    return f && f->isLeader(addr) ? f : nullptr;
}

namespace {

constexpr uint32_t kJrRa = 0x03E00008u;
constexpr uint32_t kSp = 29;

// How many variants the runtime can tell apart: which compiled code a block's
// words are is a byte, with one value for the function itself and two more
// taken (Block::body in runtime/rec_blocks.h).
constexpr size_t kMaxVariants = 253;

// A symbol or the entry point says where a function is; the other sources
// only suggest it.
bool strongBoundary(Source s) { return s == Source::Symbol || s == Source::Entry; }

struct Candidate {
    uint32_t start = 0;
    std::optional<uint32_t> end;
    std::string name;
    Source source = Source::Gap;
};

class Analyzer {
public:
    Analyzer(const ElfFile& elf, const Options& options, Program& prog) : elf_(elf), options_(options), prog_(prog) {}

    void run() {
        prog_.ranges = elf_.codeRanges();
        if (prog_.ranges.empty()) throw Error(elf_.name() + ": no executable code found (no SHF_EXECINSTR sections or PF_X segments)");
        if (options_.followCode) {
            followCode();
        } else {
            collectSymbols();
            scanJalTargets();
            scanDataPointers();
            scanCodePointers();
            scanPrologues();
            classifyPointers();
            buildFunctions();
        }
        attachVariants();
        findLeaders();
        for (const auto& f : prog_.functions) {
            prog_.sourceCounts[f.source]++;
            prog_.instructionCount += (f.end - f.start) / 4;
            prog_.leaderCount += f.leaders.size();
            prog_.variantCount += f.variants.size();
        }
    }

private:
    const ElfFile& elf_;
    const Options& options_;
    Program& prog_;
    std::map<uint32_t, Candidate> candidates_;
    std::set<uint32_t> pointers_;            // code addresses referenced from data or built by lui/addiu pairs
    std::set<uint32_t> codePointerOnly_;     // the subset found only as lui/addiu pairs
    std::set<uint32_t> labelRefs_;           // pointers that are not function starts
    std::set<uint32_t> absorbed_;            // suggested starts inside a function whose size is known

    const AddrRange* rangeOf(uint32_t a) const {
        for (const auto& r : prog_.ranges)
            if (r.contains(a)) return &r;
        return nullptr;
    }
    bool inCode(uint32_t a) const { return (a & 3) == 0 && rangeOf(a) != nullptr; }
    uint32_t word(uint32_t a) const { return prog_.word(a); }

    void addCandidate(uint32_t start, std::optional<uint32_t> end, const std::string& name, Source src) {
        if (!inCode(start)) return;
        auto [it, inserted] = candidates_.try_emplace(start, Candidate{start, end, name, src});
        if (inserted) return;
        Candidate& c = it->second;
        if (src < c.source) {   // the stronger source decides the boundary and the name
            if (!name.empty()) c.name = name;
            if (end) c.end = end;
            c.source = src;
        } else {
            if (c.name.empty()) c.name = name;
            if (!c.end && end) c.end = end;
        }
    }

    // ---- where functions start ------------------------------------------------
    void collectSymbols() {
        if (options_.useSymbols) {
            for (const ElfSymbol& s : elf_.symbols) {
                if (s.name.empty() || !inCode(s.value)) continue;
                if (s.type == elf::STT_FUNC)
                    addCandidate(s.value, s.size ? std::optional<uint32_t>(s.value + s.size) : std::nullopt, s.name, Source::Symbol);
                else if (s.type == elf::STT_NOTYPE && s.bind != elf::STB_LOCAL)
                    addCandidate(s.value, std::nullopt, s.name, Source::Symbol);   // a global label in assembly code
            }
        }
        if (!candidates_.count(elf_.entry)) addCandidate(elf_.entry, std::nullopt, "_start", Source::Entry);
    }

    void scanJalTargets() {
        for (const auto& r : prog_.ranges) {
            for (uint32_t a = r.start; a + 4 <= r.end; a += 4) {
                const uint32_t w = word(a);
                if ((w >> 26) != 0x03) continue;   // JAL
                addCandidate(Insn(w, a).jumpTarget(), std::nullopt, "", Source::Jal);
            }
        }
    }

    void scanDataPointers() {
        for (const auto& r : elf_.dataRanges()) {
            for (uint32_t a = (r.start + 3) & ~3u; a + 4 <= r.end; a += 4) {
                uint32_t v = 0;
                if (elf_.read32(a, v) && inCode(v)) pointers_.insert(v);
            }
        }
    }

    // lui rX, hi ; addiu/ori rY, rX, lo: an address built in code (a callback, a thread's entry).
    void scanCodePointers() {
        for (const auto& r : prog_.ranges) {
            for (uint32_t a = r.start; a + 4 <= r.end; a += 4) {
                const Insn lui(word(a), a);
                if (lui.op != Op::LUI || lui.rt() == 0) continue;
                for (uint32_t b = a + 4; b < a + 4 * 8 && b + 4 <= r.end; b += 4) {
                    const Insn n(word(b), b);
                    if ((n.op == Op::ADDIU || n.op == Op::ORI) && n.rs() == lui.rt()) {
                        const uint32_t hi = lui.uimm() << 16;
                        const uint32_t v = n.op == Op::ADDIU ? hi + static_cast<uint32_t>(n.simm()) : (hi | n.uimm());
                        if (inCode(v)) {
                            if (!pointers_.count(v)) codePointerOnly_.insert(v);
                            pointers_.insert(v);
                        }
                        break;
                    }
                    if (n.writesGpr(lui.rt()) || n.isControlFlow()) break;
                }
            }
        }
    }

    static bool opensFrame(const Insn& in) {
        return (in.op == Op::ADDIU || in.op == Op::DADDIU) && in.rt() == kSp && in.rs() == kSp && in.simm() < 0;
    }

    // "jr $ra", its delay slot, any padding, then an instruction that opens a
    // stack frame.
    void scanPrologues() {
        for (const auto& r : prog_.ranges) {
            for (uint32_t a = r.start; a + 12 <= r.end; a += 4) {
                if (word(a) != kJrRa) continue;
                uint32_t b = a + 8;
                while (b + 4 <= r.end && word(b) == 0) b += 4;
                if (b + 4 > r.end) continue;
                if (opensFrame(Insn(word(b), b))) addCandidate(b, std::nullopt, "", Source::Prologue);
            }
        }
    }

    bool looksLikeFunctionStart(uint32_t a) const {
        const AddrRange* r = rangeOf(a);
        if (!r) return false;
        if (a == r->start) return true;
        uint32_t p = a;
        for (int i = 0; i < 8 && p >= r->start + 8 && word(p - 4) == 0 && word(p - 8) != kJrRa; ++i) p -= 4;   // skip padding
        if (p >= r->start + 8 && word(p - 8) == kJrRa) return true;
        return opensFrame(Insn(word(a), a));
    }

    void classifyPointers() {
        for (uint32_t p : pointers_) {
            if (candidates_.count(p)) continue;
            if (looksLikeFunctionStart(p)) addCandidate(p, std::nullopt, "", codePointerOnly_.count(p) ? Source::CodePointer : Source::DataRef);
            else labelRefs_.insert(p);
        }
    }

    // ---- functions --------------------------------------------------------------
    void buildFunctions() {
        std::vector<Candidate> cs;
        cs.reserve(candidates_.size());
        for (const auto& [a, c] : candidates_) cs.push_back(c);

        std::vector<Function> fns;
        for (size_t i = 0; i < cs.size();) {
            const Candidate& c = cs[i];
            const AddrRange* r = rangeOf(c.start);
            size_t j = i + 1;
            uint32_t end;
            if (c.end && strongBoundary(c.source) && *c.end > c.start) {
                end = std::min(*c.end, r->end);
                while (j < cs.size() && cs[j].start < end) {
                    if (strongBoundary(cs[j].source)) {
                        end = cs[j].start;
                        break;
                    }
                    absorbed_.insert(cs[j].start);
                    ++j;
                }
            } else {
                end = (j < cs.size() && cs[j].start < r->end) ? cs[j].start : r->end;
            }
            Function f;
            f.start = c.start;
            f.end = end;
            f.name = c.name;
            f.source = c.source;
            fns.push_back(std::move(f));
            i = j;
        }

        // Code after a function whose size is known, up to the next start, is
        // padding or a function nothing refers to directly. Code before the
        // first function of a range likewise. Both are kept.
        std::vector<Function> gaps;
        const auto addGap = [&](uint32_t from, uint32_t limit) {
            while (from < limit && word(from) == 0) from += 4;
            if (from >= limit) return;
            Function g;
            g.start = from;
            g.end = limit;
            g.source = Source::Gap;
            gaps.push_back(std::move(g));
        };
        for (size_t i = 0; i < fns.size(); ++i) {
            const AddrRange* r = rangeOf(fns[i].start);
            const uint32_t limit = (i + 1 < fns.size() && fns[i + 1].start < r->end) ? fns[i + 1].start : r->end;
            addGap(fns[i].end, limit);
        }
        for (const auto& r : prog_.ranges) {
            auto first = std::find_if(fns.begin(), fns.end(), [&](const Function& f) { return r.contains(f.start); });
            addGap(r.start, first != fns.end() ? first->start : r.end);
        }
        for (auto& g : gaps) fns.push_back(std::move(g));
        std::sort(fns.begin(), fns.end(), [](const Function& a, const Function& b) { return a.start < b.start; });

        prog_.functions = std::move(fns);
        splitLongFunctions();
    }

    // A function longer than kMaxFunctionInstructions is cut into pieces of
    // about that many, each a function of its own: clang takes the better part of an
    // hour, and gigabytes, over one C++ function of hundreds of thousands of
    // instructions. Silent Hill 3's executable is one segment, its data with
    // its code and no section named, and nothing past its code starts a
    // function: the last function took in a megabyte of data (187,835 words).
    // No function of a game's own has come near the limit (Agent Under Fire's
    // longest, 13,871 instructions). A piece begins after an unconditional
    // jump or a return and its delay slot within kSplitSearch instructions of
    // the limit, or else at the limit, never in a delay slot; one piece runs
    // on into the next as any function whose end is not a jump does.
    static constexpr uint32_t kMaxFunctionInstructions = 16384;
    static constexpr uint32_t kSplitSearch = 1024;

    void splitLongFunctions() {
        std::vector<Function> out;
        out.reserve(prog_.functions.size());
        for (Function& f : prog_.functions) {
            while ((f.end - f.start) / 4 > kMaxFunctionInstructions) {
                const uint32_t limit = f.start + 4 * kMaxFunctionInstructions;
                uint32_t cut = limit;
                for (uint32_t a = limit; a < limit + 4 * kSplitSearch && a + 4 < f.end; a += 4) {
                    if (Insn(word(a - 8), a - 8).endsBlock()) {
                        cut = a;
                        break;
                    }
                }
                if (Insn(word(cut - 4), cut - 4).hasDelaySlot()) cut += 4;
                if (cut >= f.end) break;
                Function head;
                head.start = f.start;
                head.end = cut;
                head.name = f.name;
                head.source = f.source;
                out.push_back(std::move(head));
                f.start = cut;
                f.name.clear();
                f.source = Source::Gap;
            }
            out.push_back(std::move(f));
        }
        prog_.functions = std::move(out);
    }

    // Code an earlier run found in memory in a function's place becomes a
    // variant of that function.
    void attachVariants() {
        for (const Capture::Code& code : options_.capture.code) {
            auto it = std::lower_bound(prog_.functions.begin(), prog_.functions.end(), code.start,
                                       [](const Function& f, uint32_t a) { return f.start < a; });
            if (it == prog_.functions.end() || it->start != code.start || code.words.size() != (it->end - it->start) / 4) {
                prog_.notes.push_back({code.start, strprintf("captured code at %s (%zu words) is not in the place of a function of this "
                                                             "executable; ignored",
                                                             hex32(code.start).c_str(), code.words.size())});
                continue;
            }
            Function& f = *it;
            bool original = true;
            for (size_t i = 0; i < code.words.size(); ++i) original = original && code.words[i] == prog_.word(f.start + 4 * static_cast<uint32_t>(i));
            if (original || std::find(f.variants.begin(), f.variants.end(), code.words) != f.variants.end()) continue;
            if (f.variants.size() >= kMaxVariants) {
                prog_.notes.push_back({code.start, "more captured versions of the code at " + hex32(code.start) + " than a function can have; ignored"});
                continue;
            }
            f.variants.push_back(code.words);
        }
    }

    // ---- an image whose code is followed, not swept ----------------------------------
    std::set<uint32_t> reached_;   // the instructions reached by following the code
    std::set<uint32_t> exits_;     // targets outside the ranges

    // From the entry point, `entries` and the captured entries, through every
    // branch, jump and call: the instructions reached are the code. Functions
    // are the runs of reached instructions, cut where one is entered from
    // elsewhere (an entry, a call's target).
    void followCode() {
        std::map<uint32_t, Source> starts;
        std::vector<uint32_t> work;
        const auto start = [&](uint32_t a, Source s) {
            if (!inCode(a)) {
                if (s != Source::Entry && !(a & 3)) exits_.insert(a);
                return;
            }
            const auto [it, inserted] = starts.try_emplace(a, s);
            if (!inserted && s < it->second) it->second = s;
            work.push_back(a);
        };
        start(elf_.entry, Source::Entry);
        for (uint32_t a : options_.entries) start(a, Source::Entry);
        std::set<uint32_t> captured;
        for (uint32_t a : options_.capture.entries) {
            if (!inCode(a)) continue;
            captured.insert(a);
            work.push_back(a);
        }

        while (!work.empty()) {
            const uint32_t a = work.back();
            work.pop_back();
            follow(a, start, work);
        }

        std::vector<Function> fns;
        for (uint32_t a : reached_) {
            const bool continues = !fns.empty() && fns.back().end == a && rangeOf(a)->start != a;
            const auto s = starts.find(a);
            if (continues && s == starts.end()) {
                fns.back().end = a + 4;
                continue;
            }
            Function f;
            f.start = a;
            f.end = a + 4;
            f.source = s != starts.end() ? s->second : (captured.count(a) ? Source::Captured : Source::Gap);
            fns.push_back(std::move(f));
        }
        prog_.functions = std::move(fns);
        prog_.exits.assign(exits_.begin(), exits_.end());
    }

    // `target` is where the code followed may go next.
    void goTo(uint32_t target, std::vector<uint32_t>& work) {
        if (inCode(target)) work.push_back(target);
        else if (!(target & 3)) exits_.insert(target);
    }

    // Follows the code from `a` until it leaves for good: a return, a jump, an
    // exception return, an encoding the decoder does not know, the end of the
    // ranges, or what was reached before. Where it may also go elsewhere, that
    // is put on `work`; a call's target is a start.
    template <typename Start>
    void follow(uint32_t a, const Start& start, std::vector<uint32_t>& work) {
        while (inCode(a) && !reached_.count(a)) {
            const Insn in(word(a), a);
            if (!in.valid()) return;
            reached_.insert(a);
            if (!in.hasDelaySlot()) {
                if (in.op == Op::ERET) return;
                a += 4;
                continue;
            }
            const uint32_t slot = a + 4;
            if (inCode(slot) && Insn(word(slot), slot).valid()) reached_.insert(slot);
            if (in.is(F_BRANCH)) {
                goTo(in.branchTarget(), work);
                if (in.endsBlock()) return;
                a += 8;
                continue;
            }
            if (in.is(F_JUMP)) {
                if (in.isCall()) {
                    start(in.jumpTarget(), Source::Jal);
                    a += 8;
                    continue;
                }
                goTo(in.jumpTarget(), work);
                return;
            }
            // JR and JALR go where a register says. Where the instructions just
            // before make it a constant, that is followed.
            if (const std::optional<uint32_t> target = constantBefore(a, in.rs())) {
                if (in.isCall()) start(*target, Source::CodePointer);
                else goTo(*target, work);
            }
            if (in.isCall()) {
                a += 8;
                continue;
            }
            return;
        }
    }

    // The value register `reg` holds at `at`, where the straight-line code
    // just before makes it a constant: lui, and then perhaps addiu or ori.
    std::optional<uint32_t> constantBefore(uint32_t at, uint32_t reg) const {
        // The last instruction before `at` (at most eight back) that writes
        // `r`, if no branch or jump comes between.
        const auto writer = [&](uint32_t from, uint32_t r) -> std::optional<Insn> {
            for (uint32_t b = from; b >= 4 && from - b < 4 * 8;) {
                b -= 4;
                if (!inCode(b)) break;
                const Insn in(word(b), b);
                if (!in.valid() || in.isControlFlow()) break;
                if (in.writesGpr(r)) return in;
            }
            return std::nullopt;
        };
        if (reg == 0) return std::nullopt;
        const std::optional<Insn> last = writer(at, reg);
        if (!last) return std::nullopt;
        if (last->op == Op::LUI) return last->uimm() << 16;
        if (last->op != Op::ADDIU && last->op != Op::ORI) return std::nullopt;
        const std::optional<Insn> high = writer(last->addr, last->rs());
        if (!high || high->op != Op::LUI || last->rs() == 0) return std::nullopt;
        const uint32_t hi = high->uimm() << 16;
        return last->op == Op::ADDIU ? hi + static_cast<uint32_t>(last->simm()) : (hi | last->uimm());
    }

    // ---- blocks -----------------------------------------------------------------
    // A block can begin: at a function's start; at the target of a branch or
    // jump; after a branch's or jump's delay slot (where a branch not taken
    // continues, and where a call returns); after a system instruction; at a
    // 4 KB page boundary; after a read of an EE timer register; at any code
    // address found in data; and wherever an earlier run had to start
    // interpreting. A function and its variants have the same leaders: those
    // of any of them.
    void findLeaders() {
        std::vector<uint32_t> requests;
        const auto scan = [&](const Function& f, int variant) {
            // After an instruction that never continues to the next one (a
            // jump, a return) comes code that only a jump computed at run time
            // can reach, or padding. Padding is skipped; the first other word
            // of the function, if any, is taken for the start of a block.
            const auto afterTransfer = [&](uint32_t from) {
                while (from < f.end && prog_.word(f, variant, from) == 0) from += 4;
                if (from < f.end) requests.push_back(from);
            };
            for (uint32_t a = f.start; a < f.end; a += 4) {
                const Insn in = prog_.insn(f, variant, a);
                if (!in.valid()) continue;
                if (in.hasDelaySlot()) {
                    if (in.endsBlock()) afterTransfer(a + 8);
                    else requests.push_back(a + 8);
                    if (in.is(F_BRANCH | F_JUMP)) requests.push_back(in.staticTarget());
                } else if (isSystem(in)) {
                    if (in.endsBlock()) afterTransfer(a + 4);
                    else requests.push_back(a + 4);
                }
            }
        };
        for (const Function& f : prog_.functions) {
            scan(f, -1);
            for (size_t v = 0; v < f.variants.size(); ++v) scan(f, static_cast<int>(v));
        }

        // PCSX2's recompiler, whose blocks the runtime reproduces
        // (runtime/rec_blocks.h), never lets a block cross a 4 KB page
        // boundary, and ends one after a read of an EE timer register when it
        // knows the address. The next block begins there, so those places are
        // leaders too. What it knows is followed from the earliest place its
        // block can have begun: after the last instruction that always ends
        // one. A block that began later knows less, never more.
        for (const Function& f : prog_.functions) {
            for (uint32_t a = (f.start + 0x1000u) & ~0xFFFu; a < f.end; a += 0x1000u) requests.push_back(a);
        }
        {
            RecConsts consts;
            uint32_t previous = ~0u;
            for (const Function& f : prog_.functions) {
                // Code that does not follow on from the function before it.
                if (f.start != previous) consts.forget();
                previous = f.end;
                for (uint32_t a = f.start; a < f.end; a += 4) {
                    if ((a & 0xFFFu) == 0) consts.forget();
                    const Insn in = prog_.insn(a);
                    if (!in.valid() || in.hasDelaySlot() || isSystem(in)) {
                        // Its delay slot, if it has one, is the block's last instruction.
                        if (in.valid() && in.hasDelaySlot()) a += 4;
                        consts.forget();
                        continue;
                    }
                    uint32_t address = 0;
                    if (isNarrowLoad(in) && consts.address(in, &address) && isTimerAddress(address)) requests.push_back(a + 4);
                    consts.step(in);
                }
            }
        }

        requests.insert(requests.end(), labelRefs_.begin(), labelRefs_.end());
        requests.insert(requests.end(), absorbed_.begin(), absorbed_.end());

        for (uint32_t a : options_.capture.entries) {
            if (prog_.functionContaining(a) && !(a & 3)) requests.push_back(a);
            else prog_.notes.push_back({a, "captured entry " + hex32(a) + " is not inside the executable's code; ignored"});
        }

        std::sort(requests.begin(), requests.end());
        requests.erase(std::unique(requests.begin(), requests.end()), requests.end());

        for (auto& f : prog_.functions) f.leaders.push_back(f.start);
        for (uint32_t a : requests) {
            if (a & 3) continue;
            auto it = std::upper_bound(prog_.functions.begin(), prog_.functions.end(), a,
                                       [](uint32_t addr, const Function& f) { return addr < f.start; });
            if (it == prog_.functions.begin()) continue;
            --it;
            if (a < it->end && a != it->start) it->leaders.push_back(a);
        }
    }
};

} // namespace

Program analyze(const ElfFile& elf, const Options& options) {
    Program p;
    p.elf = &elf;
    Analyzer(elf, options, p).run();
    return p;
}

bool inCodeRanges(const ElfFile& elf, uint32_t addr) {
    for (const AddrRange& r : elf.codeRanges())
        if (r.contains(addr)) return true;
    return false;
}

std::vector<OutsideImage> codeOutside(const ElfFile& elf, const Capture& capture, std::vector<Note>& notes) {
    using Words = std::map<uint32_t, uint32_t>;
    const auto addressOf = [](const Capture::Code& c, size_t i) { return c.start + 4 * static_cast<uint32_t>(i); };
    // Whether every word of `c` is the one `words` holds at its address, where
    // it holds one.
    const auto agrees = [&](const Words& words, const Capture::Code& c) {
        for (size_t i = 0; i < c.words.size(); ++i) {
            const auto it = words.find(addressOf(c, i));
            if (it != words.end() && it->second != c.words[i]) return false;
        }
        return true;
    };
    const auto add = [&](Words& words, const Capture::Code& c) {
        for (size_t i = 0; i < c.words.size(); ++i) words.try_emplace(addressOf(c, i), c.words[i]);
    };

    // The first image: what it has always been, a word at every address any
    // [[code]] piece holds, the first piece's where two disagree. An
    // executable the program loaded itself has its own image.
    Words first;
    std::vector<uint32_t> firstStarts;
    std::set<uint32_t> disputed;
    size_t disagreeing = 0;
    for (const Capture::Code& c : capture.code) {
        if (c.program != 0 || inCodeRanges(elf, c.start)) continue;
        firstStarts.push_back(c.start);
        bool differs = false;
        for (size_t i = 0; i < c.words.size(); ++i) {
            const auto [it, inserted] = first.try_emplace(addressOf(c, i), c.words[i]);
            if (!inserted && it->second != c.words[i]) {
                differs = true;
                disputed.insert(it->first);
            }
        }
        if (differs) ++disagreeing;
    }
    if (disagreeing) {
        notes.push_back({0, strprintf("%zu pieces of code captured outside the executable disagree with others at %zu addresses: the first "
                                      "word captured at each is kept, and what runs there is captured as blocks from now on",
                                      disagreeing, disputed.size())});
    }

    // The blocks, as the interpreter ran them: into the first image where
    // they agree with it, into the first of the others they agree with where
    // they do not (other versions of code at the same addresses). A block's
    // words are what the runtime compares with memory before compiled code
    // runs it; its end is followed from too, so that a block of compiled code
    // begins there, and the words the runtime compares for it never run on
    // into another block's of another version.
    std::vector<Words> others;
    std::vector<std::vector<uint32_t>> otherStarts;
    std::vector<uint32_t> firstEnds;
    std::vector<std::vector<uint32_t>> otherEnds;
    for (const Capture::Code& c : capture.blocks) {
        if (inCodeRanges(elf, c.start)) {
            notes.push_back({c.start, "a block captured at " + hex32(c.start) + " is inside the executable's code; ignored"});
            continue;
        }
        const uint32_t end = addressOf(c, c.words.size());
        if (agrees(first, c)) {
            add(first, c);
            firstStarts.push_back(c.start);
            firstEnds.push_back(end);
            continue;
        }
        size_t k = 0;
        while (k < others.size() && !agrees(others[k], c)) ++k;
        if (k == others.size()) {
            others.emplace_back();
            otherStarts.emplace_back();
            otherEnds.emplace_back();
        }
        add(others[k], c);
        otherStarts[k].push_back(c.start);
        otherEnds[k].push_back(end);
    }
    // Where an image holds the word at a block's end.
    const auto endsHeld = [](const Words& words, std::vector<uint32_t>& starts, const std::vector<uint32_t>& ends) {
        for (uint32_t end : ends)
            if (words.count(end)) starts.push_back(end);
    };
    endsHeld(first, firstStarts, firstEnds);
    for (size_t k = 0; k < others.size(); ++k) endsHeld(others[k], otherStarts[k], otherEnds[k]);

    const auto image = [&](const Words& words, const std::string& name) {
        std::vector<ElfFile::Piece> pieces;
        for (const auto& [at, w] : words) {
            if (pieces.empty() || pieces.back().address + pieces.back().bytes.size() != at) pieces.push_back({hex32(at), at, {}});
            for (int i = 0; i < 4; ++i) pieces.back().bytes.push_back(static_cast<uint8_t>(w >> (8 * i)));
        }
        return ElfFile::fromPieces(name, 0, pieces);
    };
    const auto sorted = [](std::vector<uint32_t> list) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
        return list;
    };
    std::vector<OutsideImage> images;
    images.reserve(1 + others.size());
    const std::string name = elf.name() + ", the code it ran outside its executable";
    images.push_back({image(first, name), sorted(firstStarts)});
    for (size_t k = 0; k < others.size(); ++k)
        images.push_back({image(others[k], name + strprintf(", other versions (%zu)", k + 2)), sorted(otherStarts[k])});
    return images;
}

Capture parseCapture(const std::string& text, const std::string& fileName) {
    Capture capture;
    enum class List { None, Entries, Words } list = List::None;
    // What the lines belong to: the file itself, a [[code]] block, a [[block]]
    // block (a block run outside the executable), or a [[program]] block (an
    // executable the program loaded itself).
    enum class Section { Top, Code, Block, Program } section = Section::Top;
    bool haveStart = false;
    Capture::Code* current = nullptr; // the [[code]] or [[block]] being read
    std::vector<uint32_t>* entries = &capture.entries; // where an entries list goes; none in a [[program]] before its crc

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    const auto fail = [&](const std::string& what) -> Error { return Error(fileName + ":" + std::to_string(lineNo) + ": " + what); };
    const auto number = [&](std::string_view token) -> uint32_t {
        const auto v = parseInt(token);
        if (!v || *v < 0 || *v > 0xFFFFFFFFLL) throw fail("'" + std::string(token) + "' is not a 32-bit number");
        return static_cast<uint32_t>(*v);
    };
    const auto endSection = [&]() {
        if (section == Section::Code || section == Section::Block) {
            const char* name = section == Section::Code ? "[[code]]" : "[[block]]";
            if (!haveStart || current->words.empty()) throw fail(std::string("a ") + name + " block needs a start and words");
            if (current->start & 3) throw fail(std::string("a ") + name + " block starts at an address that is not a multiple of 4");
        }
        if (section == Section::Program && !entries) throw fail("a [[program]] block needs a crc");
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::string_view rest = trim(line);
        if (rest.empty()) continue;

        if (list == List::None) {
            if (rest == "[[code]]" || rest == "[[block]]") {
                endSection();
                const bool block = rest == "[[block]]";
                std::vector<Capture::Code>& list = block ? capture.blocks : capture.code;
                list.emplace_back();
                current = &list.back();
                section = block ? Section::Block : Section::Code;
                haveStart = false;
                continue;
            }
            if (rest == "[[program]]") {
                endSection();
                section = Section::Program;
                entries = nullptr;
                continue;
            }
            const size_t eq = rest.find('=');
            if (eq == std::string_view::npos) throw fail("expected 'name = value', [[code]] or [[program]]");
            const std::string_view key = trim(rest.substr(0, eq));
            rest = trim(rest.substr(eq + 1));
            if (key == "start") {
                if (section != Section::Code && section != Section::Block) throw fail("'start' outside a [[code]] or [[block]] block");
                current->start = number(rest);
                haveStart = true;
                continue;
            }
            if (key == "program") {
                if (section != Section::Code) throw fail("'program' outside a [[code]] block");
                current->program = number(rest);
                continue;
            }
            if (key == "crc") {
                if (section != Section::Program) throw fail("'crc' outside a [[program]] block");
                entries = &capture.programEntries[number(rest)];
                continue;
            }
            if (key == "entries" && (section == Section::Top || section == Section::Program)) {
                if (!entries) throw fail("a [[program]] block's crc comes before its entries");
                list = List::Entries;
            }
            else if (key == "words" && (section == Section::Code || section == Section::Block)) list = List::Words;
            else throw fail("unexpected '" + std::string(key) + "'");
            if (rest.empty() || rest[0] != '[') throw fail("expected '[' after '" + std::string(key) + " ='");
            rest.remove_prefix(1);
        }

        // Inside a list: numbers, commas, and the closing bracket.
        while (!rest.empty()) {
            const size_t cut = rest.find_first_of(", \t]");
            const std::string_view token = rest.substr(0, cut);
            if (!token.empty()) {
                const uint32_t v = number(token);
                if (list == List::Entries) entries->push_back(v);
                else current->words.push_back(v);
            }
            if (cut == std::string_view::npos) break;
            if (rest[cut] == ']') {
                list = List::None;
                if (!trim(rest.substr(cut + 1)).empty()) throw fail("text after ']'");
                break;
            }
            rest.remove_prefix(cut + 1);
        }
    }
    if (list != List::None) throw fail("a list is not closed");
    endSection();
    return capture;
}

Capture loadCaptureFile(const std::filesystem::path& file) { return parseCapture(readTextFile(file), file.string()); }

void Capture::normalize() {
    const auto sortUnique = [](std::vector<uint32_t>& list) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    };
    sortUnique(entries);
    for (auto& [crc, list] : programEntries) sortUnique(list);

    const auto before = [](const Code& a, const Code& b) {
        if (a.program != b.program) return a.program < b.program;
        return a.start != b.start ? a.start < b.start : a.words < b.words;
    };
    const auto same = [](const Code& a, const Code& b) { return a.program == b.program && a.start == b.start && a.words == b.words; };
    std::sort(code.begin(), code.end(), before);
    code.erase(std::unique(code.begin(), code.end(), same), code.end());
    std::sort(blocks.begin(), blocks.end(), before);
    blocks.erase(std::unique(blocks.begin(), blocks.end(), same), blocks.end());
}

std::string writeCapture(const Capture& capture, const std::string& comment) {
    const auto list = [](std::string& out, const std::vector<uint32_t>& values) {
        out += "entries = [\n";
        for (uint32_t a : values) out += strprintf("  0x%08x,\n", a);
        out += "]\n";
    };
    std::string out;
    if (!comment.empty()) out += "# " + comment + "\n";
    out += "# Give this file to the recompiler (ps2n-recomp compile --capture) to have it compiled.\n\n";
    out += "# Addresses at which a block was interpreted.\n";
    list(out, capture.entries);
    for (const auto& [crc, values] : capture.programEntries) {
        out += strprintf("\n# The same in an executable the program loaded itself (CRC %08X).\n[[program]]\ncrc = 0x%08x\n", crc, crc);
        list(out, values);
    }
    for (const Capture::Code& c : capture.code) {
        out += strprintf("\n# Code found in memory in the place of the function at 0x%08x.\n[[code]]\n", c.start);
        if (c.program) out += strprintf("program = 0x%08x\n", c.program);
        out += strprintf("start = 0x%08x\nwords = [", c.start);
        for (size_t i = 0; i < c.words.size(); ++i) out += strprintf("%s0x%08x,", (i % 8) ? " " : "\n  ", c.words[i]);
        out += "\n]\n";
    }
    for (const Capture::Code& c : capture.blocks) {
        out += strprintf("\n# A block run at 0x%08x outside the executable, where no compiled code ran it.\n[[block]]\n", c.start);
        out += strprintf("start = 0x%08x\nwords = [", c.start);
        for (size_t i = 0; i < c.words.size(); ++i) out += strprintf("%s0x%08x,", (i % 8) ? " " : "\n  ", c.words[i]);
        out += "\n]\n";
    }
    return out;
}

} // namespace ps2n::recomp
