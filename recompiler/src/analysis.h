// SPDX-License-Identifier: GPL-3.0-or-later
//
// Analysis of an EE executable for static recompilation: which addresses hold
// code, how that code divides into functions, and where its blocks begin.
//
// Function boundaries here are a matter of speed, never of correctness. A
// boundary in the wrong place only turns a jump inside one C++ function into a
// call between two. What must be right is the set of block leaders, and that
// is derived from the instructions themselves.
#pragma once

#include "ps2n/elf.h"
#include "ps2n/r5900.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ps2n::recomp {

// Where a function start came from, strongest first.
enum class Source : uint8_t { Symbol, Entry, Jal, DataRef, CodePointer, Prologue, Gap };
const char* sourceName(Source s);

struct Function {
    uint32_t start = 0;
    uint32_t end = 0;                 // exclusive: the instructions this function owns
    std::string name;                 // its symbol, or empty
    Source source = Source::Gap;

    // The addresses in [start, end) at which a block can begin, sorted, `start`
    // among them. Each is a label in the generated function and an address at
    // which it can be entered.
    std::vector<uint32_t> leaders;

    // Other code an earlier run found in this function's place in memory:
    // each is the words for [start, end). Compiled as well, with the same
    // leaders, and chosen at run time by what memory holds.
    std::vector<std::vector<uint32_t>> variants;

    bool isLeader(uint32_t addr) const;
    std::string displayName() const;  // the symbol, or sub_XXXXXXXX
};

struct Note {
    uint32_t address;
    std::string message;
};

// What the runtime reported from an earlier run (ps2n-host -capture).
struct Capture {
    // Addresses at which it had to start interpreting a block: block leaders
    // the analysis could not see.
    std::vector<uint32_t> entries;

    // Code it found in memory in the place of a function.
    struct Code {
        uint32_t start = 0;
        std::vector<uint32_t> words;
    };
    std::vector<Code> code;

    void add(const Capture& other);
};

struct Options {
    // Take function starts and sizes from the ELF's symbol table when it has one.
    bool useSymbols = true;
    Capture capture;
};

struct Program {
    const ElfFile* elf = nullptr;
    std::vector<AddrRange> ranges;        // executable address ranges, sorted
    std::vector<Function> functions;      // sorted by start, not overlapping
    std::vector<Note> notes;
    std::map<Source, size_t> sourceCounts;
    size_t instructionCount = 0;
    size_t leaderCount = 0;
    size_t variantCount = 0;

    // Whether `addr` is the address of an instruction.
    bool inCode(uint32_t addr) const;
    // The instruction word at `addr`; zero outside the loaded image.
    uint32_t word(uint32_t addr) const;
    r5900::Insn insn(uint32_t addr) const { return r5900::Insn(word(addr), addr); }
    // The same as variant `variant` of `f` has them (-1: the function itself).
    uint32_t word(const Function& f, int variant, uint32_t addr) const;
    r5900::Insn insn(const Function& f, int variant, uint32_t addr) const { return r5900::Insn(word(f, variant, addr), addr); }

    const Function* functionAt(uint32_t start) const;
    const Function* functionContaining(uint32_t addr) const;
    // The function that can be entered at `addr`, or null.
    const Function* entryAt(uint32_t addr) const;
    uint32_t indexOf(const Function& f) const { return static_cast<uint32_t>(&f - functions.data()); }
};

Program analyze(const ElfFile& elf, const Options& options = {});

// Reads a capture file written by ps2n-host:
//
//   entries = [ 0x00100040, ... ]
//
//   [[code]]
//   start = 0x00100098
//   words = [ 0x03e00008, 0x24020002, ... ]
//
// '#' starts a comment. Throws ps2n::Error on anything else.
Capture loadCaptureFile(const std::filesystem::path& file);
Capture parseCapture(const std::string& text, const std::string& fileName);

} // namespace ps2n::recomp
