// SPDX-License-Identifier: GPL-3.0-or-later
//
// The vector units' micro code, compiled ahead of time: turns the plans a
// run captured (ps2n/vu_plan.h; the host's -vu-capture file) into C++
// against runtime/include/ps2n/vu_recomp.h, one function for each plan, and
// the table by which the runtime finds them by their plans' keys.
//
// A plan is a block of micro code with everything decided already: the
// generator only writes its steps down as calls, each with its operands as
// constants. What an instruction does is not decided here but in
// ps2n/vu_ops.h, which the interpreter uses for the same steps.
#pragma once

#include "codegen.h"

#include "ps2n/vu_plan.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps2n::recomp {

// What runs reported for the VU recompiler: the plans of the blocks they
// had to interpret.
struct VuCapture {
    struct Block {
        int vu = 0;
        uint32_t at = 0;                 // where it was first met; for the reader
        std::vector<uint64_t> words;     // its pairs then (lower word | upper << 32); for the reader
        std::vector<vu::Step> steps;     // the plan
        vu::PlanKey key;                 // of the plan, computed here
    };
    std::vector<Block> blocks;

    void add(const VuCapture& other);
    // Sorts the blocks by VU and key and drops what is there twice.
    void normalize();
};

VuCapture parseVuCapture(const std::string& text, const std::string& fileName);
VuCapture loadVuCaptureFile(const std::filesystem::path& file);

// A capture as a file, in the form the host writes and parseVuCapture() reads.
std::string writeVuCapture(const VuCapture& capture, const std::string& comment);

struct VuGenerateOptions {
    // Names the generated namespace, ps2n_vu_image_<id>. A C++ identifier.
    std::string id;
    // How many files the functions are spread over, as GenerateOptions::parts.
    unsigned parts = 1;
    // With more than one part: the name the part files include the header by.
    std::string headerName;
    // A comment before every function with where its block was met and its pairs' words.
    bool comments = true;
};

struct VuGenerateStats {
    size_t blocks[2] = {0, 0};   // per VU
    size_t steps = 0;
};

// The generated files, the main file first (GeneratedFile in codegen.h).
// `capture` must be normalized.
std::vector<GeneratedFile> generateVuFiles(const VuCapture& capture, const VuGenerateOptions& options, VuGenerateStats* stats = nullptr);

} // namespace ps2n::recomp
