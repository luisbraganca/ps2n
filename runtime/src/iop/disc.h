// The disc in the drive: what cdrom0: and the CD/DVD services read.
//
// The disc is an ISO 9660 image (2048-byte sectors, or raw 2352-byte MODE1 /
// MODE2 sectors as in .bin dumps), or a host directory. A directory is
// presented as an ISO 9660 volume built in memory (volume descriptors, path
// tables, directory records), with file sectors read from the host files, so
// code that reads sectors and parses the file system itself works too.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps2::iop::disc {

enum class Type { None, Cd, Dvd };

// A file or directory recorded on the disc.
struct Entry {
    std::string name;   // as recorded: "SLUS_123.45;1", "DATA"
    uint32_t lsn = 0;
    uint32_t size = 0;
    bool dir = false;
    // ISO 9660 recording date: years since 1900, month, day, hour, minute,
    // second, offset from GMT in 15-minute units.
    uint8_t date[7] = {};
};

// Inserts the disc at `path` (an image file or a directory); an empty path
// leaves the drive empty. Returns false, with a warning, when the path is
// neither a readable ISO 9660 image nor a directory.
bool insert(const std::filesystem::path& path);
void eject();
Type type();
uint32_t sectorCount();
// Reads `count` 2048-byte sectors of user data from `lsn` on. False when the
// range is past the end of the disc or the drive is empty.
bool read(uint32_t lsn, uint32_t count, uint8_t* dst);
// Looks up a path ("\\DIR\\FILE.BIN;1", "dir/file.bin"): case-insensitive,
// the ";1" version is optional, either slash works.
bool find(const std::string& path, Entry& out);
// The entries of a directory, without "." and "..".
bool list(const std::string& dirPath, std::vector<Entry>& out);

// The ISO 9660 volume built for a directory, as one byte image (tests).
std::vector<uint8_t> buildImage(const std::filesystem::path& dir);

} // namespace ps2::iop::disc
