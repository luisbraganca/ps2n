// Host-side runtime API: what a recompiled game's main() and function table
// hand to the runtime.
#pragma once

#include "ps2/recomp.h"

#include <cstddef>
#include <cstdint>

namespace ps2 {

struct FunctionInfo {
    uint32_t start;     // address of the first instruction
    uint32_t end;       // one past the last instruction
    RecompFn fn;
    const char* name;
    bool replaced;      // a stub, skip or override from the config: runs even with --interpret
    // ps2n::CodeChecksum of the instruction words, patched ones left out,
    // to check that RAM still holds this code (0: not checked).
    uint32_t checksum;
};

// An instruction word the config replaced ([[patches]]), applied to RAM after
// the ELF is loaded so interpreted code sees it too.
struct PatchInfo {
    uint32_t address;
    uint32_t value;
};

struct GameInfo {
    const char* name;                  // executable name, used in logs
    const char* defaultElf;            // ELF the code was recompiled from (data is loaded from it at startup)
    uint32_t elfCrc32;                 // CRC-32 of that ELF; a different file triggers a warning
    uint32_t entry;                    // ELF entry point
    const FunctionInfo* functions;     // sorted by start address
    size_t functionCount;
    const uint32_t* resumePoints;      // sorted mid-function addresses that have a resume label
    size_t resumePointCount;
    const char* generator;             // recompiler version string
    const PatchInfo* patches;
    size_t patchCount;
    // The game's other executables ([input] programs), which it starts with
    // LoadExecPS2. Only the boot program's table lists them.
    const GameInfo* const* programs;
    size_t programCount;
};

// Loads the ELF, sets up the emulated kernel, hardware and IOP, runs the game.
// Returns the guest's exit code. Run `<game> --help` for the options.
int run(int argc, char** argv, const GameInfo& game);

// Calls a guest function from host code (overrides, callbacks) and returns
// $v0. Arguments go to $a0-$a3, $t0-$t3. The CPU context is saved and
// restored around the call.
uint64_t callGuest(uint32_t address, const uint64_t* args = nullptr, int argCount = 0);

} // namespace ps2
