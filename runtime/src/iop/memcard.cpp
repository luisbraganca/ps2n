// MCSERV / XMCSERV over host directories.
//
// One server answers both protocols: the old MCSERV function numbers
// (0x70-0x80) and the XMCSERV ones (0x01-0x33, 0xFE), as XMCSERV itself does.
// Requests are a 48-byte descriptor (fd, port, slot, size, offset, origin, EE
// buffer, EE parameter block, 16 inline bytes) or a 1044-byte name block
// (port, slot, flags, max entries, EE table address, path); every reply is
// the 4-byte result, except the XMCSERV version query.
#include "iop/memcard.h"

#include "internal.h"
#include "iop/iop.h"
#include "log.h"
#include "ps2/memory.h"

#include "ps2n/util.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ps2::iop::memcard {

namespace fs = std::filesystem;

namespace {

// sceMcRes* results.
constexpr int32_t kOk = 0, kChangedCard = -1, kNoEntry = -4, kDenied = -5, kNotEmpty = -6, kNoHandle = -7, kNoCard = -10;
constexpr uint16_t kAttrDir = 0x8427, kAttrFile = 0x8497;
constexpr uint32_t kClusters = 8001;   // allocatable 1 KB clusters on an 8 MB card
constexpr int kMaxFiles = 3;
constexpr int kOpenRead = 0x01, kOpenWrite = 0x02, kOpenMkdir = 0x40, kOpenCreate = 0x200;

struct Card {
    bool reported = false;   // GetInfo has reported this card as newly inserted
    std::string cwd = "/";
};

struct OpenFile {
    FILE* fp = nullptr;
    fs::path host;
    bool dir = false, read = false, write = false;
};

struct Listed {
    std::string name;
    fs::path host;
    bool dir = false;
};

Card g_cards[2];
std::array<std::optional<OpenFile>, kMaxFiles> g_files;
// mcGetDir continues where the previous call stopped; one cursor for all
// ports, as in the real module.
std::vector<Listed> g_listing;
size_t g_listingNext = 0;
bool g_listingValid = false;

int32_t i32(const uint8_t* p, size_t off) {
    int32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
uint32_t u32(const uint8_t* p, size_t off) { return static_cast<uint32_t>(i32(p, off)); }
std::string cstr(const uint8_t* p, size_t off, size_t max) {
    std::string s;
    for (size_t i = 0; i < max && p[off + i]; ++i) s.push_back(static_cast<char>(p[off + i]));
    return s;
}

// The 48-byte descriptor.
struct Desc {
    int32_t fd, port, slot, size, offset, origin;
    uint32_t buffer, param;
    const uint8_t* data;
};
Desc desc(const uint8_t* d) {
    return Desc{i32(d, 0), i32(d, 4), i32(d, 8), i32(d, 12), i32(d, 16), i32(d, 20), u32(d, 24), u32(d, 28), d + 32};
}
// The 1044-byte name block.
struct Name {
    int32_t port, slot, flags, maxent;
    uint32_t table;
    std::string path;
};
Name nameBlock(const uint8_t* d) { return Name{i32(d, 0), i32(d, 4), i32(d, 8), i32(d, 12), u32(d, 16), cstr(d, 20, 1024)}; }

fs::path cardRoot(int port) { return g_opt.memcardDir / (port ? "mc1" : "mc0"); }

bool present(int port, int slot) {
    if (port < 0 || port > 1 || slot != 0) return false;
    std::error_code ec;
    return port == 0 || fs::is_directory(cardRoot(1), ec);
}

// A card path made absolute against the current directory, with "." and ".."
// resolved ("/.." stays at the root). Names are at most 32 characters.
std::vector<std::string> resolve(int port, const std::string& path) {
    const std::string full = !path.empty() && path[0] == '/' ? path : g_cards[port].cwd + "/" + path;
    std::vector<std::string> parts;
    for (const std::string& p : ps2n::split(full, '/')) {
        if (p.empty() || p == ".") continue;
        if (p == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(p.substr(0, 32));
    }
    return parts;
}

fs::path hostOf(int port, const std::vector<std::string>& parts) {
    fs::path p = cardRoot(port);
    for (const std::string& s : parts) p /= s;
    return p;
}

std::string cardPathOf(const std::vector<std::string>& parts) {
    std::string s;
    for (const std::string& p : parts) s += "/" + p;
    return s.empty() ? "/" : s;
}

bool isDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}
bool pathExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

std::vector<fs::path> children(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

// Clusters in use: a directory takes one per two entries (counting "." and
// ".."), a file one per KB.
uint32_t usedClusters(const fs::path& dir) {
    const std::vector<fs::path> entries = children(dir);
    uint32_t used = static_cast<uint32_t>(entries.size() + 2 + 1) / 2;
    for (const fs::path& e : entries) {
        std::error_code ec;
        if (fs::is_directory(e, ec)) used += usedClusters(e);
        else used += static_cast<uint32_t>((fs::file_size(e, ec) + 1023) / 1024);
    }
    return used;
}

// sceMcStDateTime (8 bytes, Japan time): reserved, second, minute, hour, day, month, year (u16).
void putDateTime(uint8_t* out, const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    std::time_t tt = 0;
    if (!ec) {
        const auto sys = std::chrono::system_clock::now() + (t - fs::file_time_type::clock::now());
        tt = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(sys));
    }
    tt += 9 * 3600;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    out[0] = 0;
    out[1] = static_cast<uint8_t>(tm.tm_sec);
    out[2] = static_cast<uint8_t>(tm.tm_min);
    out[3] = static_cast<uint8_t>(tm.tm_hour);
    out[4] = static_cast<uint8_t>(tm.tm_mday);
    out[5] = static_cast<uint8_t>(tm.tm_mon + 1);
    const auto year = static_cast<uint16_t>(tm.tm_year + 1900);
    std::memcpy(out + 6, &year, 2);
}

// sceMcTblGetDir (64 bytes): creation and modification time, size, attributes, name.
std::array<uint8_t, 64> tableEntry(const Listed& e) {
    std::array<uint8_t, 64> t{};
    putDateTime(t.data(), e.host);
    putDateTime(t.data() + 8, e.host);
    std::error_code ec;
    const uint32_t size = e.dir ? 0 : static_cast<uint32_t>(fs::file_size(e.host, ec));
    std::memcpy(t.data() + 16, &size, 4);
    const uint16_t attr = e.dir ? kAttrDir : kAttrFile;
    std::memcpy(t.data() + 20, &attr, 2);
    std::memcpy(t.data() + 32, e.name.data(), std::min<size_t>(32, e.name.size()));
    return t;
}

// Wildcards: '*' matches any run of characters, '?' any one.
bool matches(const char* pattern, const char* name) {
    if (*pattern == '\0') return *name == '\0';
    if (*pattern == '*') return matches(pattern + 1, name) || (*name && matches(pattern, name + 1));
    if (*name == '\0') return false;
    return (*pattern == '?' || *pattern == *name) && matches(pattern + 1, name + 1);
}

OpenFile* file(int32_t fd) {
    if (fd < 0 || fd >= kMaxFiles || !g_files[static_cast<size_t>(fd)]) return nullptr;
    return &*g_files[static_cast<size_t>(fd)];
}

bool openForWrite(const fs::path& host) {
    for (const auto& f : g_files)
        if (f && f->write && f->host == host) return true;
    return false;
}
bool isOpen(const fs::path& host) {
    for (const auto& f : g_files)
        if (f && f->host == host) return true;
    return false;
}

// ---- operations ---------------------------------------------------------------------------
int32_t detect(int port, int slot) {
    if (!present(port, slot)) {
        if (port >= 0 && port <= 1 && slot == 0) g_cards[port].reported = false;
        return kNoCard;
    }
    Card& c = g_cards[port];
    if (!c.reported) {   // the first probe after an insertion reports the change and resets the directory
        c.reported = true;
        c.cwd = "/";
        return kChangedCard;
    }
    return kOk;
}

// The old protocol fills a 64-byte block (type if asked, free if asked); the
// new one a 192-byte block (type, free, and the format flag at +144).
int32_t getInfo(const Desc& d, bool xmc) {
    const int32_t r = detect(d.port, d.slot);
    const bool card = r >= kChangedCard;
    const uint32_t type = card ? 2 : 0;   // PS2 card
    const uint32_t free = card ? kClusters - usedClusters(cardRoot(d.port)) : 0;
    std::vector<uint8_t> block(xmc ? 192 : 64, 0);
    if (xmc) {
        std::memcpy(block.data(), &type, 4);
        std::memcpy(block.data() + 4, &free, 4);
        const uint32_t formatted = card ? 1 : 0;
        std::memcpy(block.data() + 144, &formatted, 4);
    } else {
        if (d.size > 0) std::memcpy(block.data(), &type, 4);
        if (d.offset > 0) std::memcpy(block.data() + 4, &free, 4);
    }
    if (d.param) writeBytes(d.param, block.data(), static_cast<uint32_t>(block.size()));
    return r;
}

int32_t open(const Name& n) {
    if (!present(n.port, n.slot)) return kNoCard;
    if (n.path.empty()) return kNoEntry;
    const std::vector<std::string> parts = resolve(n.port, n.path);
    const fs::path host = hostOf(n.port, parts);
    std::error_code ec;
    if (n.flags & kOpenMkdir) {
        if (parts.empty() || pathExists(host)) return kNoEntry;
        if (!isDir(host.parent_path()) && parts.size() > 1) return kNoEntry;
        fs::create_directories(host, ec);
        PS2_DEBUG("memcard", "mkdir mc%d:%s", n.port, cardPathOf(parts).c_str());
        return ec ? kDenied : kOk;
    }
    int fd = 0;
    while (fd < kMaxFiles && g_files[static_cast<size_t>(fd)]) ++fd;
    if (fd == kMaxFiles) return kNoHandle;
    OpenFile f;
    f.host = host;
    if (parts.empty() || isDir(host)) {   // a directory: an fd that can only be closed
        if ((n.flags & kOpenCreate) || (!parts.empty() && !pathExists(host))) return kDenied;
        f.dir = true;
        g_files[static_cast<size_t>(fd)] = f;
        return fd;
    }
    if (n.flags & kOpenCreate) {   // creates, or recreates empty
        if (parts.size() > 1 && !isDir(host.parent_path())) return kNoEntry;
        if (isOpen(host)) return kDenied;
        if (parts.size() == 1) fs::create_directories(cardRoot(n.port), ec);
        f.fp = std::fopen(host.string().c_str(), "w+b");
        f.read = true;
        f.write = true;
    } else {
        if (!pathExists(host)) return kNoEntry;
        f.read = (n.flags & kOpenRead) != 0;
        f.write = (n.flags & kOpenWrite) != 0;
        if (f.write && openForWrite(host)) return kDenied;
        f.fp = std::fopen(host.string().c_str(), f.write ? "r+b" : "rb");
    }
    if (!f.fp) return kDenied;
    PS2_DEBUG("memcard", "open mc%d:%s (0x%x) = %d", n.port, cardPathOf(parts).c_str(), n.flags, fd);
    g_files[static_cast<size_t>(fd)] = f;
    return fd;
}

int32_t close(int32_t fd) {
    OpenFile* f = file(fd);
    if (!f) return kDenied;
    if (f->fp) std::fclose(f->fp);
    g_files[static_cast<size_t>(fd)].reset();
    return kOk;
}

int32_t seek(const Desc& d) {
    OpenFile* f = file(d.fd);
    if (!f || !f->fp) return kDenied;
    long base = 0;
    if (d.origin == 2) {
        std::fseek(f->fp, 0, SEEK_END);
        base = std::ftell(f->fp);
    } else if (d.origin != 0) {
        base = std::ftell(f->fp);
    }
    const long pos = std::max<long>(0, base + d.offset);
    std::fseek(f->fp, pos, SEEK_SET);
    return static_cast<int32_t>(pos);
}

// The real server sends unaligned head and tail bytes in the parameter block
// for the EE to copy; here everything goes straight to the buffer, so the
// block says there is nothing to copy.
int32_t read(const Desc& d, bool xmc) {
    int32_t r = kDenied;
    OpenFile* f = file(d.fd);
    if (f && f->fp && f->read && d.size >= 0) {
        std::vector<uint8_t> buf(static_cast<size_t>(d.size));
        const size_t n = std::fread(buf.data(), 1, buf.size(), f->fp);
        writeBytes(d.buffer, buf.data(), static_cast<uint32_t>(n));
        r = static_cast<int32_t>(n);
    }
    if (d.param) fillBytes(d.param, 0, xmc ? 192 : 64);
    return r;
}

// The first `origin` bytes come inline in the descriptor, the other `size` from EE memory.
int32_t write(const Desc& d) {
    OpenFile* f = file(d.fd);
    if (!f || !f->fp || !f->write || d.size < 0 || d.origin < 0 || d.origin > 16) return kDenied;
    std::vector<uint8_t> buf(static_cast<size_t>(d.origin + d.size));
    std::memcpy(buf.data(), d.data, static_cast<size_t>(d.origin));
    readBytes(d.buffer, buf.data() + d.origin, static_cast<uint32_t>(d.size));
    return static_cast<int32_t>(std::fwrite(buf.data(), 1, buf.size(), f->fp));
}

int32_t flush(int32_t fd) {
    OpenFile* f = file(fd);
    if (!f) return kDenied;
    if (f->fp) std::fflush(f->fp);
    return kOk;
}

// Changes the directory (an empty name only asks) and always writes the
// current one, as an absolute path, to the EE's 1024-byte buffer.
int32_t chdir(const Name& n) {
    int32_t r = present(n.port, n.slot) ? kOk : kNoCard;
    if (r == kOk && !n.path.empty()) {
        const std::vector<std::string> parts = resolve(n.port, n.path);
        if (parts.empty() || isDir(hostOf(n.port, parts))) g_cards[n.port].cwd = cardPathOf(parts);
        else r = kNoEntry;
    }
    std::array<uint8_t, 1024> buf{};
    if (n.port >= 0 && n.port <= 1) {
        const std::string& cwd = g_cards[n.port].cwd;
        std::memcpy(buf.data(), cwd.data(), std::min<size_t>(cwd.size(), buf.size() - 1));
    }
    if (n.table) writeBytes(n.table, buf.data(), static_cast<uint32_t>(buf.size()));
    return r;
}

// flags 0 starts a listing of "dir/pattern"; other values continue it. The
// root has no "." and "..", other directories list them first.
int32_t getDir(const Name& n) {
    if (n.maxent <= 0) return 0;
    if ((n.flags & 0xFFFF) == 0) {
        if (!present(n.port, n.slot)) return kNoCard;
        const auto slash = n.path.rfind('/');
        const std::string dirPart = slash == std::string::npos ? "" : n.path.substr(0, slash + 1);
        const std::string pattern = slash == std::string::npos ? n.path : n.path.substr(slash + 1);
        const std::vector<std::string> parts = resolve(n.port, dirPart);
        const fs::path dir = hostOf(n.port, parts);
        if (!parts.empty() && !isDir(dir)) {
            g_listingValid = false;
            return kNoEntry;
        }
        g_listing.clear();
        g_listingNext = 0;
        g_listingValid = true;
        if (!parts.empty()) {
            if (matches(pattern.c_str(), ".")) g_listing.push_back({".", dir, true});
            if (matches(pattern.c_str(), "..")) g_listing.push_back({"..", dir.parent_path(), true});
        }
        for (const fs::path& p : children(dir)) {
            const std::string name = p.filename().string();
            if (matches(pattern.c_str(), name.c_str())) g_listing.push_back({name, p, isDir(p)});
        }
    } else if (!g_listingValid) {
        return kNoEntry;
    }
    int32_t count = 0;
    for (; n.table && count < n.maxent && g_listingNext < g_listing.size(); ++count) {
        const auto entry = tableEntry(g_listing[g_listingNext++]);
        writeBytes(n.table + 64u * static_cast<uint32_t>(count), entry.data(), 64);
    }
    return count;
}

// Times and attributes are accepted and not kept; flag 0x10 renames to the
// table entry's name.
int32_t setFileInfo(const Name& n) {
    if (!present(n.port, n.slot)) return kNoCard;
    const std::vector<std::string> parts = resolve(n.port, n.path);
    const fs::path host = hostOf(n.port, parts);
    if (parts.empty() || !pathExists(host)) return kNoEntry;
    if (n.flags & 0x10) {
        std::array<char, 33> raw{};
        readBytes(n.table + 32, raw.data(), 32);
        const std::string name = std::string(raw.data()).substr(0, 31);
        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) return kNoEntry;
        const fs::path to = host.parent_path() / name;
        if (pathExists(to)) return kNoEntry;
        if (isOpen(host)) return kDenied;
        std::error_code ec;
        fs::rename(host, to, ec);
        if (ec) return kDenied;
    }
    return kOk;
}

int32_t remove(const Name& n) {
    if (!present(n.port, n.slot)) return kNoCard;
    if (n.flags != 0) return kNoEntry;   // undelete
    const std::vector<std::string> parts = resolve(n.port, n.path);
    const fs::path host = hostOf(n.port, parts);
    if (parts.empty() || !pathExists(host)) return kNoEntry;
    if (isOpen(host)) return kDenied;
    if (isDir(host) && !children(host).empty()) return kNotEmpty;
    std::error_code ec;
    fs::remove(host, ec);
    PS2_DEBUG("memcard", "delete mc%d:%s", n.port, cardPathOf(parts).c_str());
    return ec ? kDenied : kOk;
}

// Free directory entries in the last cluster of a directory (two per cluster).
int32_t entrySpace(const Name& n) {
    if (!present(n.port, n.slot)) return kNoCard;
    const fs::path dir = hostOf(n.port, resolve(n.port, n.path));
    return static_cast<int32_t>((children(dir).size() + 2) & 1);
}

std::vector<uint8_t> words(std::initializer_list<int32_t> v) {
    std::vector<uint8_t> out(v.size() * 4);
    size_t i = 0;
    for (int32_t x : v) std::memcpy(out.data() + 4 * i++, &x, 4);
    return out;
}

class McServer final : public RpcServer {
public:
    McServer() : RpcServer("MCSERV") {}

