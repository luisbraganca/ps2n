#include "iop/hostfs.h"

#include "internal.h"
#include "iop/disc.h"
#include "log.h"
#include "stubs/format.h"

#include "ps2uc/util.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace ps2::iop::hostfs {

namespace fs = std::filesystem;
using ps2uc::split;
using ps2uc::startsWith;
using ps2uc::toLower;
using ps2uc::toUpper;

namespace {

struct Handle {
    FILE* fp = nullptr;   // a host file
    bool console = false;
    bool dir = false;
    std::vector<std::pair<std::string, Stat>> entries;   // directory listing
    size_t next = 0;
    // A file on a disc image (onDisc), or one held in memory (rom0:).
    bool image = false, onDisc = false;
    uint32_t lsn = 0;
    std::vector<uint8_t> data;
    uint64_t size = 0, pos = 0;
};
std::map<int, Handle> g_handles;
int g_nextFd = 3;

// A PS2 path split into its device and the path on it ('/'-separated, no
// leading '/', ISO 9660 version kept). Unit numbers are dropped from device
// names ("host0", "cdrom0", "rom0"), except for the two memory cards.
struct DevicePath {
    std::string dev, path;
};

DevicePath splitPath(const std::string& ps2Path) {
    DevicePath d;
    d.path = ps2Path;
    if (auto c = d.path.find(':'); c != std::string::npos) {
        d.dev = toLower(d.path.substr(0, c));
        d.path = d.path.substr(c + 1);
    }
    for (char& ch : d.path)
        if (ch == '\\') ch = '/';
    while (!d.path.empty() && d.path.front() == '/') d.path.erase(d.path.begin());
    while (!d.dev.empty() && std::isdigit(static_cast<unsigned char>(d.dev.back())) && d.dev != "mc0" && d.dev != "mc1")
        d.dev.pop_back();
    return d;
}

bool isCdrom(const DevicePath& d) { return d.dev == "cdrom" || d.dev == "cdfs"; }

// cdrom0: reads the disc image when the disc is one (a directory disc is
// read through the host file system directly).
bool onDiscImage(const DevicePath& d) {
    std::error_code ec;
    return isCdrom(d) && disc::type() != disc::Type::None && !fs::is_directory(g_opt.cdrom, ec);
}

// Files the BIOS ROM provides.
bool romFile(const DevicePath& d, std::vector<uint8_t>& out) {
    if (d.dev != "rom" || toUpper(d.path) != "ROMVER") return false;
    const std::string_view v = romVersion();
    out.assign(v.begin(), v.end());
    out.push_back(0);
    return true;
}

// Seconds since the Unix epoch for an ISO 9660 recording date (UTC).
int64_t isoTime(const uint8_t d[7]) {
    const int64_t y = 1900 + d[0] - (d[1] <= 2 ? 1 : 0);
    const int64_t m = d[1] ? d[1] : 1;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (d[2] ? d[2] : 1) - 1;
    const int64_t days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return days * 86400 + d[3] * 3600 + d[4] * 60 + d[5] - static_cast<int8_t>(d[6]) * 15 * 60;
}

Stat discStat(const disc::Entry& e) {
    Stat st;
    st.dir = e.dir;
    st.size = e.dir ? 0 : e.size;
    st.mtime = isoTime(e.date);
    return st;
}

std::string withoutVersion(const std::string& name) { return name.substr(0, name.find(';')); }

// Bytes [offset, offset + n) of the file whose data starts at sector `lsn`.
bool readDisc(uint32_t lsn, uint64_t offset, uint64_t n, uint8_t* dst) {
    uint8_t sector[2048];
    while (n) {
        const uint32_t s = lsn + static_cast<uint32_t>(offset / 2048);
        const uint32_t in = static_cast<uint32_t>(offset % 2048);
        if (in == 0 && n >= 2048) {
            const uint32_t count = static_cast<uint32_t>(n / 2048);
            if (!disc::read(s, count, dst)) return false;
            offset += count * 2048ull;
            dst += count * 2048ull;
            n -= count * 2048ull;
            continue;
        }
        if (!disc::read(s, 1, sector)) return false;
        const uint64_t chunk = std::min<uint64_t>(n, 2048 - in);
        std::memcpy(dst, sector + in, chunk);
        offset += chunk;
        dst += chunk;
        n -= chunk;
    }
    return true;
}

// Case-insensitive path resolution (ISO 9660 names are upper case; host
// copies of disc files often are not).
fs::path resolveCi(const fs::path& base, const std::string& rel) {
    fs::path cur = base;
    for (const auto& part : split(rel, '/')) {
        if (part.empty() || part == ".") continue;
        fs::path next = cur / part;
        std::error_code ec;
        if (!fs::exists(next, ec) && fs::is_directory(cur, ec)) {
            for (const auto& entry : fs::directory_iterator(cur, ec)) {
                if (toLower(entry.path().filename().string()) == toLower(part)) {
                    next = entry.path();
                    break;
                }
            }
        }
        cur = next;
    }
    return cur;
}

bool isConsolePath(const std::string& p) {
    const std::string l = toLower(p);
    return startsWith(l, "tty:") || startsWith(l, "tty0:");
}

int addHandle(Handle h) {
    const int fd = g_nextFd++;
    g_handles[fd] = std::move(h);
    return fd;
}

Stat statOf(const fs::path& p) {
    Stat st;
    std::error_code ec;
    st.dir = fs::is_directory(p, ec);
    if (!st.dir) st.size = static_cast<uint64_t>(fs::file_size(p, ec));
    const auto t = fs::last_write_time(p, ec);
    if (!ec) {
        const auto sys = std::chrono::system_clock::now() + (t - fs::file_time_type::clock::now());
        st.mtime = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
    }
    return st;
}

} // namespace

