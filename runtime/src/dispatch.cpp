// Dispatcher: maps guest addresses to recompiled functions, and falls back to
// the interpreter for code the recompiler did not cover.
#include "internal.h"
#include "kernel/kernel.h"
#include "log.h"

#include "ps2uc/util.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>

namespace ps2 {

Cpu cpu;
const GameInfo* g_game = nullptr;
bool g_exitRequested = false;
int g_exitCode = 0;
uint64_t g_switchGeneration = 0;

namespace {

// One of the game's executables. Its recompiled code runs only while RAM
// holds that program. A program the runtime loaded counts as present until
// the game invalidates the instruction cache, as it must after loading code
// itself; that starts a new code epoch, after which each function is checked
// against RAM before it runs again. A program the game jumps into without
// LoadExecPS2 must be in RAM as a whole before any of its code runs.
struct Program {
    const GameInfo* info = nullptr;          // null: no recompiled code
    std::string name;                        // its ELF file name
    std::set<uint32_t>* missing = nullptr;   // its interpreted entry points
    uint32_t presentEpoch = 0;               // all its code was in RAM in this epoch
    uint32_t absentEpoch = 0;                // ...was found not to be
    std::vector<uint32_t> checkedEpoch;      // per function: found in RAM in this epoch
    bool entered = false;                    // the game jumped into it before
};

std::vector<Program> g_programs;   // the boot program, then [input] programs
Program g_uncompiled;              // the running executable when it has no recompiled code
Program* g_running = nullptr;
uint32_t g_codeEpoch = 1;

struct CacheEntry {
    uint32_t pc;
    RecompFn fn;
};
CacheEntry g_cache[8192];

// Addresses that had to be interpreted, per program (by the file they go to).
std::map<std::filesystem::path, std::set<uint32_t>> g_missing;

int g_hostCallDepth = 0;

void clearCache() {
    for (auto& e : g_cache) e = {0xFFFFFFFFu, nullptr};
}

std::string elfName(const GameInfo& info) { return std::filesystem::path(info.defaultElf).filename().string(); }

void initProgram(Program& p, const GameInfo* info, const std::string& name, bool boot) {
    p = Program{};
    p.info = info;
    p.name = name;
    // The boot program's interpreted entry points go to --missing-out, another
    // program's next to it, named after it (ps2uc_missing.MAIN.ELF.toml).
    const std::filesystem::path& out = g_opt.missingOut;
    p.missing = &g_missing[out.empty() || boot ? out : out.parent_path() / (out.stem().string() + "." + name + out.extension().string())];
    if (!info) return;
    p.checkedEpoch.assign(info->functionCount, 0);
    for (size_t i = 1; i < info->functionCount; ++i)
        if (info->functions[i - 1].start >= info->functions[i].start)
            PS2_ERROR("dispatch", "%s: function table is not sorted at %s (0x%08x)", name.c_str(), info->functions[i].name,
                      info->functions[i].start);
}

const FunctionInfo* functionIn(const Program& p, uint32_t pc) {
    if (!p.info) return nullptr;
    const FunctionInfo* begin = p.info->functions;
    const FunctionInfo* end = begin + p.info->functionCount;
    const FunctionInfo* it = std::upper_bound(begin, end, pc, [](uint32_t a, const FunctionInfo& f) { return a < f.start; });
    if (it == begin) return nullptr;
    --it;
    return pc < it->end ? it : nullptr;
}

// The function of p to run at `pc`: one that starts there or has a resume
// label there. With --interpret only host replacements (stubs, skips,
// overrides) count: they have no guest code to interpret, and the config
// asked for them.
const FunctionInfo* entryIn(const Program& p, uint32_t pc) {
    const FunctionInfo* f = functionIn(p, pc);
    if (!f) return nullptr;
    if (g_opt.interpretOnly) return pc == f->start && f->replaced ? f : nullptr;
    const uint32_t* resume = p.info->resumePoints;
    return pc == f->start || std::binary_search(resume, resume + p.info->resumePointCount, pc) ? f : nullptr;
}

// Whether RAM holds f's instructions as the recompiler saw them. Patched
// words are left out on both sides: RAM has them patched or not, depending
// on who loaded the program.
bool inRam(const Program& p, const FunctionInfo& f) {
    if (!f.checksum) return true;
    const PatchInfo* patches = p.info->patches;
    const PatchInfo* patchesEnd = patches + p.info->patchCount;
    ps2uc::CodeChecksum sum;
    for (uint32_t a = f.start; a < f.end; a += 4) {
        const PatchInfo* q = std::lower_bound(patches, patchesEnd, a, [](const PatchInfo& x, uint32_t addr) { return x.address < addr; });
        if (q != patchesEnd && q->address == a) continue;
        sum.add(read32(a));
    }
    return sum.value == f.checksum;
}

// Whether RAM holds all of p's code, found out once per code epoch. A program
// the game loaded by itself gets the config's instruction patches then.
bool programInRam(Program& p) {
    if (p.presentEpoch == g_codeEpoch) return true;
    if (p.absentEpoch == g_codeEpoch) return false;
    for (size_t i = 0; i < p.info->functionCount; ++i) {
        if (!inRam(p, p.info->functions[i])) {
            p.absentEpoch = g_codeEpoch;
            return false;
        }
    }
    p.presentEpoch = g_codeEpoch;
    for (size_t i = 0; i < p.info->patchCount; ++i) write32(p.info->patches[i].address, p.info->patches[i].value);
    return true;
}

// The recompiled function to run at `pc`, if any: the running program's while
// its code there is in RAM, else that of another of the game's programs whose
// code is all in RAM, which the game loaded and jumped into by itself.
const FunctionInfo* compiledEntry(uint32_t pc) {
    Program& run = *g_running;
    if (const FunctionInfo* f = entryIn(run, pc)) {
        const auto i = static_cast<size_t>(f - run.info->functions);
        if (run.presentEpoch == g_codeEpoch || run.checkedEpoch[i] == g_codeEpoch) return f;
        if (inRam(run, *f)) {
            run.checkedEpoch[i] = g_codeEpoch;
            return f;
        }
    }
    for (Program& p : g_programs) {
        if (&p == g_running) continue;
        const FunctionInfo* f = entryIn(p, pc);
        if (!f || !programInRam(p)) continue;
        if (!p.entered) PS2_INFO("dispatch", "running %s, which the game loaded by itself", p.name.c_str());
        p.entered = true;
        g_running = &p;
        clearCache();
        return f;
    }
    return nullptr;
}

RecompFn lookupEntry(uint32_t pc) {
    CacheEntry& c = g_cache[(pc >> 2) & 8191];
    if (c.pc == pc) return c.fn;
    const FunctionInfo* f = compiledEntry(pc);
    if (!f) return nullptr;
    c = {pc, f->fn};
    return f->fn;
}

} // namespace

void initDispatch(const GameInfo& game) {
    g_game = &game;
    g_programs.assign(1 + game.programCount, Program{});
    initProgram(g_programs[0], &game, elfName(game), true);
    for (size_t i = 0; i < game.programCount; ++i) initProgram(g_programs[i + 1], game.programs[i], elfName(*game.programs[i]), false);
    g_codeEpoch = 1;
    g_programs[0].presentEpoch = g_codeEpoch;   // the runtime loaded it
    g_running = &g_programs[0];
    clearCache();
}

void programLoaded(const GameInfo* program, const std::string& name) {
    ++g_codeEpoch;   // RAM holds another program now
    clearCache();
    g_running = nullptr;
    for (Program& p : g_programs)
        if (program && p.info == program) g_running = &p;
    if (!g_running) {
        initProgram(g_uncompiled, nullptr, name, false);
        g_running = &g_uncompiled;
    }
    g_running->presentEpoch = g_codeEpoch;
}

void invalidateCompiledCode() {
    ++g_codeEpoch;
    clearCache();
}

const FunctionInfo* findFunction(uint32_t pc) { return functionIn(*g_running, pc); }

bool hasCompiledEntry(uint32_t pc) { return compiledEntry(pc) != nullptr; }

void recordMissingEntry(uint32_t pc) {
    if (g_running->missing->insert(pc).second) {
        const FunctionInfo* f = findFunction(pc);
        if (f)
            PS2_WARN("dispatch", "no resume label for 0x%08x inside %s; interpreting (add it to [analysis] entries)", pc, f->name);
        else
            PS2_WARN("dispatch", "no recompiled code at 0x%08x; interpreting (missing function or code loaded at run time)", pc);
    }
}

void writeMissingEntries() {
    for (const auto& [file, entries] : g_missing) {
        if (entries.empty() || file.empty()) continue;
        std::ofstream out(file);
        if (!out) continue;
        out << "# Addresses ps2uc had to interpret during the last run.\n"
            << "# Point [analysis].entries_files at this file and recompile to turn them into entry points.\n"
            << "entries = [\n";
        for (uint32_t a : entries) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "  0x%08x,\n", a);
            out << buf;
        }
        out << "]\n";
        PS2_INFO("dispatch", "%zu interpreted entry point(s) written to %s", entries.size(), file.string().c_str());
    }
}

