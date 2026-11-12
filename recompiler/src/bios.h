// SPDX-License-Identifier: GPL-3.0-or-later
//
// The BIOS's EE code as images for the recompiler (docs/game-log.md, "G8"):
// the files of the ROM the EE runs (ps2n/romdir.h), each placed where the EE
// runs it, grouped into images whose addresses lie close together, so that
// the runtime can tell them apart by address.
//
// Everything made from these is derived from the user's BIOS dump. It is
// made in the build tree and never leaves it (docs/adr/0007).
#pragma once

#include "ps2n/elf.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps2n::recomp {

struct BiosImage {
    std::string id;                 // names the generated image: bios_rom0, bios_rom1, bios_kernel, bios_loader
    std::string what;               // in words, for reports
    ElfFile code;                   // the files, each where the EE runs it
    std::vector<uint32_t> entries;  // where execution is known to enter it, besides what runs report
};

struct Bios {
    std::string name;               // the dump's file name
    uint32_t crc = 0;               // of the whole ROM, as PCSX2 computes a game's CRC (the exclusive-or of its words)
    std::vector<BiosImage> images;
};

// Throws ps2n::Error when the image has no directory, or lacks a file the EE
// runs.
Bios readBios(const std::vector<uint8_t>& rom, const std::string& name);
Bios readBios(const std::filesystem::path& rom);

} // namespace ps2n::recomp
