// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files of a disc image in ISO 9660, the file system every PS2 disc
// carries (a DVD beside UDF): where the wizard, which starts from a disc
// image, takes a game's executables from (docs/phase4-proposal.md). An
// image of 2048-byte sectors (an .iso) is read where it is, a sector at a
// time; a DVD's is several gigabytes and is never read whole.
//
// Errors are reported by throwing ps2n::Error: this is the build-time
// tools' (ps2n-recomp extract), not the runtime's.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ps2n::iso {

inline constexpr uint32_t kSectorSize = 2048;

// Reads one sector into `out` (kSectorSize bytes); false when the image has
// no such sector.
using SectorReader = std::function<bool(uint32_t sector, uint8_t* out)>;

// A reader of an image file, open for as long as the reader is kept.
SectorReader openImage(const std::filesystem::path& path);

struct Entry {
    std::string name;  // as on the disc, without its version (";1")
    uint32_t sector = 0;  // where its data begins
    uint32_t size = 0;  // in bytes
    bool directory = false;
};

// The root directory of the image's primary volume.
Entry root(const SectorReader& read);

// The entries of a directory, but for "." and "..".
std::vector<Entry> list(const SectorReader& read, const Entry& directory);

// The entry at `path` from the root ("SLES_505.39", "MODULES/IOPRP.IMG"; "/"
// or "\" between the names), whatever the case of its letters, or nothing.
std::optional<Entry> find(const SectorReader& read, std::string_view path);

// A file's contents.
std::vector<uint8_t> readFile(const SectorReader& read, const Entry& file);

// The path of the executable SYSTEM.CNF's BOOT2 line names ("cdrom0:\SLES_505.39;1"
// gives "SLES_505.39"), or nothing when the disc has no SYSTEM.CNF or it names none.
std::optional<std::string> bootExecutable(const SectorReader& read);

} // namespace ps2n::iso
