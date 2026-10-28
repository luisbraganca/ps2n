// Dispatcher: maps guest addresses to recompiled functions, and falls back to
// the interpreter for code the recompiler did not cover.
#include "internal.h"
#include "kernel/kernel.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>

namespace ps2 {

Cpu cpu;
const GameInfo* g_game = nullptr;
bool g_exitRequested = false;
int g_exitCode = 0;
uint64_t g_switchGeneration = 0;

namespace {

const FunctionInfo* g_functions = nullptr;
size_t g_functionCount = 0;
const uint32_t* g_resume = nullptr;
size_t g_resumeCount = 0;

struct CacheEntry {
    uint32_t pc;
    RecompFn fn;
};
CacheEntry g_cache[8192];

std::set<uint32_t> g_missing;   // addresses that had to be interpreted

int g_hostCallDepth = 0;

} // namespace

void initDispatch(const GameInfo& game) {
    g_game = &game;
    g_functions = game.functions;
    g_functionCount = game.functionCount;
    g_resume = game.resumePoints;
    g_resumeCount = game.resumePointCount;
    for (auto& e : g_cache) e = {0xFFFFFFFFu, nullptr};
    for (size_t i = 1; i < g_functionCount; ++i)
        if (g_functions[i - 1].start >= g_functions[i].start)
            PS2_ERROR("dispatch", "function table is not sorted at %s (0x%08x)", g_functions[i].name, g_functions[i].start);
}

const FunctionInfo* findFunction(uint32_t pc) {
    const FunctionInfo* end = g_functions + g_functionCount;
    const FunctionInfo* it = std::upper_bound(g_functions, end, pc, [](uint32_t a, const FunctionInfo& f) { return a < f.start; });
    if (it == g_functions) return nullptr;
    --it;
    return pc < it->end ? it : nullptr;
}

// The recompiled function to run at `pc`, if any. With --interpret only host
// replacements (stubs, skips, overrides) count: they have no guest code to
// interpret, and the config asked for them.
static const FunctionInfo* compiledEntry(uint32_t pc) {
    const FunctionInfo* f = findFunction(pc);
    if (!f) return nullptr;
    if (g_opt.interpretOnly) return pc == f->start && f->replaced ? f : nullptr;
    return pc == f->start || std::binary_search(g_resume, g_resume + g_resumeCount, pc) ? f : nullptr;
}

bool hasCompiledEntry(uint32_t pc) { return compiledEntry(pc) != nullptr; }

static RecompFn lookupEntry(uint32_t pc) {
    CacheEntry& c = g_cache[(pc >> 2) & 8191];
    if (c.pc == pc) return c.fn;
    const FunctionInfo* f = compiledEntry(pc);
    if (!f) return nullptr;
    c = {pc, f->fn};
    return f->fn;
}

void recordMissingEntry(uint32_t pc) {
    if (g_missing.insert(pc).second) {
        const FunctionInfo* f = findFunction(pc);
        if (f)
            PS2_WARN("dispatch", "no resume label for 0x%08x inside %s; interpreting (add it to [analysis] entries)", pc, f->name);
        else
            PS2_WARN("dispatch", "no recompiled code at 0x%08x; interpreting (missing function or code loaded at run time)", pc);
    }
}

void writeMissingEntries() {
    if (g_missing.empty() || g_opt.missingOut.empty()) return;
    std::ofstream out(g_opt.missingOut);
    if (!out) return;
    out << "# Addresses ps2uc had to interpret during the last run.\n"
        << "# Point [analysis].entries_files at this file and recompile to turn them into entry points.\n"
        << "entries = [\n";
    for (uint32_t a : g_missing) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "  0x%08x,\n", a);
        out << buf;
    }
    out << "]\n";
    PS2_INFO("dispatch", "%zu interpreted entry point(s) written to %s", g_missing.size(), g_opt.missingOut.string().c_str());
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
