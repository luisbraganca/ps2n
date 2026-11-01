// Writes the VU1 micro programs the vu_compiled test recompiles: a VU code
// file for `ps2n vu` (the format the runtime records), and the same images
// with their entries in binary for check.cpp (per image: the number of
// entries, the entries, then micro memory).
// usage: ps2n_vu_fuzz_generate <code.toml> <images.bin>
#include "fuzz.h"

#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kSize = 16384;

void write(FILE* toml, FILE* bin, const std::vector<uint8_t>& micro, const std::vector<uint32_t>& entries) {
    std::fprintf(toml, "[[image]]\nunit = 1\nentries = [");
    for (size_t i = 0; i < entries.size(); ++i) std::fprintf(toml, "%s0x%04x", i ? ", " : "", entries[i]);
    std::fprintf(toml, "]\ncode = \"\"\"\n");
    for (uint32_t at = 0; at < kSize; at += 32) {
        uint64_t w[4];
        std::memcpy(w, micro.data() + at, 32);
        std::fprintf(toml, "%04x %016llx %016llx %016llx %016llx\n", at, static_cast<unsigned long long>(w[0]),
                     static_cast<unsigned long long>(w[1]), static_cast<unsigned long long>(w[2]), static_cast<unsigned long long>(w[3]));
    }
    std::fprintf(toml, "\"\"\"\n\n");
    const auto n = static_cast<uint32_t>(entries.size());
    std::fwrite(&n, 4, 1, bin);
    std::fwrite(entries.data(), 4, n, bin);
    std::fwrite(micro.data(), 1, kSize, bin);
}

// Programs written out by hand, each ending in recompiled code: a loop, the
// branches that end a program with the E bit (MSCNT goes on where they go), a
// call and its return, Q through WAITQ, a pair whose lower half reads what its
// upper half writes.
std::vector<uint8_t> directed(std::vector<uint32_t>& entries) {
    std::vector<uint8_t> micro(kSize);
    const uint32_t nop = 0x000002FFu, lnop = 0x8000033Cu, ebit = 0x40000000u;
    auto put = [&](uint32_t pc, uint32_t lower, uint32_t upper) {
        std::memcpy(micro.data() + pc, &lower, 4);
        std::memcpy(micro.data() + pc + 4, &upper, 4);
    };
    auto off = [](uint32_t pc, uint32_t target) { return ((target - (pc + 8)) / 8) & 0x7FF; };   // from TPC
    auto iaddiu = [](uint32_t it, uint32_t is, uint32_t imm) { return (0x08u << 25) | (it << 16) | (is << 11) | (imm & 0x7FF); };
    auto iaddi = [](uint32_t it, uint32_t is, int imm) { return (0x40u << 25) | (it << 16) | (is << 11) | ((static_cast<uint32_t>(imm) & 31) << 6) | 0x32u; };
    auto add = [](uint32_t fd, uint32_t fs, uint32_t ft) { return (0xFu << 21) | (ft << 16) | (fs << 11) | (fd << 6) | 0x28u; };
    auto mulq = [](uint32_t fd, uint32_t fs) { return (0xEu << 21) | (fs << 11) | (fd << 6) | 0x1Cu; };
    // 0x0000: a loop, then B with the E bit: the program ends with TPC at 0x0100.
    put(0x000, iaddiu(1, 0, 3), nop);
    put(0x008, iaddi(1, 1, -1), add(3, 1, 2));
    put(0x010, (0x29u << 25) | (1u << 11) | off(0x010, 0x008), nop);   // IBNE vi01, vi00, 0x008
    put(0x018, lnop, add(4, 3, 3));
    put(0x020, (0x20u << 25) | off(0x020, 0x100), nop | ebit);        // B 0x0100 [E]
    put(0x028, lnop, add(5, 4, 1));
    // 0x0100: BAL to a routine, which JRs back; the E bit after.
    put(0x100, (0x21u << 25) | (15u << 16) | off(0x100, 0x180), nop);  // BAL vi15, 0x0180
    put(0x108, iaddiu(2, 0, 7), nop);
    put(0x110, lnop, nop | ebit);
    put(0x118, lnop, add(6, 5, 5));
    put(0x180, iaddi(3, 3, 1), add(5, 3, 4));
    put(0x188, (0x24u << 25) | (15u << 11), nop);                     // JR vi15
    put(0x190, lnop, nop);
    // 0x0200: a conditional branch with the E bit; it is taken.
    put(0x200, (0x28u << 25) | off(0x200, 0x280), nop | ebit);        // IBEQ vi00, vi00, 0x0280 [E]
    put(0x208, lnop, add(7, 1, 1));
    // 0x0300: JR with the E bit, to an address set by IADDIU.
    put(0x300, iaddiu(4, 0, 0x380 / 8), nop);
    put(0x308, (0x24u << 25) | (4u << 11), nop | ebit);               // JR vi04 [E]
    put(0x310, lnop, nop);
    // 0x0400: DIV, WAITQ with an operation on Q, then a lower half reading
    // the register its upper half writes.
    put(0x400, 0x80000000u | (1u << 23) | (0u << 21) | (2u << 16) | (1u << 11) | (0xEu << 6) | 0x3Cu, nop);   // DIV Q, vf01x, vf02y
    put(0x408, 0x80000000u | (0xEu << 6) | 0x3Fu, mulq(7, 1));         // WAITQ | MULq.xyz vf07, vf01, Q
    put(0x410, (0x40u << 25) | (0xFu << 21) | (8u << 16) | (7u << 11) | (0xCu << 6) | 0x3Cu, add(7, 1, 2));   // MOVE vf08, vf07 | ADD vf07
    put(0x418, lnop, nop | ebit);
    put(0x420, lnop, nop);
    entries = {0x000, 0x100, 0x200, 0x300, 0x400};
    return micro;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <code.toml> <images.bin>\n", argv[0]);
        return 2;
    }
    FILE* toml = std::fopen(argv[1], "w");
    FILE* bin = std::fopen(argv[2], "wb");
    if (!toml || !bin) return 1;
    std::vector<uint32_t> entries;
    const std::vector<uint8_t> handWritten = directed(entries);
    write(toml, bin, handWritten, entries);
    // Random images, with E bits and branches from sparse (long programs,
    // loops) to dense (branches in delay slots, which the interpreter takes over).
    struct Setting {
        uint32_t ePercent, branchPercent;
    };
    const Setting settings[] = {{2, 5}, {1, 3}, {3, 10}, {2, 5}, {3, 20}, {5, 30}, {3, 100}, {1, 10}};
    vufuzz::Random random(0x5EED0001);
    for (const Setting& s : settings) {
        std::vector<uint8_t> micro(kSize);
        for (uint32_t pc = 0; pc < kSize; pc += 8) random.pair(micro.data(), pc, s.ePercent, s.branchPercent);
        entries.assign(4, 0);
        for (uint32_t& e : entries) e = random.below(kSize / 8) * 8;
        write(toml, bin, micro, entries);
    }
    std::fclose(toml);
    std::fclose(bin);
    return 0;
}