void requestExit(int code) {
    g_exitRequested = true;
    g_exitCode = code;
    cpu.pc = kExitPc;
    g_slice = 0;   // the next loop back-edge reaches a safepoint, which unwinds
}

static void runTrampoline(uint32_t pc) {
    switch (pc) {
    case kTrampThreadExit:
        kernel::onThreadReturned();
        return;
    case kTrampExec:
        startPendingProgram();
        return;
    default:
        PS2_ERROR("dispatch", "jump to unknown trampoline 0x%08x", pc);
        requestExit(1);
        return;
    }
}

void dispatch() {
    const uint32_t pc = cpu.pc;
    if (isTrampoline(pc)) {
        runTrampoline(pc);
        return;
    }
    if (RecompFn fn = lookupEntry(pc)) {
        if (g_opt.traceFunctions) {
            const FunctionInfo* f = findFunction(pc);
            PS2_INFO("trace", "-> %s @0x%08x", f ? f->name : "?", pc);
        }
        fn();
        return;
    }
    if (!g_opt.interpretOnly) recordMissingEntry(pc);
    interpret();
}

void call(uint32_t target) {
    cpu.pc = target;
    if (isTrampoline(target)) return;   // e.g. returning to a host caller; let the loop see it
    if (RecompFn fn = lookupEntry(target)) {
        fn();
        return;
    }
    if (!g_opt.interpretOnly) recordMissingEntry(target);
    interpret();
}