    void call(uint32_t fno, const uint8_t* d, uint32_t, uint32_t, Reply reply) override {
        int32_t r = last_;
        switch (fno) {
        case 0x70: r = kOk; break;                                   // init (old protocol)
        case 0xFE:                                                   // init, XMCSERV: result and versions
            last_ = kOk;
            return reply(words({kOk, 0x210, 0x230}));
        case 0x78: r = getInfo(desc(d), false); break;
        case 0x01: r = getInfo(desc(d), true); break;
        case 0x71: case 0x02: r = open(nameBlock(d)); break;
        case 0x72: case 0x03: r = close(i32(d, 0)); break;
        case 0x75: case 0x04: r = seek(desc(d)); break;
        case 0x73: r = read(desc(d), false); break;
        case 0x05: r = read(desc(d), true); break;
        case 0x74: case 0x06: r = write(desc(d)); break;
        case 0x7A: case 0x0A: r = flush(i32(d, 0)); break;
        case 0x7B: case 0x0C: r = chdir(nameBlock(d)); break;
        case 0x76: case 0x0D: r = getDir(nameBlock(d)); break;
        case 0x7C: case 0x0E: r = setFileInfo(nameBlock(d)); break;
        case 0x79: case 0x0F: r = remove(nameBlock(d)); break;
        case 0x12: r = entrySpace(nameBlock(d)); break;
        case 0x14: r = kOk; break;                                   // thread priority
        case 0x33: r = kOk; break;                                   // check block: no bad blocks
        case 0x77: case 0x10: case 0x80: case 0x11:                  // format / unformat
            PS2_WARN("memcard", "the game asked to %sformat the card in port %d; cards are host directories and are left as they are",
                     fno == 0x80 || fno == 0x11 ? "un" : "", i32(d, 4) + 1);
            r = kOk;
            break;
        default: {
            // The real server leaves the previous result in place for an unknown function.
            char key[64];
            std::snprintf(key, sizeof key, "MCSERV function 0x%02x", fno);
            PS2_UNIMPL("memcard", key, "%s not implemented; returning the previous result (%d)", key, r);
            break;
        }
        }
        last_ = r;
        reply(words({r}));
    }

private:
    int32_t last_ = 0;
};

} // namespace

std::unique_ptr<RpcServer> makeServer() { return std::make_unique<McServer>(); }

void reset() {
    for (auto& f : g_files) {
        if (f && f->fp) std::fclose(f->fp);
        f.reset();
    }
    for (Card& c : g_cards) c = Card{};
    g_listing.clear();
    g_listingNext = 0;
    g_listingValid = false;
}

} // namespace ps2::iop::memcard
