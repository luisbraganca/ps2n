// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysis.h"

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
    code.insert(code.end(), other.code.begin(), other.code.end());
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

// How many variants the runtime can tell apart: a function's state is a byte,
// with three values taken (FunctionState in runtime/include/ps2n/recomp.h).
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
        collectSymbols();
        scanJalTargets();
        scanDataPointers();
        scanCodePointers();
        scanPrologues();
        classifyPointers();
        buildFunctions();
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

    // ---- blocks -----------------------------------------------------------------
    // A block begins: at a function's start; at the target of a branch or
    // jump; after a branch's or jump's delay slot (where a branch not taken
    // continues, and where a call returns); after a system instruction; at any
    // code address found in data; and wherever an earlier run had to start
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

Capture parseCapture(const std::string& text, const std::string& fileName) {
    Capture capture;
    enum class List { None, Entries, Words } list = List::None;
    bool inBlock = false, haveStart = false;

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    const auto fail = [&](const std::string& what) -> Error { return Error(fileName + ":" + std::to_string(lineNo) + ": " + what); };
    const auto number = [&](std::string_view token) -> uint32_t {
        const auto v = parseInt(token);
        if (!v || *v < 0 || *v > 0xFFFFFFFFLL) throw fail("'" + std::string(token) + "' is not a 32-bit number");
        return static_cast<uint32_t>(*v);
    };
    const auto endBlock = [&]() {
        if (!inBlock) return;
        if (!haveStart || capture.code.back().words.empty()) throw fail("a [[code]] block needs a start and words");
        if (capture.code.back().start & 3) throw fail("a [[code]] block starts at an address that is not a multiple of 4");
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::string_view rest = trim(line);
        if (rest.empty()) continue;

        if (list == List::None) {
            if (rest == "[[code]]") {
                endBlock();
                capture.code.emplace_back();
                inBlock = true;
                haveStart = false;
                continue;
            }
            const size_t eq = rest.find('=');
            if (eq == std::string_view::npos) throw fail("expected 'name = value' or [[code]]");
            const std::string_view key = trim(rest.substr(0, eq));
            rest = trim(rest.substr(eq + 1));
            if (key == "start") {
                if (!inBlock) throw fail("'start' outside a [[code]] block");
                capture.code.back().start = number(rest);
                haveStart = true;
                continue;
            }
            if (key == "entries") list = List::Entries;
            else if (key == "words" && inBlock) list = List::Words;
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
                if (list == List::Entries) capture.entries.push_back(v);
                else capture.code.back().words.push_back(v);
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
    endBlock();
    return capture;
}

Capture loadCaptureFile(const std::filesystem::path& file) { return parseCapture(readTextFile(file), file.string()); }

void Capture::normalize() {
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());

    const auto before = [](const Code& a, const Code& b) { return a.start != b.start ? a.start < b.start : a.words < b.words; };
    const auto same = [](const Code& a, const Code& b) { return a.start == b.start && a.words == b.words; };
    std::sort(code.begin(), code.end(), before);
    code.erase(std::unique(code.begin(), code.end(), same), code.end());
}

std::string writeCapture(const Capture& capture, const std::string& comment) {
    std::string out;
    if (!comment.empty()) out += "# " + comment + "\n";
    out += "# Give this file to the recompiler (ps2n-recomp compile --capture) to have it compiled.\n\n";
    out += "# Addresses at which a block was interpreted.\nentries = [\n";
    for (uint32_t a : capture.entries) out += strprintf("  0x%08x,\n", a);
    out += "]\n";
    for (const Capture::Code& c : capture.code) {
        out += strprintf("\n# Code found in memory in the place of the function at 0x%08x.\n[[code]]\nstart = 0x%08x\nwords = [", c.start, c.start);
        for (size_t i = 0; i < c.words.size(); ++i) out += strprintf("%s0x%08x,", (i % 8) ? " " : "\n  ", c.words[i]);
        out += "\n]\n";
    }
    return out;
}

} // namespace ps2n::recomp
