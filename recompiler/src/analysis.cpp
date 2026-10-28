#include "analysis.h"

#include "function_map.h"

#include "ps2uc/util.h"

#include <algorithm>
#include <cctype>
#include <optional>

namespace ps2uc {

using namespace r5900;

const char* sourceName(Source s) {
    switch (s) {
    case Source::Config: return "config";
    case Source::Map: return "function map";
    case Source::Symbol: return "ELF symbol";
    case Source::Entry: return "entry point";
    case Source::ExtraEntry: return "extra entry";
    case Source::Jal: return "JAL target";
    case Source::DataRef: return "data pointer";
    case Source::CodePointer: return "code pointer (lui/addiu)";
    case Source::Prologue: return "prologue heuristic";
    case Source::Gap: return "unclaimed code";
    }
    return "?";
}

const std::vector<std::string>& runtimeStubNames() {
    static const std::vector<std::string> names = {
#define PS2_STUB(name, summary) #name,
#include "ps2/stub_list.inc"
#undef PS2_STUB
    };
    return names;
}

bool isRuntimeStub(const std::string& name) {
    const auto& n = runtimeStubNames();
    return std::find(n.begin(), n.end(), name) != n.end();
}

uint32_t Program::word(uint32_t addr) const {
    if (auto it = patched.find(addr); it != patched.end()) return it->second;
    uint32_t w = 0;
    elf->read32(addr, w);
    return w;
}

const Function* Program::functionAt(uint32_t start) const {
    auto it = std::lower_bound(functions.begin(), functions.end(), start, [](const Function& f, uint32_t a) { return f.start < a; });
    return it != functions.end() && it->start == start ? &*it : nullptr;
}

Function* Program::functionContaining(uint32_t addr) {
    auto it = std::upper_bound(functions.begin(), functions.end(), addr, [](uint32_t a, const Function& f) { return a < f.start; });
    if (it == functions.begin()) return nullptr;
    --it;
    return addr < it->end ? &*it : nullptr;
}

const Function* Program::functionContaining(uint32_t addr) const {
    return const_cast<Program*>(this)->functionContaining(addr);
}

std::string Program::describe(uint32_t addr) const {
    if (const Function* f = functionContaining(addr)) {
        if (addr == f->start) return f->name;
        return f->name + strprintf("+0x%x", addr - f->start);
    }
    return hex32(addr);
}

namespace {

constexpr uint32_t kJrRa = 0x03E00008u;

bool strongBoundary(Source s) { return s == Source::Config || s == Source::Map || s == Source::Symbol || s == Source::Entry; }

std::string sanitize(const std::string& name) {
    std::string out;
    for (char c : name) out.push_back(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_');
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "f_" + out;
    if (out.size() > 96) out.resize(96);
    return out;
}

struct Candidate {
    uint32_t start = 0;
    std::optional<uint32_t> end;
    std::string name;
    Source source = Source::Gap;
};

class Analyzer {
public:
    Analyzer(const ElfFile& elf, const Config& cfg, Program& prog) : elf_(elf), cfg_(cfg), prog_(prog) {}

    void run() {
        ranges_ = elf_.codeRanges();
        if (ranges_.empty()) throw Error(elf_.name() + ": no executable code found (no SHF_EXECINSTR sections or PF_X segments)");
        applyPatches();
        collectSymbols();
        collectConfigAndMaps();
        if (cfg_.scanJalTargets) scanJalTargets();
        if (cfg_.scanDataPointers) {
            scanDataPointers();
            scanCodePointers();
        }
        if (cfg_.scanPrologues) scanPrologues();
        classifyPointers();
        buildFunctions();
        bindFunctions();
        for (auto& f : prog_.functions)
            if (f.binding == Binding::Recompile) analyzeFunction(f);
        applyCrossEntries();
        for (const auto& f : prog_.functions) {
            prog_.sourceCounts[f.source]++;
            prog_.instructionCount += f.binding == Binding::Recompile ? (f.end - f.start) / 4 : 0;
        }
    }

private:
    const ElfFile& elf_;
    const Config& cfg_;
    Program& prog_;
    std::vector<AddrRange> ranges_;
    std::map<uint32_t, Candidate> candidates_;
    std::set<uint32_t> pointers_;            // code addresses referenced from data / lui-addiu pairs
    std::set<uint32_t> codePointerOnly_;     // subset found only via lui/addiu
    std::set<uint32_t> labelRefs_;           // pointers that are not function starts
    std::set<uint32_t> absorbed_;            // weak starts swallowed by a sized function
    std::set<uint32_t> crossEntries_;        // branch/call targets inside other functions
    std::map<uint32_t, std::vector<std::string>> aliases_;   // address -> every symbol/map name there

    const AddrRange* rangeOf(uint32_t a) const {
        for (const auto& r : ranges_)
            if (r.contains(a)) return &r;
        return nullptr;
    }
    bool inCode(uint32_t a) const { return (a & 3) == 0 && rangeOf(a) != nullptr; }
    uint32_t word(uint32_t a) const { return prog_.word(a); }

    void warn(uint32_t addr, std::string msg) { prog_.warnings.push_back({addr, std::move(msg)}); }

    void addCandidate(uint32_t start, std::optional<uint32_t> end, const std::string& name, Source src) {
        if (!inCode(start)) {
            if (src == Source::Config || src == Source::Map || src == Source::ExtraEntry)
                warn(start, std::string(sourceName(src)) + " entry " + hex32(start) + " is not inside executable code; ignored");
            return;
        }
        if (!name.empty()) aliases_[start].push_back(name);
        auto [it, inserted] = candidates_.try_emplace(start, Candidate{start, end, name, src});
        if (inserted) return;
        Candidate& c = it->second;
        if (src < c.source) {   // stronger source wins the boundary and the name
            if (!name.empty()) c.name = name;
            if (end) c.end = end;
            c.source = src;
        } else {
            if (c.name.empty()) c.name = name;
            if (!c.end && end) c.end = end;
        }
    }

    // ---- inputs -------------------------------------------------------------
    void applyPatches() {
        for (const Patch& p : cfg_.patches) {
            uint32_t orig = 0;
            if (!elf_.read32(p.address, orig)) throw Error("patch at " + hex32(p.address) + " is outside the loaded image");
            if (p.expect && *p.expect != orig)
                throw Error(strprintf("patch at 0x%08x expects 0x%08x but the ELF has 0x%08x: this config was written for a different build",
                                      p.address, *p.expect, orig));
            prog_.patched[p.address] = p.value;
        }
    }

    void collectSymbols() {
        for (const ElfSymbol& s : elf_.symbols) {
            if (s.name.empty() || !inCode(s.value)) continue;
            if (s.type == elf::STT_FUNC) {
                if (cfg_.useSymbols) addCandidate(s.value, s.size ? std::optional<uint32_t>(s.value + s.size) : std::nullopt, s.name, Source::Symbol);
                else aliases_[s.value].push_back(s.name);
            } else if (s.type == elf::STT_NOTYPE) {
                // Global labels in assembly code are entry points; local ones are just labels.
                if (cfg_.useSymbols && s.bind != elf::STB_LOCAL) addCandidate(s.value, std::nullopt, s.name, Source::Symbol);
                else prog_.labelNames.emplace(s.value, s.name);
            }
        }
        if (!candidates_.count(elf_.entry)) addCandidate(elf_.entry, std::nullopt, "_start", Source::Entry);
    }

    void collectConfigAndMaps() {
        for (const ExplicitFunction& f : cfg_.functions) addCandidate(f.start, f.end, f.name, Source::Config);
        for (const auto& path : cfg_.functionMaps) {
            for (const MapEntry& e : loadFunctionMap(path)) addCandidate(e.start, e.end, e.name, Source::Map);
        }
        for (uint32_t a : cfg_.entries) addCandidate(a, std::nullopt, "", Source::ExtraEntry);
        for (const auto& path : cfg_.entriesFiles) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                warn(0, "entries file " + path.string() + " does not exist (yet); ignored");
                continue;
            }
            for (uint32_t a : loadEntriesFile(path)) addCandidate(a, std::nullopt, "", Source::ExtraEntry);
        }
        // Address-based bindings name function starts explicitly.
        auto addRef = [&](const FunctionRef& r) {
            if (r.address) addCandidate(*r.address, std::nullopt, "", Source::Config);
        };
        for (const auto& [ref, h] : cfg_.stubBindings) addRef(ref);
        for (const auto& ref : cfg_.skip) addRef(ref);
        for (const auto& [ref, h] : cfg_.overrides) addRef(ref);
    }

