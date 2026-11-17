// SPDX-License-Identifier: GPL-3.0-or-later
// See ps2n/iso9660.h. The layout is ECMA-119's: the volume descriptors from
// sector 16 on, the primary one holding the root directory's record; a
// directory is a run of records, none of which crosses into the next sector.
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

void readSector(const SectorReader& read, uint32_t sector, uint8_t* out) {
    if (!read(sector, out)) throw Error(strprintf("the disc image ends before sector %u", sector));
}

// A directory record (ECMA-119 9.1), `length` bytes of it.
Entry entryOf(const uint8_t* record, size_t length) {
    if (length < 34) throw Error("a directory record of the disc image is too short");
    const size_t nameLength = record[32];
    if (33 + nameLength > length) throw Error("a directory record of the disc image is too short for its name");
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
    if (!*file) throw Error("cannot open the disc image " + path.string());
    return [file](uint32_t sector, uint8_t* out) {
        file->clear();
        file->seekg(static_cast<std::streamoff>(sector) * kSectorSize, std::ios::beg);
        return static_cast<bool>(file->read(reinterpret_cast<char*>(out), kSectorSize));
    };
}

Entry root(const SectorReader& read) {
    uint8_t sector[kSectorSize];
    for (uint32_t at = 16; at < 16 + 64; ++at) {
        readSector(read, at, sector);
        if (std::string_view(reinterpret_cast<const char*>(sector + 1), 5) != "CD001")
            throw Error("the disc image holds no ISO 9660 file system (no volume descriptor at sector 16)");
        if (sector[0] == 255) break; // the set's terminator
        if (sector[0] != 1) continue; // not the primary volume descriptor
        const uint32_t blockSize = static_cast<uint32_t>(sector[128]) | (static_cast<uint32_t>(sector[129]) << 8);
        if (blockSize != kSectorSize)
            throw Error(strprintf("the disc image's file system has blocks of %u bytes, not 2048", blockSize));
        Entry r = entryOf(sector + 156, 34);
        r.name.clear();
        r.directory = true;
        return r;
    }
    throw Error("the disc image's ISO 9660 file system has no primary volume descriptor");
}

std::vector<Entry> list(const SectorReader& read, const Entry& directory) {
    if (!directory.directory) throw Error("'" + directory.name + "' is not a directory");
    std::vector<Entry> entries;
    uint8_t sector[kSectorSize];
    const uint32_t sectors = (directory.size + kSectorSize - 1) / kSectorSize;
    for (uint32_t s = 0; s < sectors; ++s) {
        readSector(read, directory.sector + s, sector);
        size_t at = 0;
        while (at < kSectorSize) {
            const size_t length = sector[at];
            if (length == 0) break; // the rest of the sector is padding
            if (at + length > kSectorSize) throw Error("a directory record of the disc image crosses a sector's end");
            Entry e = entryOf(sector + at, length);
            if (e.name != "." && e.name != "..") entries.push_back(std::move(e));
            at += length;
        }
    }
    return entries;
}

std::optional<Entry> find(const SectorReader& read, std::string_view path) {
    Entry at = root(read);
    std::string rest(path);
    for (char& c : rest)
        if (c == '\\') c = '/';
    for (const std::string& part : split(rest, '/')) {
        if (part.empty()) continue;
        if (!at.directory) return std::nullopt;
        const std::string wanted = toUpper(part);
        bool found = false;
        for (const Entry& e : list(read, at)) {
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

std::vector<uint8_t> readFile(const SectorReader& read, const Entry& file) {
    if (file.directory) throw Error("'" + file.name + "' is a directory");
    std::vector<uint8_t> data(file.size);
    uint8_t sector[kSectorSize];
    for (uint32_t done = 0, s = 0; done < file.size; done += kSectorSize, ++s) {
        readSector(read, file.sector + s, sector);
        const uint32_t n = file.size - done < kSectorSize ? file.size - done : kSectorSize;
        std::copy(sector, sector + n, data.begin() + done);
    }
    return data;
}

std::optional<std::string> bootExecutable(const SectorReader& read) {
    const std::optional<Entry> cnf = find(read, "SYSTEM.CNF");
    if (!cnf || cnf->directory) return std::nullopt;
    const std::vector<uint8_t> bytes = readFile(read, *cnf);
    const std::string text(bytes.begin(), bytes.end());
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

} // namespace ps2n::iso
