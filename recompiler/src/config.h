// Recompilation config (TOML). See docs/config.md for the full reference.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ps2uc {

// A function referenced from the config: by symbol name or by address.
struct FunctionRef {
    std::string text;                  // as written ("printf", "0x00123456")
    std::optional<uint32_t> address;   // set when the text is an address
};

struct ExplicitFunction {
    std::string name;
    uint32_t start = 0;
    std::optional<uint32_t> end;
};

struct Patch {
    uint32_t address = 0;
    uint32_t value = 0;
    std::optional<uint32_t> expect;
};

struct Config {
    std::filesystem::path configDir;   // relative paths resolve against this

    // [input]
    std::filesystem::path elf;
    std::vector<std::filesystem::path> functionMaps;   // ghidra_map + map_files

    // [output]
    std::filesystem::path outDir = "out";
    std::string name;                  // executable name (default: ELF stem)
    bool singleFile = false;
    int files = 0;                     // multi-file: fixed file count (0 = by size)
    bool comments = true;
    bool project = true;               // write CMakeLists.txt + main
    std::filesystem::path runtimeDir;  // ps2-uncompiler source tree for the generated CMakeLists
    std::vector<std::filesystem::path> extraSources;   // user code (overrides) added to the generated project

    // [analysis]
    bool useSymbols = true;
    bool scanJalTargets = true;
    bool scanDataPointers = true;
    bool scanPrologues = false;
    bool jumpTables = true;
    bool preemptLoops = true;
    bool splitGaps = true;             // make functions out of unnamed code between known ones
    std::vector<uint32_t> entries;
    std::vector<std::filesystem::path> entriesFiles;
    std::vector<ExplicitFunction> functions;

    // [stubs]
    bool autoStubs = true;
    std::vector<std::pair<FunctionRef, std::string>> stubBindings;   // target -> handler
    std::vector<std::string> stubExclude;

    // [functions]
    std::vector<FunctionRef> skip;
    std::vector<std::pair<FunctionRef, std::string>> overrides;      // target -> C++ symbol

    // [[patches]]
    std::vector<Patch> patches;

    static Config load(const std::filesystem::path& file);
    // A config for an ELF without a TOML file (analyze, quick runs).
    static Config forElf(const std::filesystem::path& elf);
    std::filesystem::path resolve(const std::filesystem::path& p) const;
};

FunctionRef parseFunctionRef(const std::string& text);

} // namespace ps2uc
