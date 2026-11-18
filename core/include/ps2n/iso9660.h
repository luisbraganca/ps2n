// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files of a disc image in ISO 9660, the file system every PS2 disc
// carries (a DVD beside UDF): where the wizard, which starts from a disc
// image, takes a game's executables from (docs/phase4-proposal.md). An
// image is read where it is, a sector at a time; a DVD's is several
// gigabytes and is never read whole. The sectors come from a reader: an
// image of 2048-byte sectors (an .iso) is read by openImage() below; the
// runtime reads every format PCSX2 plays through PCSX2's own readers
// (ps2n-host -disc-executables, -disc-extract; host/disc.cpp).
//
// A broken image (no ISO 9660 file system, a record that does not fit, a
// sector past its end) is reported without exceptions, which PCSX2's build
// switches off in the runtime: by an empty result, and what is wrong in
// *error. The build-time tools (ps2n-recomp extract) turn it into an Error.
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

// Reads one sector's 2048 bytes of data into `out`; false when the image has
// no such sector.
using SectorReader = std::function<bool(uint32_t sector, uint8_t* out)>;

// A reader of an image file of 2048-byte sectors, open for as long as the
// reader is kept; an empty one when the file cannot be opened.
SectorReader openImage(const std::filesystem::path& path);

struct Entry {
    std::string name;  // as on the disc, without its version (";1")
    uint32_t sector = 0;  // where its data begins
    uint32_t size = 0;  // in bytes
    bool directory = false;
};

// Each function below says in *error (never null) what is wrong with a broken
// image, and returns nothing then.

// The root directory of the image's primary volume.
std::optional<Entry> root(const SectorReader& read, std::string* error);

// The entries of a directory, but for "." and "..".
std::optional<std::vector<Entry>> list(const SectorReader& read, const Entry& directory, std::string* error);

// The entry at `path` from the root ("SLES_505.39", "MODULES/IOPRP.IMG"; "/"
// or "\" between the names), whatever the case of its letters; nothing, with
// *error empty, when there is none.
std::optional<Entry> find(const SectorReader& read, std::string_view path, std::string* error);

// A file's contents.
std::optional<std::vector<uint8_t>> readFile(const SectorReader& read, const Entry& file, std::string* error);

// The path of the executable SYSTEM.CNF's BOOT2 line names ("cdrom0:\SLES_505.39;1"
// gives "SLES_505.39"); nothing, with *error empty, when the disc has no
// SYSTEM.CNF or it names none.
std::optional<std::string> bootExecutable(const SectorReader& read, std::string* error);

// The EE's executables on the disc, by their headers (ELF, 32 bits,
// little-endian, an executable for MIPS; an IOP module has a type of its own):
// the one SYSTEM.CNF boots first, as bootExecutable() names it, then the others
// in the order of the disc's directories, by their path from the root
// ("SUB/GAME.ELF"). A game made of several executables loads the others
// itself (docs/adr/0026); the wizard compiles those its user keeps.
std::optional<std::vector<std::string>> executables(const SectorReader& read, std::string* error);

} // namespace ps2n::iso
