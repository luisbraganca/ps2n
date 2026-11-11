// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fingerprint of a run of instruction words: 64-bit FNV-1a over their bytes.
//
// The recompiler stores one per function it compiles. Before a compiled
// function runs, the runtime computes the same over the words in guest
// memory: equal fingerprints mean the memory holds the code the function was
// compiled from. A generated program thus carries fingerprints of the game's
// code, not the code.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2n {

struct CodeHash {
    uint64_t value = 14695981039346656037ull;

    void add(uint32_t word) {
        for (int i = 0; i < 4; ++i) {
            value = (value ^ (word & 0xFFu)) * 1099511628211ull;
            word >>= 8;
        }
    }

    static uint64_t of(const uint32_t* words, size_t count) {
        CodeHash h;
        for (size_t i = 0; i < count; ++i) h.add(words[i]);
        return h.value;
    }
};

} // namespace ps2n
