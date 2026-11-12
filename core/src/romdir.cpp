// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps2n/romdir.h"

#include <cstring>

namespace ps2n::bios {

namespace {

constexpr size_t kEntrySize = 16;  // char name[10]; u16 size of its EXTINFO part; u32 size of the file
constexpr size_t kNameSize = 10;

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// The entry's name, or nothing when it does not end within its ten bytes.
std::optional<std::string> nameOf(const uint8_t* entry) {
    const void* end = std::memchr(entry, 0, kNameSize);
    if (!end) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(entry), static_cast<size_t>(static_cast<const uint8_t*>(end) - entry));
}

// The files of a directory taken to begin at `at`; none when a file would
// lie past the end of the image.
std::vector<RomFile> readFrom(const uint8_t* image, size_t size, size_t at) {
    std::vector<RomFile> files;
    uint64_t offset = 0;
    for (; at + kEntrySize <= size; at += kEntrySize) {
        const uint8_t* entry = image + at;
        const std::optional<std::string> name = nameOf(entry);
        if (!name || name->empty()) break;  // the end of the directory
        const uint32_t fileSize = le32(entry + 12);
        if (offset + fileSize > size) return {};
        files.push_back({*name, static_cast<uint32_t>(offset), fileSize});
        offset += (static_cast<uint64_t>(fileSize) + 15) & ~uint64_t{15};
    }
    return files;
}

} // namespace

std::vector<RomFile> readRomDirectory(const uint8_t* image, size_t size) {
    if (!image) return {};
    for (size_t at = 0; at + kEntrySize <= size; at += kEntrySize) {
        if (std::memcmp(image + at, "RESET", 6) != 0) continue;
        // The directory describes itself: its own entry, ROMDIR, says where it
        // is. "RESET" anywhere else (in the reset code's own bytes) is not its
        // first entry.
        std::vector<RomFile> files = readFrom(image, size, at);
        const std::optional<RomFile> self = findRomFile(files, "ROMDIR");
        if (self && self->offset == at) return files;
    }
    return {};
}

std::optional<RomFile> findRomFile(const std::vector<RomFile>& files, std::string_view name) {
    for (const RomFile& f : files)
        if (f.name == name) return f;
    return std::nullopt;
}

} // namespace ps2n::bios
