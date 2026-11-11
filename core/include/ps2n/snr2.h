// SPDX-License-Identifier: GPL-3.0-or-later
// SN Systems ProDG SNR2 relocatable EE modules. The module is linked at zero
// for analysis; its own loader chooses the address and resolves the imports.
#pragma once

#include "ps2n/elf.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps2n {

struct Snr2Link {
    static constexpr uint32_t internal = 0xFFFFFFFFu;

    uint32_t offset = 0;                 // byte offset in the module
    uint32_t type = 0;                   // R_MIPS_*, or a linker-specific type
    uint32_t symbol = internal;          // external symbol-table index
    uint32_t addend = 0;
    uint32_t value = 0;                  // internal link's base-zero value
    bool external() const { return symbol != internal; }
};

struct Snr2Module {
    std::string name;                    // file name, also used for its entry-point log
    uint32_t size = 0;
    uint32_t crc = 0;
    uint32_t symbolTable = 0;
    uint32_t symbolCount = 0;
    uint32_t codeStart = 0, codeEnd = 0;
    std::vector<uint32_t> entryPoints;   // constructor/destructor entry seeds
    std::vector<ElfSymbol> symbols;
    std::vector<Snr2Link> links;         // sorted by offset
    std::vector<uint8_t> bytes;          // exactly size bytes, before relocation

    static Snr2Module load(const std::filesystem::path& path);
    static Snr2Module parse(std::vector<uint8_t> bytes, std::string fileName);

    // A fixed base-zero ELF image, with internal links applied. External
    // imports retain their file values until the game's loader resolves them.
    ElfFile image() const;
};

} // namespace ps2n