std::string hostPath(const std::string& ps2Path) {
    DevicePath d = splitPath(ps2Path);
    if (auto semi = d.path.rfind(';'); semi != std::string::npos) d.path = d.path.substr(0, semi);
    if (d.dev.empty() || d.dev == "host") return resolveCi(g_opt.host0Dir, d.path).string();
    if (isCdrom(d)) {
        std::error_code ec;
        if (g_opt.cdrom.empty() || !fs::is_directory(g_opt.cdrom, ec)) return {};
        return resolveCi(g_opt.cdrom, d.path).string();
    }
    if (d.dev == "mc0" || d.dev == "mc1") return resolveCi(g_opt.memcardDir / d.dev, d.path).string();
    return {};
}

bool readWholeFile(const std::string& ps2Path, std::vector<uint8_t>& out) {
    const DevicePath d = splitPath(ps2Path);
    if (romFile(d, out)) return true;
    if (onDiscImage(d)) {
        disc::Entry e;
        if (!disc::find(d.path, e) || e.dir) return false;
        out.resize(e.size);
        return readDisc(e.lsn, 0, e.size, out.data());
    }
    const std::string host = hostPath(ps2Path);
    std::error_code ec;
    if (host.empty() || !fs::is_regular_file(host, ec)) return false;
    std::ifstream in(host, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

int open(const std::string& ps2Path, int flags) {
    Handle h;
    if (isConsolePath(ps2Path)) {
        h.console = true;
        return addHandle(std::move(h));
    }
    const DevicePath d = splitPath(ps2Path);
    if (romFile(d, h.data) || onDiscImage(d)) {
        if ((flags & 3) != O_RDONLY) return E_ACCES;
        if (h.data.empty()) {
            disc::Entry e;
            if (!disc::find(d.path, e)) return E_NOENT;
            if (e.dir) return E_ACCES;
            h.onDisc = true;
            h.lsn = e.lsn;
            h.size = e.size;
        } else {
            h.size = h.data.size();
        }
        h.image = true;
        return addHandle(std::move(h));
    }
    const std::string host = hostPath(ps2Path);
    if (host.empty()) {
        PS2_WARN("hostfs", "open(\"%s\"): no host directory backs this device", ps2Path.c_str());
        return E_NODEV;
    }
    if (isCdrom(d) && (flags & 3) != O_RDONLY) return E_ACCES;
    std::error_code ec;
    const bool exists = fs::exists(host, ec);
    if (exists && fs::is_directory(host, ec)) return E_ACCES;
    const int acc = flags & 3;
    std::string mode;
    if (acc == O_RDONLY) mode = "rb";
    else if (flags & O_APPEND) mode = acc == O_RDWR ? "a+b" : "ab";
    else if ((flags & O_TRUNC) || ((flags & O_CREAT) && !exists)) mode = acc == O_RDWR ? "w+b" : "wb";
    else mode = "r+b";
    if (acc != O_RDONLY) fs::create_directories(fs::path(host).parent_path(), ec);
    h.fp = std::fopen(host.c_str(), mode.c_str());
    if (!h.fp) {
        PS2_DEBUG("hostfs", "open(\"%s\" -> %s, 0x%x) failed", ps2Path.c_str(), host.c_str(), flags);
        return E_NOENT;
    }
    const int fd = addHandle(std::move(h));
    PS2_DEBUG("hostfs", "open(\"%s\", 0x%x) = %d", ps2Path.c_str(), flags, fd);
    return fd;
}

int close(int fd) {
    auto it = g_handles.find(fd);
    if (it == g_handles.end()) return E_BADF;
    if (it->second.fp) std::fclose(it->second.fp);
    g_handles.erase(it);
    return 0;
}

int read(int fd, void* dst, int size) {
    auto it = g_handles.find(fd);
    if (it == g_handles.end() || size < 0) return E_BADF;
    Handle& h = it->second;
    if (h.image) {
        const uint64_t n = std::min<uint64_t>(static_cast<uint64_t>(size), h.size - std::min(h.pos, h.size));
        auto* out = static_cast<uint8_t*>(dst);
        if (!h.onDisc) std::memcpy(out, h.data.data() + h.pos, n);
        else if (!readDisc(h.lsn, h.pos, n, out)) return E_IO;
        h.pos += n;
        return static_cast<int>(n);
    }
    if (!h.fp) return h.console ? 0 : E_BADF;
    return static_cast<int>(std::fread(dst, 1, static_cast<size_t>(size), h.fp));
}

int write(int fd, const void* src, int size) {
    if (size < 0) return E_INVAL;
    const auto* p = static_cast<const char*>(src);
    auto it = g_handles.find(fd);
    if (fd == 1 || fd == 2 || (it != g_handles.end() && it->second.console)) {
        hostPrint(std::string(p, static_cast<size_t>(size)));
        return size;
    }
    if (it != g_handles.end() && it->second.image) return E_ACCES;
    if (it == g_handles.end() || !it->second.fp) return E_BADF;
    return static_cast<int>(std::fwrite(p, 1, static_cast<size_t>(size), it->second.fp));
}

int64_t lseek(int fd, int64_t offset, int whence) {
    auto it = g_handles.find(fd);
    if (it == g_handles.end()) return E_BADF;
    Handle& h = it->second;
    if (h.image) {
        const int64_t base = whence == 1 ? static_cast<int64_t>(h.pos) : whence == 2 ? static_cast<int64_t>(h.size) : 0;
        if (base + offset < 0) return E_INVAL;
        h.pos = static_cast<uint64_t>(base + offset);
        return static_cast<int64_t>(h.pos);
    }
    if (!h.fp) return E_BADF;
    const int w = whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : SEEK_SET;
    if (std::fseek(h.fp, static_cast<long>(offset), w) != 0) return E_INVAL;
    return std::ftell(h.fp);
}

int remove(const std::string& ps2Path) {
    if (isCdrom(splitPath(ps2Path))) return E_ACCES;
    const std::string host = hostPath(ps2Path);
    std::error_code ec;
    if (host.empty()) return E_NODEV;
    return fs::is_regular_file(host, ec) && fs::remove(host, ec) ? 0 : E_NOENT;
}

int mkdir(const std::string& ps2Path) {
    if (isCdrom(splitPath(ps2Path))) return E_ACCES;
    const std::string host = hostPath(ps2Path);
    std::error_code ec;
    if (host.empty()) return E_NODEV;
    if (fs::exists(host, ec)) return E_EXIST;
    fs::create_directories(host, ec);
    return ec ? E_IO : 0;
}

int rmdir(const std::string& ps2Path) {
    if (isCdrom(splitPath(ps2Path))) return E_ACCES;
    const std::string host = hostPath(ps2Path);
    std::error_code ec;
    if (host.empty()) return E_NODEV;
    return fs::is_directory(host, ec) && fs::remove(host, ec) ? 0 : E_NOENT;
}

int rename(const std::string& from, const std::string& to) {
    if (isCdrom(splitPath(from)) || isCdrom(splitPath(to))) return E_ACCES;
    const std::string a = hostPath(from), b = hostPath(to);
    if (a.empty() || b.empty()) return E_NODEV;
    std::error_code ec;
    fs::rename(a, b, ec);
    return ec ? E_NOENT : 0;
}

int dopen(const std::string& ps2Path) {
    Handle h;
    h.dir = true;
    const DevicePath d = splitPath(ps2Path);
    if (onDiscImage(d)) {
        std::vector<disc::Entry> entries;
        if (!disc::list(d.path, entries)) return E_NOENT;
        for (const disc::Entry& e : entries) h.entries.emplace_back(withoutVersion(e.name), discStat(e));
        return addHandle(std::move(h));
    }
    const std::string host = hostPath(ps2Path);
    std::error_code ec;
    if (host.empty()) return E_NODEV;
    if (!fs::is_directory(host, ec)) return E_NOENT;
    std::vector<fs::path> paths;
    for (const auto& e : fs::directory_iterator(host, ec)) paths.push_back(e.path());
    std::sort(paths.begin(), paths.end());
    for (const fs::path& p : paths) h.entries.emplace_back(p.filename().string(), statOf(p));
    return addHandle(std::move(h));
}

int dread(int fd, std::string& name, Stat& st) {
    auto it = g_handles.find(fd);
    if (it == g_handles.end() || !it->second.dir) return E_BADF;
    Handle& h = it->second;
    if (h.next >= h.entries.size()) return 0;
    name = h.entries[h.next].first;
    st = h.entries[h.next].second;
    ++h.next;
    return 1;
}

int getstat(const std::string& ps2Path, Stat& st) {
    const DevicePath d = splitPath(ps2Path);
    std::vector<uint8_t> rom;
    if (romFile(d, rom)) {
        st = Stat{};
        st.size = rom.size();
        return 0;
    }
    if (onDiscImage(d)) {
        disc::Entry e;
        if (!disc::find(d.path, e)) return E_NOENT;
        st = discStat(e);
        return 0;
    }
    const std::string host = hostPath(ps2Path);
    std::error_code ec;
    if (host.empty()) return E_NODEV;
    if (!fs::exists(host, ec)) return E_NOENT;
    st = statOf(host);
    return 0;
}

void reset() {
    for (auto& [fd, h] : g_handles)
        if (h.fp) std::fclose(h.fp);
    g_handles.clear();
    g_nextFd = 3;
}

} // namespace ps2::iop::hostfs