uint64_t callGuestOnStack(uint32_t address, const uint64_t* args, int argCount, uint32_t stack) {
    static const uint32_t argRegs[8] = {regs::A0, regs::A1, regs::A2, regs::A3, regs::T0, regs::T1, regs::T2, regs::T3};
    const Cpu saved = cpu;
    for (int i = 0; i < argCount && i < 8; ++i) cpu.gpr[argRegs[i]].d[0] = args[i];
    cpu.gpr[regs::RA].d[0] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(kTrampHostReturn)));
    if (stack) cpu.gpr[regs::SP].d[0] = stack;
    cpu.pc = address;
    ++g_hostCallDepth;
    while (cpu.pc != kTrampHostReturn && !g_exitRequested) dispatch();
    --g_hostCallDepth;
    const uint64_t v0 = cpu.gpr[regs::V0].d[0];
    if (!g_exitRequested) cpu = saved;
    return v0;
}

uint64_t callGuest(uint32_t address, const uint64_t* args, int argCount) {
    return callGuestOnStack(address, args, argCount, 0);
}

void badEntry(uint32_t functionStart) {
    PS2_FALLBACK("dispatch", "entry without resume label", "function 0x%08x entered at 0x%08x, which has no resume label; interpreting",
                 functionStart, cpu.pc);
    recordMissingEntry(cpu.pc);
    interpret();
}

void skippedFunction(uint32_t addr, const char* name) {
    PS2_FALLBACK("skip", std::string("skipped function ") + name, "called skipped function %s (0x%08x); returning 0", name, addr);
    cpu.gpr[regs::V0].d[0] = 0;
}

} // namespace ps2