    // ---- scans --------------------------------------------------------------
    void scanJalTargets() {
        for (const auto& r : ranges_) {
            for (uint32_t a = r.start; a + 4 <= r.end; a += 4) {
                const uint32_t w = word(a);
                if ((w >> 26) != 0x03) continue;   // JAL
                const Insn in(w, a);
                if (inCode(in.jumpTarget())) addCandidate(in.jumpTarget(), std::nullopt, "", Source::Jal);
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

    // lui rX, hi ; addiu/ori rY, rX, lo  ->  address constants in code (callbacks, thread entries).
    void scanCodePointers() {
        for (const auto& r : ranges_) {
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

    void scanPrologues() {
        for (const auto& r : ranges_) {
            for (uint32_t a = r.start; a + 12 <= r.end; a += 4) {
                if (word(a) != kJrRa) continue;
                uint32_t b = a + 8;
                while (b + 4 <= r.end && word(b) == 0) b += 4;
                if (b + 4 > r.end) continue;
                const Insn in(word(b), b);
                if ((in.op == Op::ADDIU || in.op == Op::DADDIU) && in.rt() == regsSp() && in.rs() == regsSp() && in.simm() < 0)
                    addCandidate(b, std::nullopt, "", Source::Prologue);
            }
        }
    }
    static uint32_t regsSp() { return 29; }

    bool looksLikeFunctionStart(uint32_t a) const {
        const AddrRange* r = rangeOf(a);
        if (!r) return false;
        if (a == r->start) return true;
        uint32_t p = a;
        for (int i = 0; i < 8 && p >= r->start + 8 && word(p - 4) == 0 && word(p - 8) != kJrRa; ++i) p -= 4;   // skip padding
        if (p >= r->start + 8 && word(p - 8) == kJrRa) return true;
        const Insn in(word(a), a);
        return (in.op == Op::ADDIU || in.op == Op::DADDIU) && in.rt() == 29 && in.rs() == 29 && in.simm() < 0;
    }

    void classifyPointers() {
        for (uint32_t p : pointers_) {
            if (candidates_.count(p)) continue;
            if (looksLikeFunctionStart(p)) addCandidate(p, std::nullopt, "", codePointerOnly_.count(p) ? Source::CodePointer : Source::DataRef);
            else labelRefs_.insert(p);
        }
    }

    // ---- functions -------------------------------------------------------------
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

        // Code between a sized function and the next start is either padding or
        // a function nobody references directly; keep the latter.
        if (cfg_.splitGaps) {
            std::vector<Function> gaps;
            for (size_t i = 0; i < fns.size(); ++i) {
                const AddrRange* r = rangeOf(fns[i].start);
                const uint32_t limit = (i + 1 < fns.size() && fns[i + 1].start < r->end) ? fns[i + 1].start : r->end;
                uint32_t a = fns[i].end;
                while (a < limit && word(a) == 0) a += 4;
                if (a < limit) {
                    Function g;
                    g.start = a;
                    g.end = limit;
                    g.source = Source::Gap;
                    gaps.push_back(std::move(g));
                }
            }
            // Code before the first function of each range.
            for (const auto& r : ranges_) {
                auto first = std::find_if(fns.begin(), fns.end(), [&](const Function& f) { return r.contains(f.start); });
                const uint32_t limit = first != fns.end() ? first->start : r.end;
                uint32_t a = r.start;
                while (a < limit && word(a) == 0) a += 4;
                if (a < limit) {
                    Function g;
                    g.start = a;
                    g.end = limit;
                    g.source = Source::Gap;
                    gaps.push_back(std::move(g));
                }
            }
            for (auto& g : gaps) fns.push_back(std::move(g));
            std::sort(fns.begin(), fns.end(), [](const Function& a, const Function& b) { return a.start < b.start; });
        }

        for (auto& f : fns) {
            if (f.name.empty()) f.name = strprintf("sub_%08x", f.start);
            f.cname = sanitize(f.name) + strprintf("_%08x", f.start);
        }
        prog_.functions = std::move(fns);
    }

    Function* resolve(const FunctionRef& r, const char* what) {
        if (r.address) {
            Function* f = const_cast<Function*>(prog_.functionAt(*r.address));
            if (!f) warn(*r.address, std::string(what) + " target " + r.text + " is not a function start");
            return f;
        }
        for (auto& f : prog_.functions) {
            if (f.name == r.text) return &f;
            if (auto it = aliases_.find(f.start); it != aliases_.end())
                for (const auto& n : it->second)
                    if (n == r.text) return &f;
        }
        warn(0, std::string(what) + " target '" + r.text + "' not found (no symbol or map entry with that name)");
        return nullptr;
    }

    void bindFunctions() {
        if (cfg_.autoStubs) {
            static const std::set<std::string> generic = {"ret0", "ret1", "retm1", "nop"};
            for (auto& f : prog_.functions) {
                if (generic.count(f.name) || !isRuntimeStub(f.name)) continue;
                if (std::find(cfg_.stubExclude.begin(), cfg_.stubExclude.end(), f.name) != cfg_.stubExclude.end()) continue;
                f.binding = Binding::Stub;
                f.handler = f.name;
            }
        }
        for (const auto& [ref, handler] : cfg_.stubBindings) {
            if (!isRuntimeStub(handler))
                throw Error("stub handler '" + handler + "' (for " + ref.text + ") does not exist in the runtime; "
                            "`ps2-uncompiler stubs` lists the available ones");
            if (Function* f = resolve(ref, "stub")) {
                f->binding = Binding::Stub;
                f->handler = handler;
            }
        }
        for (const auto& ref : cfg_.skip)
            if (Function* f = resolve(ref, "skip")) f->binding = Binding::Skip;
        for (const auto& [ref, sym] : cfg_.overrides) {
            if (sanitize(sym) != sym) throw Error("override symbol '" + sym + "' is not a valid C++ identifier");
            if (Function* f = resolve(ref, "override")) {
                f->binding = Binding::Override;
                f->handler = sym;
            }
        }
    }

    // ---- per-function analysis --------------------------------------------------
    int writerBefore(const Function& f, int from, uint32_t reg, int maxBack) const {
        for (int k = from - 1; k >= 0 && k >= from - maxBack; --k)
            if (f.insns[static_cast<size_t>(k)].writesGpr(reg)) return k;
        return -1;
    }

    // Constant value of `reg` produced by lui (+addiu/ori/daddiu) right before
    // index `at`, possibly by way of a stack slot.
    std::optional<uint32_t> constantOf(const Function& f, int at, uint32_t reg, int depth = 0) const {
        if (depth > 4) return std::nullopt;
        const int w = writerBefore(f, at, reg, 12);
        if (w < 0) return std::nullopt;
        const Insn& in = f.insns[static_cast<size_t>(w)];
        if (in.op == Op::LUI) return in.uimm() << 16;
        if ((in.op == Op::ADDIU || in.op == Op::DADDIU) && in.rs() == reg) {
            if (auto base = constantOf(f, w, reg, depth + 1)) return *base + static_cast<uint32_t>(in.simm());
        }
        if ((in.op == Op::ADDIU || in.op == Op::DADDIU) && in.rs() != reg) {
            if (auto base = constantOf(f, w, in.rs(), depth + 1)) return *base + static_cast<uint32_t>(in.simm());
        }
        if (in.op == Op::ORI) {
            if (auto base = constantOf(f, w, in.rs(), depth + 1)) return *base | in.uimm();
        }
        if ((in.op == Op::LW || in.op == Op::LD) && (in.rs() == 29 || in.rs() == 30))
            return spilledConstant(f, in.rs(), in.simm(), depth + 1);
        return std::nullopt;
    }

    // GCC spills switch-table addresses to the stack under register pressure.
    // A slot of $sp or $fp is private to the function, so if every store to it
    // writes the same constant, a load from it yields that constant.
    std::optional<uint32_t> spilledConstant(const Function& f, uint32_t baseReg, int32_t offset, int depth) const {
        std::optional<uint32_t> value;
        for (size_t k = 0; k < f.insns.size(); ++k) {
            const Insn& st = f.insns[k];
            if ((st.op != Op::SW && st.op != Op::SD) || st.rs() != baseReg || st.simm() != offset) continue;
            const auto v = constantOf(f, static_cast<int>(k), st.rt(), depth);
            if (!v || (value && *value != *v)) return std::nullopt;
            value = v;
        }
        return value;
    }

    // GCC-style switch tables:  [sltiu t,i,N ...]  lui b,%hi(T)  addu b,b,i4  lw x,%lo(T)(b)  jr x
    std::optional<std::vector<uint32_t>> jumpTable(const Function& f, int jrIdx) const {
        const Insn& jr = f.insns[static_cast<size_t>(jrIdx)];
        const int lwIdx = writerBefore(f, jrIdx, jr.rs(), 8);
        if (lwIdx < 0 || f.insns[static_cast<size_t>(lwIdx)].op != Op::LW) return std::nullopt;
        const Insn& lw = f.insns[static_cast<size_t>(lwIdx)];
        if (constantOf(f, lwIdx, lw.rs())) return std::nullopt;   // a fixed address, not an indexed table
        const int addIdx = writerBefore(f, lwIdx, lw.rs(), 8);
        if (addIdx < 0) return std::nullopt;
        const Insn& add = f.insns[static_cast<size_t>(addIdx)];
        if (add.op != Op::ADDU && add.op != Op::DADDU) return std::nullopt;
        std::optional<uint32_t> base = constantOf(f, addIdx, add.rs());
        uint32_t indexReg = add.rt();
        if (!base) {
            base = constantOf(f, addIdx, add.rt());
            indexReg = add.rs();
        }
        if (!base) return std::nullopt;
        const uint32_t table = *base + static_cast<uint32_t>(lw.simm());

        // Bound from "sltiu t, i, N" on the unscaled index, if we can see it.
        std::optional<uint32_t> count;
        for (int k = addIdx - 1; k >= 0 && k >= addIdx - 24; --k) {
            const Insn& in = f.insns[static_cast<size_t>(k)];
            if (in.op == Op::SLTIU && in.simm() > 0 && in.simm() <= 4096) {
                count = static_cast<uint32_t>(in.simm());
                break;
            }
        }
        (void)indexReg;
        std::vector<uint32_t> targets;
        const uint32_t maxEntries = count.value_or(1024);
        for (uint32_t i = 0; i < maxEntries; ++i) {
            uint32_t t = 0;
            if (!elf_.read32(table + 4 * i, t)) break;
            const bool local = (t & 3) == 0 && t >= f.start && t < f.end;
            if (!local) {
                if (count) return std::nullopt;   // a bounded table must be entirely local
                break;
            }
            targets.push_back(t);
        }
        if (targets.empty()) return std::nullopt;
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        return targets;
    }

    // Resume points are labels inside the function. A call in the last slot
    // returns to the next function's start, which the dispatcher already knows.
    void addResume(Function& f, uint32_t a) {
        if (a < f.start || a >= f.end) return;
        f.resume.insert(a);
        f.labels.insert(a);
    }

    void crossEntry(const Function& f, uint32_t target) {
        if (target == f.start) return;
        if (const Function* g = prog_.functionAt(target); g) return;   // a function start: no label needed
        crossEntries_.insert(target);
    }

    void analyzeFunction(Function& f) {
        const uint32_t n = (f.end - f.start) / 4;
        f.insns.reserve(n + 1);
        for (uint32_t i = 0; i < n; ++i) f.insns.emplace_back(word(f.start + 4 * i), f.start + 4 * i);
        if (!f.insns.empty() && f.insns.back().hasDelaySlot()) f.insns.emplace_back(word(f.end), f.end);

        const auto inside = [&](uint32_t a) { return a >= f.start && a < f.end; };
        for (size_t k = 0; k < f.insns.size(); ++k) {
            const Insn& in = f.insns[k];
            const uint32_t a = in.addr;
            if (a >= f.end) break;   // trailing delay slot belongs to the last branch
            if (!in.valid()) {
                if (in.raw != 0) prog_.unknownInstructions.push_back({a, strprintf("unknown instruction word 0x%08x in %s", in.raw, f.name.c_str())});
                continue;
            }
            if (in.hasDelaySlot() && k + 1 < f.insns.size()) {
                const Insn& ds = f.insns[k + 1];
                if (ds.isControlFlow() || ds.op == Op::SYSCALL)
                    warn(ds.addr, "control-flow instruction in the delay slot at " + hex32(ds.addr) + " (" + f.name + ") is not executed");
            }
            const uint32_t ret = a + 8;
            if (in.op == Op::SYSCALL) {
                addResume(f, a + 4);
                continue;
            }
            if (in.op == Op::JAL) {
                addResume(f, ret);
                crossEntry(f, in.jumpTarget());
                continue;
            }
            if (in.op == Op::JALR) {
                addResume(f, ret);
                continue;
            }
            if (in.op == Op::J) {
                const uint32_t t = in.jumpTarget();
                if (inside(t)) {
                    f.labels.insert(t);
                    if (t <= a && cfg_.preemptLoops) addResume(f, t);
                } else {
                    crossEntry(f, t);
                }
                continue;
            }
            if (in.op == Op::JR) {
                if (in.rs() == 31) continue;
                std::optional<std::vector<uint32_t>> table;
                if (cfg_.jumpTables) table = jumpTable(f, static_cast<int>(k));
                if (table) {
                    for (uint32_t t : *table) f.labels.insert(t);
                    f.jumpTables[a] = *table;
                    prog_.resolvedJumpTables++;
                } else {
                    f.unresolvedJumps.push_back(a);
                }
                continue;
            }
            if (in.isBranch()) {
                const uint32_t t = in.branchTarget();
                if (in.isCall()) {   // BLTZAL / BGEZAL / BAL
                    addResume(f, ret);
                    if (inside(t)) addResume(f, t);   // a local subroutine call: entered through the dispatcher on return
                    else crossEntry(f, t);
                } else if (inside(t)) {
                    f.labels.insert(t);
                    if (t <= a && cfg_.preemptLoops) addResume(f, t);
                } else {
                    crossEntry(f, t);
                }
            }
        }
        // Code addresses held in data (jump tables we could not parse, callback
        // tables pointing mid-function): make them reachable.
        for (auto it = labelRefs_.lower_bound(f.start + 4); it != labelRefs_.end() && *it < f.end; ++it) {
            addResume(f, *it);
            f.switchCandidates.insert(*it);
        }
        for (auto it = absorbed_.lower_bound(f.start + 4); it != absorbed_.end() && *it < f.end; ++it) addResume(f, *it);
        for (const auto& [jr, targets] : f.jumpTables)
            for (uint32_t t : targets) f.switchCandidates.insert(t);
        for (uint32_t jr : f.unresolvedJumps) {
            if (Insn(word(jr), jr).rs() == 25) continue;   // jr $t9: GCC's indirect tail call, dispatched at run time
            warn(jr, "indirect jump at " + hex32(jr) + " in " + f.name +
                         " has no resolved jump table; falling back to " +
                         (f.switchCandidates.empty() ? "runtime dispatch" : "data-referenced labels, then runtime dispatch"));
        }
    }

    void applyCrossEntries() {
        for (uint32_t t : crossEntries_) {
            Function* g = prog_.functionContaining(t);
            if (g && g->binding == Binding::Recompile && t != g->start) addResume(*g, t);
        }
    }
};

} // namespace

Program analyze(const ElfFile& elf, const Config& cfg) {
    Program p;
    p.elf = &elf;
    p.config = &cfg;
    Analyzer(elf, cfg, p).run();
    return p;
}

} // namespace ps2uc
