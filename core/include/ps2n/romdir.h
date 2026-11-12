// SPDX-License-Identifier: GPL-3.0-or-later
//
// The directory of a PS2 BIOS image (its ROMDIR): the files the ROM holds,
// in order, and where each one is. The BIOS's own reset code finds its pieces
// this way, and so does PCSX2 (LoadBiosVersion in pcsx2/ps2/BiosTools.cpp),
// whose walk this follows. The recompiler takes the BIOS's EE code from the
// image through it; the runtime compares the code in memory with it.
//
// Nothing here throws: the runtime, which PCSX2's build compiles without C++
// exceptions, uses it too.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ps2n::bios {

struct RomFile {
    std::string name;     // at most ten characters
    uint32_t offset = 0;  // of its first byte, from the start of the image
    uint32_t size = 0;    // in bytes
};

// The files of the image's directory, in order, or none when the image has
// no directory. The directory is a file of its own (ROMDIR) whose first
// entry describes the first file, RESET; it is looked for on 16-byte
// boundaries. Each file begins where the one before it ends, rounded up to
// 16 bytes; the entries named "-" are space between files.
std::vector<RomFile> readRomDirectory(const uint8_t* image, size_t size);

// The file named `name`, or nothing.
std::optional<RomFile> findRomFile(const std::vector<RomFile>& files, std::string_view name);

// The files of the ROM that the EE runs from RAM under PCSX2's fast boot,
// and where each is copied to and runs: the kernel, the idle thread and the
// program loader, where PCSX2 has them (EEKERNEL_START, EENULL_START and
// EELOAD_START in pcsx2/R5900.h). The rest of the BIOS's EE code runs in the
// ROM itself, through either of its mirrors (BFC00000, and 9FC00000, which
// the processor caches): the reset code (RESET) and the memory's set-up
// (RDRAM), each at its place in the ROM.
struct RamPiece {
    const char* file;
    uint32_t address;
};
inline constexpr RamPiece kRamPieces[] = {
    {"KERNEL", 0x80000000u},
    {"EENULL", 0x00081FC0u},
    {"EELOAD", 0x00082000u},
};

} // namespace ps2n::bios
