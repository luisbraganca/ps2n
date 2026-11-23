// SPDX-License-Identifier: GPL-3.0-or-later
//
// Turns an analysed executable into C++ against runtime/include/ps2n/recomp.h:
// one function per guest function, and the tables by which the runtime finds
// them and checks them against the code in memory.
#pragma once

#include "analysis.h"

#include <cstdint>
#include <map>
#include <optional>
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
    // What the image holds (ImageInfo::kind in recomp.h): an executable's
    // code (0), a piece of the BIOS (1), the code an executable ran outside
    // its own (2), or an executable another loads itself (3). An image of
    // the second or third kind may hold no code at all.
    uint32_t kind = 0;
    // In place of the executable's file name and CRC: for the BIOS, the
    // dump's name and the CRC of the whole ROM (Bios in bios.h); for code
    // run outside an executable, and for an executable another loads itself,
    // that other's CRC.
    std::string name;
    std::optional<uint32_t> crc;
    // For an executable another loads itself (kind 3): its own CRC
    // (ImageInfo::program_crc).
    uint32_t programCrc = 0;
    // The settings of PCSX2's that what its recompiler decides of a COP2
    // instruction depends on, for the game the code is compiled for: its
    // vuFlagHack (on unless switched off, and PS2Native fixes it) and its
    // GameDB's FullVU0SyncHack (ps2n/rec_cop2.h; ADR 0046). The runtime
    // compares what is decided with its own record all the same.
    bool cop2FlagHack = true;
    bool cop2FullSync = false;
    // Whether to decide that at all (off: every COP2 instruction asks the
    // runtime, as before ADR 0046).
    bool cop2Decide = true;
    // For the first image of the code an executable ran outside its own
    // (kind 2): the ids of the images that hold other versions of that code
    // at the same addresses (ImageInfo::layers; ADR 0057), generated
    // beside it.
    std::vector<std::string> layerIds;
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
    size_t cop2Moves = 0;          // and transfers (QMFC2, QMTC2, LQC2, SQC2) whose move it makes where it may
    // Of those two kinds, the ones of which the recompiler decided what PCSX2's
    // recompiler decides, by their addresses and those bits (ImageInfo::cop2).
    std::vector<std::pair<uint32_t, uint32_t>> cop2Decided;
    size_t delayedDisables = 0;    // DI compiled in one piece with the instruction after it
    size_t onLocals = 0;           // instructions that work on the local variables registers are kept in
    size_t labelsHolding = 0;      // labels at which the locals hold registers, whichever way the label is reached
    size_t registersAtLabels = 0;  // and how many, over all those labels
    size_t variants = 0;           // other versions of functions, from code an earlier run found in memory
    size_t idleLoops = 0;          // branches that may close an idle loop, whose time is skipped
    size_t fixedLoops = 0;         // loops of one block that may reach a fixed point, whose times round after it are skipped
};

// The generated files: the main file first.
std::vector<GeneratedFile> generateFiles(const Program& prog, const GenerateOptions& options, GenerateStats* stats = nullptr);

// The generated translation unit, for one part.
std::string generate(const Program& prog, const GenerateOptions& options, GenerateStats* stats = nullptr);

// The suffixes of the files generateFiles() makes for `parts` parts, in its
// order: what a build system has to know before the recompiler has run.
std::vector<std::string> generatedSuffixes(unsigned parts);

// A translation unit that lists images for a host: the array
// `ps2n_images` and its length `ps2n_image_count`, and for the images of
// compiled micro code (vu_codegen.h) `ps2n_vu_images` and
// `ps2n_vu_image_count`.
std::string generateRegistry(const std::vector<std::string>& ids, const std::vector<std::string>& vuIds = {},
                             const std::vector<std::string>& iopIds = {});

// The CRC by which PCSX2 identifies an executable: the exclusive-or of its
// 32-bit words.
uint32_t pcsx2Crc(const ElfFile& elf);

// A C++ identifier made from a file name.
std::string identifierFor(const std::string& name);

extern const char* const kGeneratorName;

} // namespace ps2n::recomp
