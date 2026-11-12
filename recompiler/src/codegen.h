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
    // How many files the functions are spread over. With one, everything is
    // in a single translation unit and the functions are local to it. With
    // more, for a program too large to compile as one: the functions go into
    // that many part files, in address order and of about equal size, a
    // header declares them, and the main file holds the tables.
    unsigned parts = 1;
    // With more than one part: the name the part files include the header by.
    std::string headerName;
};

// One generated file. `suffix` tells which, and names it once put after the
// output's stem: ".cpp" is the main file (the only one when there is one
// part), ".h" the header, "_part001.cpp" and so on the parts.
struct GeneratedFile {
    std::string suffix;
    std::string text;
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
    size_t cop2 = 0;               // COP2 instructions, which the runtime carries out whole or decides about
    size_t vu0 = 0;                // of those, VU0 instructions that compiled code carries out itself where it may
    size_t delayedDisables = 0;    // DI compiled in one piece with the instruction after it
    size_t onLocals = 0;           // instructions that work on the local variables registers are kept in
    size_t labelsHolding = 0;      // labels at which the locals hold registers, whichever way the label is reached
    size_t registersAtLabels = 0;  // and how many, over all those labels
    size_t variants = 0;           // other versions of functions, from code an earlier run found in memory
    size_t idleLoops = 0;          // branches that may close an idle loop, whose time is skipped
};

// The generated files: the main file first.
std::vector<GeneratedFile> generateFiles(const Program& prog, const GenerateOptions& options, GenerateStats* stats = nullptr);

// The generated translation unit, for one part.
std::string generate(const Program& prog, const GenerateOptions& options, GenerateStats* stats = nullptr);

// The suffixes of the files generateFiles() makes for `parts` parts, in its
// order: what a build system has to know before the recompiler has run.
std::vector<std::string> generatedSuffixes(unsigned parts);

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
