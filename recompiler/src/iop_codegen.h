// SPDX-License-Identifier: GPL-3.0-or-later
//
// The I/O processor's code, compiled ahead of time: turns the blocks a run
// captured (the host's -iop-capture file) into C++ against
// runtime/include/ps2n/iop_recomp.h, one function for each block, and the
// table by which the runtime finds them by the key of their instructions.
//
// A captured block is a block as PCSX2's IOP recompiler formed it in that
// run: its instructions, from its first to its branch's delay slot, or to
// where it was cut short. What an instruction does is not decided here but in
// ps2n/iop_ops.h, which the interpreter uses too; what the generator decides
// is which of the IOP's registers the function holds in locals, when it
// takes them from the core and when it puts them back.
#pragma once

#include "codegen.h"

#include "ps2n/r3000a.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps2n::recomp {

// What runs reported for the IOP's recompiler: the blocks they had to
// interpret.
struct IopCapture {
    struct Block {
        uint32_t pc = 0;                 // where it was first met; for the reader
        std::vector<uint32_t> words;     // its instructions
        r3000a::Key key;                 // of its instructions, computed here
    };
    std::vector<Block> blocks;

    void add(const IopCapture& other);
    // Sorts the blocks by key and drops what is there twice.
    void normalize();
};

IopCapture parseIopCapture(const std::string& text, const std::string& fileName);
IopCapture loadIopCaptureFile(const std::filesystem::path& file);

// A capture as a file, in the form the host writes and parseIopCapture() reads.
std::string writeIopCapture(const IopCapture& capture, const std::string& comment);

struct IopGenerateOptions {
    // Names the generated namespace, ps2n_iop_image_<id>. A C++ identifier.
    std::string id;
    // How many files the functions are spread over, as GenerateOptions::parts.
    unsigned parts = 1;
    // With more than one part: the name the part files include the header by.
    std::string headerName;
    // A comment before every function with where its block was met and its words.
    bool comments = true;
};

struct IopGenerateStats {
    size_t blocks = 0;
    size_t instructions = 0;
    size_t skipped = 0;   // blocks the provider would refuse, and so never runs
};

// The generated files, the main file first (GeneratedFile in codegen.h).
// `capture` must be normalized.
std::vector<GeneratedFile> generateIopFiles(const IopCapture& capture, const IopGenerateOptions& options, IopGenerateStats* stats = nullptr);

} // namespace ps2n::recomp
