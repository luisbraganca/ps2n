// External function maps: CSV exported from Ghidra (our script or Ghidra's
// own Functions / Symbol Table windows), and entry lists written by the
// runtime (ps2uc_missing.toml).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ps2uc {

struct MapEntry {
    uint32_t start = 0;
    std::optional<uint32_t> end;
    std::string name;
};

// Recognised layouts (header names are case-insensitive, quotes optional):
//   Name,Start,End,Size           tools/ghidra/ExportPs2ucFunctions.java (and PS2Recomp)
//   Name,Location,...,Function Size   Ghidra "Functions" window export
//   Name,Location,Type,...        Ghidra "Symbol Table" export (rows of type Function)
// Addresses may be 0x-prefixed or bare hex (Ghidra style), with an optional
// "ram:" space prefix. Sizes are decimal unless 0x-prefixed.
std::vector<MapEntry> loadFunctionMap(const std::filesystem::path& file);

// "entries = [0x..., ...]" files produced by the runtime.
std::vector<uint32_t> loadEntriesFile(const std::filesystem::path& file);

} // namespace ps2uc
