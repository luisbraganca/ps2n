// Program analysis: function discovery, boundaries, bindings and per-function
// control flow (labels, resume points, jump tables).
#pragma once

#include "config.h"

#include "ps2n/elf.h"
#include "ps2n/r5900.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps2n {

// Where a function start came from, strongest first.
enum class Source : uint8_t { Config, Map, Symbol, Entry, ExtraEntry, Jal, DataRef, CodePointer, Prologue, Gap };
const char* sourceName(Source s);

enum class Binding : uint8_t { Recompile, Stub, Skip, Override };

struct Function {
    uint32_t start = 0;
    uint32_t end = 0;                       // exclusive
    std::string name;                       // symbol or sub_XXXXXXXX
    std::string cname;                      // C++ identifier
    Source source = Source::Gap;
    Binding binding = Binding::Recompile;
    std::string handler;                    // stub handler / override symbol

    // Filled in for Binding::Recompile.
    std::vector<r5900::Insn> insns;         // start..end, plus a trailing delay slot if the last insn needs one
    std::set<uint32_t> labels;              // addresses that need a C++ label
    std::set<uint32_t> resume;              // mid-function entry points (dispatcher / cross-function)
    std::map<uint32_t, std::vector<uint32_t>> jumpTables;   // jr address -> local targets
    std::set<uint32_t> switchCandidates;    // local targets for unresolved indirect jumps
    std::vector<uint32_t> unresolvedJumps;  // jr sites with no resolved table
};

struct Diagnostic {
    uint32_t address;
    std::string message;
};

struct Program {
    const ElfFile* elf = nullptr;
    const Config* config = nullptr;
    std::vector<Function> functions;        // sorted by start
    std::unordered_map<uint32_t, uint32_t> patched;   // address -> patched instruction word
    std::map<uint32_t, std::string> labelNames;        // symbol names inside functions (comments)
    std::vector<Diagnostic> warnings;
    std::vector<Diagnostic> unknownInstructions;
    std::map<Source, size_t> sourceCounts;
    size_t instructionCount = 0;
    size_t resolvedJumpTables = 0;

    uint32_t word(uint32_t addr) const;     // instruction word, with patches applied
    const Function* functionAt(uint32_t start) const;
    const Function* functionContaining(uint32_t addr) const;
    Function* functionContaining(uint32_t addr);
    std::string describe(uint32_t addr) const;   // "name+0x10" for comments
};

// The names of the runtime's stub handlers (from runtime/include/ps2/stub_list.inc).
const std::vector<std::string>& runtimeStubNames();
bool isRuntimeStub(const std::string& name);

Program analyze(const ElfFile& elf, const Config& cfg);

} // namespace ps2n
