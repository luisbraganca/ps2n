// What recompiled VU micro programs need (recompiler/src/vu_codegen.cpp
// writes them). A program is a function of an image of micro memory, entered
// where the program started; it runs pairs through the same operations as the
// interpreter (ps2/vu_micro.h) with the pipeline's timing worked out ahead,
// and returns when the program ends, or hands it back to the interpreter.
// It runs wherever micro memory holds its code: games upload a program at
// different addresses, and its branches are relative.
#pragma once

#include "ps2/vu_micro.h"

#include <cstddef>
#include <cstdint>

namespace ps2::vu {

constexpr uint32_t kEnded = 0xFFFFFFFFu;    // the program ran to its end
constexpr uint32_t kNotRun = 0xFFFFFFFEu;   // nothing ran: micro memory does not hold this code
// Pairs a program may run before the runtime stops it (as stuck): 50 million;
// tests that run random programs set fewer.
extern uint64_t g_stepLimit;

// Runs the program compiled at `entry` from `entry + base` (bytes, within
// micro memory): every address it reads or writes, TPC and links included,
// is its compiled address plus `base`. Returns kEnded, kNotRun, or the
// address the interpreter goes on from, with the pipeline written back to
// the unit.
using ProgramFn = uint32_t (*)(Unit& u, uint32_t entry, uint32_t base);

struct CompiledProgram {
    int unit;
    uint32_t entry;   // where it was compiled from
    ProgramFn fn;
    uint64_t key;     // ps2n::vu::entryKey of its first pairs, which the runtime finds it by
};

// Counts changes to each unit's micro memory (VIF MPG, EE stores): compiled
// code checks the words it was compiled from once per change.
extern uint64_t g_microGeneration[2];
bool microMatches(const Unit& u, uint32_t pc, const uint64_t* words, uint32_t pairs);

} // namespace ps2::vu

// One half of a pair, given its word: the operation is chosen at compile time
// by the decoding the interpreter does at run time.
#define PS2_VU_UPPER(word)                                                     \
    do {                                                                       \
        constexpr ::ps2::vu::Op op_ = ::ps2::vu::upperFn(word);                \
        op_(u, word);                                                          \
    } while (0)
#define PS2_VU_LOWER(word, unit)                                               \
    do {                                                                       \
        constexpr ::ps2::vu::Op op_ = ::ps2::vu::lowerFn(word, unit);          \
        op_(u, word);                                                          \
    } while (0)
// A pair whose lower half reads the VF register `reg` its upper half writes.
#define PS2_VU_ISOLATED(reg, upperWord, lowerWord, unit)                                    \
    do {                                                                                    \
        constexpr ::ps2::vu::Op up_ = ::ps2::vu::upperFn(upperWord);                        \
        constexpr ::ps2::vu::Op lo_ = ::ps2::vu::lowerFn(lowerWord, unit);                  \
        ::ps2::vu::runIsolated(u, reg, up_, upperWord, lo_, lowerWord);                     \
    } while (0)
