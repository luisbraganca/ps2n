// Which micro program a start address holds, wherever it sits: shared by
// the runtime (runtime/src/vu.cpp), which looks compiled programs up and
// records the ones it interpreted, and the recompiler's VU code generator
// (recompiler/src/vu_codegen.cpp).
//
// Games upload the same program at different addresses (Agent Under Fire's
// driving missions place each of their programs wherever micro memory has
// room). A program's branches are relative, so its words are the same
// wherever it sits: what it is can be told from its words and where they sit
// from its start, not from its address.
//
// `words(pc)` reads the pair at byte address `pc` (a multiple of 8, within
// `mask`) as lower word | upper word << 32, as micro memory holds it.
#pragma once

#include "ps2n/vu_micro.h"

#include <bitset>
#include <cstdint>
#include <vector>

namespace ps2n::vu {

constexpr uint64_t mixKey(uint64_t h, uint64_t v) {
    h = (h ^ v) * 0x100000001B3ull;
    return h ^ (h >> 29);
}

constexpr bool isBranchPair(const Pair& p) { return p.runLower && p.lower.branch; }

// The pairs a program starts with, up to and including its first branch or
// E bit and the pair after it, at most kEntryKeyPairs of them: the key
// compiled programs are found by. Programs whose keys match are told apart
// by the code each compiled one checks.
constexpr uint32_t kEntryKeyPairs = 8;
template <class Words>
uint64_t entryKey(const Words& words, uint32_t entry, uint32_t mask, int unit) {
    uint64_t h = 0xCBF29CE484222325ull ^ static_cast<uint64_t>(unit);
    uint32_t pc = entry & mask & ~7u;
    bool last = false;
    for (uint32_t n = 0; n < kEntryKeyPairs; ++n) {
        const uint64_t w = words(pc);
        h = mixKey(h, w);
        if (last) break;
        const Pair p = decodePair(static_cast<uint32_t>(w), static_cast<uint32_t>(w >> 32), unit);
        last = isBranchPair(p) || p.ebit;
        pc = (pc + 8) & mask;
    }
    return h;
}

// Everything a program reaches from `entry`: the blocks its branches and
// calls lead to, and the pairs after its calls (where its JRs return), up to
// the delay slots of its E bits; with their words and where they sit from
// the entry. Two starts with the same key run the same program, wherever
// each sits. JR and JALR targets are not followed.
template <class Words>
uint64_t programKey(const Words& words, uint32_t entry, uint32_t mask, int unit) {
    std::bitset<2048> walked, in;   // pairs (VU1 has 2048, VU0 512)
    std::vector<uint32_t> work{entry & mask & ~7u};
    while (!work.empty()) {
        uint32_t pc = work.back();
        work.pop_back();
        for (uint32_t n = 0; n <= mask / 8 && !walked[pc / 8]; ++n) {
            walked[pc / 8] = in[pc / 8] = true;
            const uint64_t w = words(pc);
            const auto lower = static_cast<uint32_t>(w);
            const Pair p = decodePair(lower, static_cast<uint32_t>(w >> 32), unit);
            const uint32_t slot = (pc + 8) & mask;
            if (isBranchPair(p)) {
                in[slot / 8] = true;   // its delay slot
                const uint32_t target = static_cast<uint32_t>(static_cast<int32_t>(slot) + imm11(lower) * 8) & mask & ~7u;
                const uint32_t after = (slot + 8) & mask;
                switch (p.lower.op) {
                case LowerOp::B: work.push_back(target); break;
                case LowerOp::Bal: work.push_back(target); work.push_back(after); break;
                case LowerOp::Jr: break;
                case LowerOp::Jalr: work.push_back(after); break;
                default: work.push_back(target); work.push_back(after); break;   // conditional
                }
                break;
            }
            if (p.ebit) {
                in[slot / 8] = true;
                break;
            }
            pc = slot;
        }
    }
    uint64_t h = 0x84222325CBF29CE4ull ^ static_cast<uint64_t>(unit);
    for (uint32_t k = 0; k <= mask / 8; ++k) {   // in order from the entry
        const uint32_t pc = (entry + 8 * k) & mask & ~7u;
        if (!in[pc / 8]) continue;
        h = mixKey(mixKey(h, k), words(pc));
    }
    return h;
}

} // namespace ps2n::vu
