// SPDX-License-Identifier: GPL-3.0-or-later
//
// Turns an analysed executable into C++ against runtime/include/ps2n/recomp.h:
// one function per guest function, and the tables by which the runtime finds
// them and checks them against the code in memory.
#pragma once

#include "analysis.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ps2n::recomp {

struct GenerateOptions {
    // Names the generated namespace, ps2n_image_<id>. A C++ identifier.
    std::string id;
    // A comment beside every instruction with its address and disassembly.
    bool comments = true;
};

struct GenerateStats {
    size_t functions = 0;
    size_t instructions = 0;       // owned by the functions
    size_t leaders = 0;
    size_t delegated = 0;          // instructions carried out by the core's interpreter function
    std::map<r5900::Op, size_t> delegatedByOp;
    size_t tails = 0;              // functions that run on into the code after them
    size_t tailInstructions = 0;   // instructions compiled a second time for that
    size_t leaves = 0;             // places where the rest of a block is left to the interpreter
    size_t guards = 0;             // likely branches left to it when taken, for what is in their delay slot
    size_t variants = 0;           // other versions of functions, from code an earlier run found in memory
    size_t idleLoops = 0;          // branches that close an idle loop, whose time is skipped
};

// The generated translation unit.
std::string generate(const Program& prog, const GenerateOptions& options, GenerateStats* stats = nullptr);

// A translation unit that lists images for a host: the array
// `ps2n_images` and its length `ps2n_image_count`.
std::string generateRegistry(const std::vector<std::string>& ids);

// The CRC by which PCSX2 identifies an executable: the exclusive-or of its
// 32-bit words.
uint32_t pcsx2Crc(const ElfFile& elf);

// A C++ identifier made from a file name.
std::string identifierFor(const std::string& name);

extern const char* const kGeneratorName;

} // namespace ps2n::recomp
