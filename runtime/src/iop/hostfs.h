// PS2 file devices (host0:, cdrom0:, mc0:, rom0:, tty:), shared by the
// FILEIO service the EE calls and the ioman calls IOP modules make, so both
// see one table of open files.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2::iop::hostfs {

// PS2 open flags (ioman / FILEIO).
constexpr int O_RDONLY = 0x0001, O_WRONLY = 0x0002, O_RDWR = 0x0003, O_NBLOCK = 0x0010, O_APPEND = 0x0100,
              O_CREAT = 0x0200, O_TRUNC = 0x0400;
// Results are >= 0 on success, or a negative errno.
constexpr int E_NOENT = -2, E_IO = -5, E_BADF = -9, E_ACCES = -13, E_EXIST = -17, E_NODEV = -19, E_INVAL = -22, E_MFILE = -24;

struct Stat {
    bool dir = false;
    uint64_t size = 0;
    int64_t mtime = 0;   // seconds since the Unix epoch
};

// Host path behind a PS2 path, or "" when the device has no host backing.
// Lookup is case-insensitive per component and drops ISO 9660 ";1" suffixes.
std::string hostPath(const std::string& ps2Path);
bool readWholeFile(const std::string& ps2Path, std::vector<uint8_t>& out);

int open(const std::string& ps2Path, int flags);
int close(int fd);
int read(int fd, void* dst, int size);
int write(int fd, const void* src, int size);
int64_t lseek(int fd, int64_t offset, int whence);
int remove(const std::string& ps2Path);
int mkdir(const std::string& ps2Path);
int rmdir(const std::string& ps2Path);
int rename(const std::string& from, const std::string& to);
int dopen(const std::string& ps2Path);
int dread(int fd, std::string& name, Stat& st);   // 1: an entry, 0: end
int getstat(const std::string& ps2Path, Stat& st);

void reset();   // closes everything (IOP reboot)

} // namespace ps2::iop::hostfs
