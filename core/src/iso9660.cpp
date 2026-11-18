// SPDX-License-Identifier: GPL-3.0-or-later
// See ps2n/iso9660.h. The layout is ECMA-119's: the volume descriptors from
// sector 16 on, the primary one holding the root directory's record; a
// directory is a run of records, none of which crosses into the next sector.
// No exceptions: the runtime compiles this too (runtime/targets.cmake).
#include "ps2n/iso9660.h"

#include "ps2n/util.h"

#include <algorithm>
#include <fstream>
#include <memory>

namespace ps2n::iso {

namespace {

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool readSector(const SectorReader& read, uint32_t sector, uint8_t* out, std::string* error) {
    if (read(sector, out)) return true;
    *error = strprintf("the disc image ends before sector %u", sector);
    return false;
}

// A directory record (ECMA-119 9.1), `length` bytes of it.
std::optional<Entry> entryOf(const uint8_t* record, size_t length, std::string* error) {
    if (length < 34) {
        *error = "a directory record of the disc image is too short";
        return std::nullopt;
    }
    const size_t nameLength = record[32];
    if (33 + nameLength > length) {
        *error = "a directory record of the disc image is too short for its name";
        return std::nullopt;
    }
    Entry e;
    e.sector = le32(record + 2);
    e.size = le32(record + 10);
    e.directory = (record[25] & 2) != 0;
    e.name.assign(reinterpret_cast<const char*>(record + 33), nameLength);
    if (nameLength == 1 && (record[33] == 0 || record[33] == 1)) e.name = record[33] == 0 ? "." : "..";
    const size_t version = e.name.find(';');
    if (version != std::string::npos) e.name.resize(version);
    // A file without an extension is recorded with its dot ("NAME.;1").
    if (!e.directory && !e.name.empty() && e.name.back() == '.') e.name.pop_back();
    return e;
}

} // namespace

SectorReader openImage(const std::filesystem::path& path) {
    auto file = std::make_shared<std::ifstream>(path, std::ios::binary);
    if (!*file) return {};
    return [file](uint32_t sector, uint8_t* out) {
        file->clear();
        file->seekg(static_cast<std::streamoff>(sector) * kSectorSize, std::ios::beg);
        return static_cast<bool>(file->read(reinterpret_cast<char*>(out), kSectorSize));
    };
}

std::optional<Entry> root(const SectorReader& read, std::string* error) {
    uint8_t sector[kSectorSize];
    for (uint32_t at = 16; at < 16 + 64; ++at) {
        if (!readSector(read, at, sector, error)) return std::nullopt;
        if (std::string_view(reinterpret_cast<const char*>(sector + 1), 5) != "CD001") {
            *error = "the disc image holds no ISO 9660 file system (no volume descriptor at sector 16)";
            return std::nullopt;
        }
        if (sector[0] == 255) break; // the set's terminator
        if (sector[0] != 1) continue; // not the primary volume descriptor
        const uint32_t blockSize = static_cast<uint32_t>(sector[128]) | (static_cast<uint32_t>(sector[129]) << 8);
        if (blockSize != kSectorSize) {
            *error = strprintf("the disc image's file system has blocks of %u bytes, not 2048", blockSize);
            return std::nullopt;
        }
        std::optional<Entry> r = entryOf(sector + 156, 34, error);
        if (!r) return std::nullopt;
        r->name.clear();
        r->directory = true;
        return r;
    }
    *error = "the disc image's ISO 9660 file system has no primary volume descriptor";
    return std::nullopt;
}

std::optional<std::vector<Entry>> list(const SectorReader& read, const Entry& directory, std::string* error) {
    if (!directory.directory) {
        *error = "'" + directory.name + "' is not a directory";
        return std::nullopt;
    }
    std::vector<Entry> entries;
    uint8_t sector[kSectorSize];
    const uint32_t sectors = (directory.size + kSectorSize - 1) / kSectorSize;
    for (uint32_t s = 0; s < sectors; ++s) {
        if (!readSector(read, directory.sector + s, sector, error)) return std::nullopt;
        size_t at = 0;
        while (at < kSectorSize) {
            const size_t length = sector[at];
            if (length == 0) break; // the rest of the sector is padding
            if (at + length > kSectorSize) {
                *error = "a directory record of the disc image crosses a sector's end";
                return std::nullopt;
            }
            std::optional<Entry> e = entryOf(sector + at, length, error);
            if (!e) return std::nullopt;
            if (e->name != "." && e->name != "..") entries.push_back(std::move(*e));
            at += length;
        }
    }
    return entries;
}

std::optional<Entry> find(const SectorReader& read, std::string_view path, std::string* error) {
    error->clear();
    std::optional<Entry> at = root(read, error);
    if (!at) return std::nullopt;
    std::string rest(path);
    for (char& c : rest)
        if (c == '\\') c = '/';
    for (const std::string& part : split(rest, '/')) {
        if (part.empty()) continue;
        if (!at->directory) return std::nullopt;
        const std::optional<std::vector<Entry>> entries = list(read, *at, error);
        if (!entries) return std::nullopt;
        const std::string wanted = toUpper(part);
        bool found = false;
        for (const Entry& e : *entries) {
            if (toUpper(e.name) == wanted) {
                at = e;
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    return at;
}

std::optional<std::vector<uint8_t>> readFile(const SectorReader& read, const Entry& file, std::string* error) {
    if (file.directory) {
        *error = "'" + file.name + "' is a directory";
        return std::nullopt;
    }
    std::vector<uint8_t> data(file.size);
    uint8_t sector[kSectorSize];
    for (uint32_t done = 0, s = 0; done < file.size; done += kSectorSize, ++s) {
        if (!readSector(read, file.sector + s, sector, error)) return std::nullopt;
        const uint32_t n = file.size - done < kSectorSize ? file.size - done : kSectorSize;
        std::copy(sector, sector + n, data.begin() + done);
    }
    return data;
}

std::optional<std::string> bootExecutable(const SectorReader& read, std::string* error) {
    const std::optional<Entry> cnf = find(read, "SYSTEM.CNF", error);
    if (!cnf || cnf->directory) return std::nullopt;
    const std::optional<std::vector<uint8_t>> bytes = readFile(read, *cnf, error);
    if (!bytes) return std::nullopt;
    const std::string text(bytes->begin(), bytes->end());
    for (const std::string& line : split(text, '\n')) {
        const std::string_view l = trim(line);
        if (!startsWith(toUpper(l), "BOOT2")) continue;
        const size_t equals = l.find('=');
        if (equals == std::string_view::npos) continue;
        std::string_view value = trim(l.substr(equals + 1));
        // "cdrom0:\SLES_505.39;1": the device, then the path, then the version.
        const size_t colon = value.find(':');
        if (colon != std::string_view::npos) value = value.substr(colon + 1);
        const size_t version = value.find(';');
        if (version != std::string_view::npos) value = value.substr(0, version);
        std::string p(value);
        for (char& c : p)
            if (c == '\\') c = '/';
        while (!p.empty() && p.front() == '/') p.erase(p.begin());
        if (!p.empty()) return p;
    }
    return std::nullopt;
}

namespace {

// An ELF header of a program of the EE's: 32 bits, little-endian, an executable
// (ET_EXEC) for MIPS. An IOP module has the type 0xFF80, and is not one.
bool isEeExecutable(const uint8_t* h) {
    if (h[0] != 0x7F || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' || h[4] != 1 || h[5] != 1) return false;
    const uint32_t type = h[16] | (static_cast<uint32_t>(h[17]) << 8);
    const uint32_t machine = h[18] | (static_cast<uint32_t>(h[19]) << 8);
    return type == 2 && machine == 8;
}

// Every file's first sector, directory after directory; a disc's own nesting
// goes nowhere near the limit, which only keeps a broken image from looping.
bool collectExecutables(const SectorReader& read, const Entry& directory, const std::string& prefix, int depth,
                        std::vector<std::string>* out, std::string* error) {
    if (depth > 32) return true;
    const std::optional<std::vector<Entry>> entries = list(read, directory, error);
    if (!entries) return false;
    uint8_t first[kSectorSize];
    for (const Entry& e : *entries) {
        const std::string path = prefix.empty() ? e.name : prefix + "/" + e.name;
        if (e.directory) {
            if (!collectExecutables(read, e, path, depth + 1, out, error)) return false;
        } else if (e.size >= 52 && read(e.sector, first) && isEeExecutable(first)) {
            out->push_back(path);
        }
    }
    return true;
}

} // namespace

std::optional<std::vector<std::string>> executables(const SectorReader& read, std::string* error) {
    error->clear();
    const std::optional<Entry> top = root(read, error);
    if (!top) return std::nullopt;
    std::vector<std::string> found;
    if (!collectExecutables(read, *top, "", 0, &found, error)) return std::nullopt;
    const std::optional<std::string> boot = bootExecutable(read, error);
    if (!boot && !error->empty()) return std::nullopt;
    std::vector<std::string> out;
    if (boot) out.push_back(*boot);
    for (const std::string& path : found) {
        if (!boot || toUpper(path) != toUpper(*boot)) out.push_back(path);
    }
    return out;
}

} // namespace ps2n::iso
