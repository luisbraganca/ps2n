// SPDX-License-Identifier: GPL-3.0-or-later
#include "bios.h"

#include "ps2n/romdir.h"
#include "ps2n/util.h"

#include <cstring>
#include <iterator>
#include <optional>

namespace ps2n::recomp {

namespace {

// The ROM through its two mirrors: the one the processor caches, and the one
// it does not, where the EE starts.
constexpr uint32_t kRomCached = 0x9FC00000u;
constexpr uint32_t kRomUncached = 0xBFC00000u;

// The files the EE runs in the ROM itself (ps2n/romdir.h): the reset code and
// the set-up of main memory.
constexpr const char* kRomFiles[] = {"RESET", "RDRAM"};

// The EE's exception vectors once the kernel handles exceptions (Status.BEV
// clear): the TLB refill, the performance counter, debug, all others, and
// interrupts.
constexpr uint32_t kKernelVectors[] = {0x80000000u, 0x80000080u, 0x80000100u, 0x80000180u, 0x80000200u};

uint32_t ramAddress(const char* file) {
    for (const bios::RamPiece& p : bios::kRamPieces)
        if (std::strcmp(p.file, file) == 0) return p.address;
    throw Error(std::string("no place in RAM is known for the BIOS's ") + file);
}

} // namespace

Bios readBios(const std::vector<uint8_t>& rom, const std::string& name) {
    const std::vector<bios::RomFile> files = bios::readRomDirectory(rom.data(), rom.size());
    if (files.empty()) throw Error(name + ": not a BIOS image (no ROM directory found in it)");
    const auto file = [&](const char* fileName) {
        const std::optional<bios::RomFile> f = bios::findRomFile(files, fileName);
        if (!f) throw Error(name + ": the ROM's directory has no " + fileName);
        return *f;
    };
    const auto piece = [&](const bios::RomFile& f, uint32_t address) {
        ElfFile::Piece p;
        p.name = f.name;
        p.address = address;
        p.bytes.assign(rom.begin() + f.offset, rom.begin() + f.offset + f.size);
        return p;
    };

    Bios b;
    b.name = name;
    for (size_t i = 0; i + 4 <= rom.size(); i += 4) {
        b.crc ^= static_cast<uint32_t>(rom[i]) | static_cast<uint32_t>(rom[i + 1]) << 8 | static_cast<uint32_t>(rom[i + 2]) << 16 |
                 static_cast<uint32_t>(rom[i + 3]) << 24;
    }

    // The reset code and the memory's set-up, in the ROM itself, through each
    // mirror: the code runs through both.
    for (const uint32_t base : {kRomCached, kRomUncached}) {
        std::vector<ElfFile::Piece> pieces;
        for (const char* f : kRomFiles) {
            const bios::RomFile rf = file(f);
            pieces.push_back(piece(rf, base + rf.offset));
        }
        BiosImage image;
        image.id = base == kRomCached ? "bios_rom0" : "bios_rom1";
        image.what = base == kRomCached ? "the ROM's code through its cached mirror (RESET, RDRAM from 9FC00000)"
                                        : "the ROM's code (RESET, RDRAM from BFC00000)";
        image.code = ElfFile::fromPieces(name + " (" + image.id + ")", base == kRomUncached ? kRomUncached : 0, pieces);
        if (base == kRomUncached) image.entries.push_back(kRomUncached);  // where the EE starts
        b.images.push_back(std::move(image));
    }

    {
        BiosImage image;
        image.id = "bios_kernel";
        image.what = "the kernel (KERNEL at 80000000)";
        image.code = ElfFile::fromPieces(name + " (" + image.id + ")", kKernelVectors[0], {piece(file("KERNEL"), ramAddress("KERNEL"))});
        image.entries.assign(std::begin(kKernelVectors), std::end(kKernelVectors));
        b.images.push_back(std::move(image));
    }

    {
        BiosImage image;
        image.id = "bios_loader";
        image.what = "the idle thread and the program loader (EENULL at 00081FC0, EELOAD at 00082000)";
        const uint32_t idle = ramAddress("EENULL");
        const uint32_t loader = ramAddress("EELOAD");
        image.code = ElfFile::fromPieces(name + " (" + image.id + ")", idle, {piece(file("EENULL"), idle), piece(file("EELOAD"), loader)});
        image.entries = {idle, loader};
        b.images.push_back(std::move(image));
    }
    return b;
}

Bios readBios(const std::filesystem::path& rom) { return readBios(readFile(rom), rom.filename().string()); }

} // namespace ps2n::recomp
