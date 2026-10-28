#include "iop/disc.h"

#include "log.h"

#include "ps2n/util.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>

namespace ps2::iop::disc {

namespace fs = std::filesystem;
using ps2n::toUpper;

namespace {

constexpr uint32_t kSector = 2048;
constexpr uint32_t kPvdLsn = 16;

// Where the sectors come from.
class Source {
public:
    virtual ~Source() = default;
    virtual uint32_t sectors() const = 0;
    virtual bool read(uint32_t lsn, uint8_t* dst) = 0;   // one sector of user data
};

// An image file: cooked 2048-byte sectors, or raw 2352-byte ones with the
// user data at `dataOffset` (16 for MODE1, 24 for MODE2 form 1).
class ImageSource final : public Source {
public:
    ImageSource(std::ifstream in, uint32_t rawSize, uint32_t dataOffset, uint32_t sectors)
        : in_(std::move(in)), rawSize_(rawSize), dataOffset_(dataOffset), sectors_(sectors) {}
    uint32_t sectors() const override { return sectors_; }
    bool read(uint32_t lsn, uint8_t* dst) override {
        if (lsn >= sectors_) return false;
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(lsn) * rawSize_ + dataOffset_);
        in_.read(reinterpret_cast<char*>(dst), kSector);
        return in_.gcount() == static_cast<std::streamsize>(kSector);
    }

private:
    std::ifstream in_;
    uint32_t rawSize_, dataOffset_, sectors_;
};

// ---- ISO 9660 encoding -------------------------------------------------------------------
void put16both(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}
void put32le(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void put32be(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}
void put32both(uint8_t* p, uint32_t v) {
    put32le(p, v);
    put32be(p + 4, v);
}
uint32_t get32le(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
void putText(uint8_t* p, size_t n, const std::string& s) {
    std::memset(p, ' ', n);
    std::memcpy(p, s.data(), std::min(n, s.size()));
}

// Directory record length for a name of `n` bytes (padded to an even length).
uint32_t recordLength(size_t n) { return static_cast<uint32_t>(33 + n + (n % 2 == 0 ? 1 : 0)); }

void recordDate(uint8_t out[7], std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    out[0] = static_cast<uint8_t>(tm.tm_year);
    out[1] = static_cast<uint8_t>(tm.tm_mon + 1);
    out[2] = static_cast<uint8_t>(tm.tm_mday);
    out[3] = static_cast<uint8_t>(tm.tm_hour);
    out[4] = static_cast<uint8_t>(tm.tm_min);
    out[5] = static_cast<uint8_t>(tm.tm_sec);
    out[6] = 0;
}

std::time_t hostTime(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto sys = std::chrono::system_clock::now() + (t - fs::file_time_type::clock::now());
    return std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(sys));
}

// A host directory as an ISO 9660 volume. Sectors 0 up to the first file
// (system area, volume descriptors, path tables, directories) are built in
// memory; file sectors are read from the host files on demand.
class DirectorySource final : public Source {
public:
    explicit DirectorySource(const fs::path& root) {
        nodes_.push_back(Node{});
        nodes_[0].dir = true;
        nodes_[0].host = root;
        recordDate(nodes_[0].date, hostTime(root));
        scan(0);
        layout();
    }

    uint32_t sectors() const override { return sectors_; }

    bool read(uint32_t lsn, uint8_t* dst) override {
        if (lsn >= sectors_) return false;
        if (lsn < metaSectors_) {
            std::memcpy(dst, meta_.data() + static_cast<size_t>(lsn) * kSector, kSector);
            return true;
        }
        // The file whose extent holds `lsn` (files are laid out in order).
        auto it = std::upper_bound(files_.begin(), files_.end(), lsn,
                                   [&](uint32_t l, int idx) { return l < nodes_[static_cast<size_t>(idx)].lsn; });
        std::memset(dst, 0, kSector);
        if (it == files_.begin()) return true;
        const Node& f = nodes_[static_cast<size_t>(*--it)];
        const uint64_t off = static_cast<uint64_t>(lsn - f.lsn) * kSector;
        if (off >= f.size) return true;
        if (openFile_ != &f) {
            in_.close();
            in_.clear();
            in_.open(f.host, std::ios::binary);
            openFile_ = &f;
        }
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(off));
        in_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(std::min<uint64_t>(kSector, f.size - off)));
        return true;
    }

    std::vector<uint8_t> image() {
        std::vector<uint8_t> out(static_cast<size_t>(sectors_) * kSector);
        for (uint32_t l = 0; l < sectors_; ++l) read(l, out.data() + static_cast<size_t>(l) * kSector);
        return out;
    }

private:
    struct Node {
        std::string name;   // recorded name
        fs::path host;
        bool dir = false;
        uint32_t lsn = 0, size = 0;   // directories: the extent size
        int parent = 0;
        std::vector<int> children;
        uint8_t date[7] = {};
    };

    void scan(int dirIndex) {
        std::error_code ec;
        std::vector<Node> found;
        for (const auto& e : fs::directory_iterator(nodes_[static_cast<size_t>(dirIndex)].host, ec)) {
            Node n;
            n.host = e.path();
            n.dir = e.is_directory(ec);
            if (!n.dir && !e.is_regular_file(ec)) continue;
            n.name = toUpper(e.path().filename().string());
            if (n.name.size() > 200) {   // a directory record holds at most 222 bytes of name
                PS2_WARN("disc", "%s: name too long for ISO 9660; left out", e.path().string().c_str());
                continue;
            }
            if (!n.dir) {
                const uint64_t size = e.file_size(ec);
                if (size > 0xFFFFFFFFu) {
                    PS2_WARN("disc", "%s is larger than 4 GB, which ISO 9660 cannot record; left out",
                             e.path().string().c_str());
                    continue;
                }
                n.size = static_cast<uint32_t>(size);
                n.name += ";1";
            }
            n.parent = dirIndex;
            recordDate(n.date, hostTime(n.host));
            found.push_back(std::move(n));
        }
        std::sort(found.begin(), found.end(), [](const Node& a, const Node& b) { return a.name < b.name; });
        for (Node& n : found) {
            nodes_.push_back(std::move(n));
            nodes_[static_cast<size_t>(dirIndex)].children.push_back(static_cast<int>(nodes_.size() - 1));
        }
        for (int c : std::vector<int>(nodes_[static_cast<size_t>(dirIndex)].children))
            if (nodes_[static_cast<size_t>(c)].dir) scan(c);
    }

    // Directory records never straddle a sector boundary.
    uint32_t directoryBytes(const Node& d) const {
        uint32_t pos = 2 * recordLength(1);
        for (int c : d.children) {
            const uint32_t len = recordLength(nodes_[static_cast<size_t>(c)].name.size());
            if (pos % kSector + len > kSector) pos = (pos / kSector + 1) * kSector;
            pos += len;
        }
        return (pos + kSector - 1) / kSector * kSector;
    }

    void layout() {
        // Directories in breadth-first order: the path table order.
        std::vector<int> dirs{0};
        for (size_t i = 0; i < dirs.size(); ++i)
            for (int c : nodes_[static_cast<size_t>(dirs[i])].children)
                if (nodes_[static_cast<size_t>(c)].dir) dirs.push_back(c);
        std::vector<int> number(nodes_.size(), 0);   // 1-based directory numbers
        uint32_t pathTableBytes = 0;
        for (size_t i = 0; i < dirs.size(); ++i) {
            number[static_cast<size_t>(dirs[i])] = static_cast<int>(i + 1);
            const size_t n = dirs[i] == 0 ? 1 : nodes_[static_cast<size_t>(dirs[i])].name.size();
            pathTableBytes += static_cast<uint32_t>(8 + n + (n % 2));
        }
        const uint32_t pathTableSectors = (pathTableBytes + kSector - 1) / kSector;
        const uint32_t lTable = kPvdLsn + 2, mTable = lTable + pathTableSectors;
        uint32_t next = mTable + pathTableSectors;
        for (int d : dirs) {
            Node& n = nodes_[static_cast<size_t>(d)];
            n.size = directoryBytes(n);
            n.lsn = next;
            next += n.size / kSector;
        }
        metaSectors_ = next;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            Node& n = nodes_[i];
            if (n.dir) continue;
            n.lsn = next;
            next += (n.size + kSector - 1) / kSector;
            if (n.size) files_.push_back(static_cast<int>(i));
        }
        sectors_ = next;

        meta_.assign(static_cast<size_t>(metaSectors_) * kSector, 0);
        // Primary volume descriptor.
        uint8_t* pvd = meta_.data() + kPvdLsn * kSector;
        pvd[0] = 1;
        std::memcpy(pvd + 1, "CD001", 5);
        pvd[6] = 1;
        putText(pvd + 8, 32, "PLAYSTATION");
        putText(pvd + 40, 32, toUpper(nodes_[0].host.filename().string()));
        put32both(pvd + 80, sectors_);
        put16both(pvd + 120, 1);
        put16both(pvd + 124, 1);
        put16both(pvd + 128, static_cast<uint16_t>(kSector));
        put32both(pvd + 132, pathTableBytes);
        put32le(pvd + 140, lTable);
        put32be(pvd + 148, mTable);
        writeRecord(pvd + 156, nodes_[0], std::string(1, '\0'));
        putText(pvd + 190, 623, "");   // volume set ... bibliographic file identifiers: unspecified
        for (int i = 0; i < 4; ++i) {   // creation, modification, expiration, effective dates: unspecified
            std::memset(pvd + 813 + 17 * i, '0', 16);
            pvd[813 + 17 * i + 16] = 0;
        }
        pvd[881] = 1;
        // Volume descriptor set terminator.
        uint8_t* term = meta_.data() + (kPvdLsn + 1) * kSector;
        term[0] = 255;
        std::memcpy(term + 1, "CD001", 5);
        term[6] = 1;
        // Path tables, little and big endian.
        size_t lp = static_cast<size_t>(lTable) * kSector, mp = static_cast<size_t>(mTable) * kSector;
        for (int d : dirs) {
            const Node& n = nodes_[static_cast<size_t>(d)];
            const std::string name = d == 0 ? std::string(1, '\0') : n.name;
            const uint16_t parent = static_cast<uint16_t>(d == 0 ? 1 : number[static_cast<size_t>(n.parent)]);
            for (int big = 0; big < 2; ++big) {
                uint8_t* p = meta_.data() + (big ? mp : lp);
                p[0] = static_cast<uint8_t>(name.size());
                if (big) {
                    put32be(p + 2, n.lsn);
                    p[6] = static_cast<uint8_t>(parent >> 8);
                    p[7] = static_cast<uint8_t>(parent);
                } else {
                    put32le(p + 2, n.lsn);
                    p[6] = static_cast<uint8_t>(parent);
                    p[7] = static_cast<uint8_t>(parent >> 8);
                }
                std::memcpy(p + 8, name.data(), name.size());
            }
            const size_t len = 8 + name.size() + (name.size() % 2);
            lp += len;
            mp += len;
        }
        // Directory records.
        for (int d : dirs) {
            const Node& n = nodes_[static_cast<size_t>(d)];
            uint8_t* base = meta_.data() + static_cast<size_t>(n.lsn) * kSector;
            uint32_t pos = 0;
            pos += writeRecord(base + pos, n, std::string(1, '\0'));
            pos += writeRecord(base + pos, nodes_[static_cast<size_t>(n.parent)], std::string(1, '\1'));
            for (int c : n.children) {
                const Node& child = nodes_[static_cast<size_t>(c)];
                const uint32_t len = recordLength(child.name.size());
                if (pos % kSector + len > kSector) pos = (pos / kSector + 1) * kSector;
                pos += writeRecord(base + pos, child, child.name);
            }
        }
    }

    static uint32_t writeRecord(uint8_t* r, const Node& n, const std::string& name) {
        const uint32_t len = recordLength(name.size());
        r[0] = static_cast<uint8_t>(len);
        put32both(r + 2, n.lsn);
        put32both(r + 10, n.size);
        std::memcpy(r + 18, n.date, 7);
        r[25] = n.dir ? 0x02 : 0x00;
        put16both(r + 28, 1);
        r[32] = static_cast<uint8_t>(name.size());
        std::memcpy(r + 33, name.data(), name.size());
        return len;
    }

    std::vector<Node> nodes_;
    std::vector<int> files_;   // non-empty files, by LSN
    std::vector<uint8_t> meta_;
    uint32_t metaSectors_ = 0, sectors_ = 0;
    std::ifstream in_;
    const Node* openFile_ = nullptr;
};

std::unique_ptr<Source> g_source;
Type g_type = Type::None;

// ---- ISO 9660 reading ---------------------------------------------------------------------
Entry parseRecord(const uint8_t* r) {
    Entry e;
    e.lsn = get32le(r + 2);
    e.size = get32le(r + 10);
    std::memcpy(e.date, r + 18, 7);
    e.dir = (r[25] & 0x02) != 0;
    e.name.assign(reinterpret_cast<const char*>(r + 33), r[32]);
    return e;
}

bool rootEntry(Entry& out) {
    uint8_t s[kSector];
    if (!g_source || !g_source->read(kPvdLsn, s) || s[0] != 1 || std::memcmp(s + 1, "CD001", 5) != 0) return false;
    out = parseRecord(s + 156);
    out.name.clear();
    return true;
}

bool readDirectory(const Entry& dir, std::vector<Entry>& out) {
    uint8_t s[kSector];
    const uint32_t count = (dir.size + kSector - 1) / kSector;
    for (uint32_t i = 0; i < count; ++i) {
        if (!g_source->read(dir.lsn + i, s)) return false;
        for (uint32_t pos = 0; pos + 33 < kSector && s[pos] != 0;) {
            const uint8_t len = s[pos];
            if (len < 34 || pos + len > kSector) break;
            Entry e = parseRecord(s + pos);
            if (!(e.name.size() == 1 && (e.name[0] == '\0' || e.name[0] == '\1'))) out.push_back(std::move(e));
            pos += len;
        }
    }
    return true;
}

std::string stripVersion(const std::string& n) {
    const auto semi = n.find(';');
    std::string s = toUpper(semi == std::string::npos ? n : n.substr(0, semi));
    if (!s.empty() && s.back() == '.') s.pop_back();   // "NAME." for files without an extension
    return s;
}

bool walk(const std::string& path, Entry& out) {
    Entry cur;
    if (!rootEntry(cur)) return false;
    std::string part;
    auto step = [&](const std::string& name) {
        std::vector<Entry> entries;
        if (!cur.dir || !readDirectory(cur, entries)) return false;
        const std::string want = stripVersion(name);
        for (Entry& e : entries) {
            if (stripVersion(e.name) == want) {
                cur = std::move(e);
                return true;
            }
        }
        return false;
    };
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!part.empty() && part != "." && !step(part)) return false;
            part.clear();
        } else {
            part.push_back(c);
        }
    }
    if (!part.empty() && part != "." && !step(part)) return false;
    out = std::move(cur);
    return true;
}

std::unique_ptr<Source> openImage(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return nullptr;
    std::error_code ec;
    const uint64_t bytes = fs::file_size(path, ec);
    // Cooked 2048-byte sectors, then raw 2352-byte MODE1 and MODE2 sectors.
    const struct { uint32_t raw, offset; } layouts[] = {{2048, 0}, {2352, 16}, {2352, 24}};
    for (const auto& l : layouts) {
        char id[6] = {};
        in.clear();
        in.seekg(static_cast<std::streamoff>(kPvdLsn) * l.raw + l.offset);
        in.read(id, 6);
        if (in.gcount() == 6 && std::memcmp(id + 1, "CD001", 5) == 0) {
            g_type = l.raw == 2048 ? Type::Dvd : Type::Cd;
            return std::make_unique<ImageSource>(std::move(in), l.raw, l.offset, static_cast<uint32_t>(bytes / l.raw));
        }
    }
    return nullptr;
}

} // namespace

bool insert(const fs::path& path) {
    eject();
    if (path.empty()) return true;
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        g_source = std::make_unique<DirectorySource>(path);
        g_type = Type::Dvd;
        PS2_INFO("disc", "%s inserted as a DVD (%u sectors built from the directory)", path.string().c_str(),
                 g_source->sectors());
        return true;
    }
    g_source = openImage(path);
    if (!g_source) {
        PS2_WARN("disc", "%s is neither a directory nor an ISO 9660 image; the drive stays empty", path.string().c_str());
        g_type = Type::None;
        return false;
    }
    PS2_INFO("disc", "%s inserted as a %s (%u sectors)", path.string().c_str(), g_type == Type::Cd ? "CD" : "DVD",
             g_source->sectors());
    return true;
}

void eject() {
    g_source.reset();
    g_type = Type::None;
}

Type type() { return g_type; }
uint32_t sectorCount() { return g_source ? g_source->sectors() : 0; }

bool read(uint32_t lsn, uint32_t count, uint8_t* dst) {
    if (!g_source) return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!g_source->read(lsn + i, dst + static_cast<size_t>(i) * kSector)) return false;
    return true;
}

bool find(const std::string& path, Entry& out) { return g_source && walk(path, out); }

bool list(const std::string& dirPath, std::vector<Entry>& out) {
    Entry d;
    if (!find(dirPath, d) || !d.dir) return false;
    return readDirectory(d, out);
}

std::vector<uint8_t> buildImage(const fs::path& dir) { return DirectorySource(dir).image(); }

} // namespace ps2::iop::disc
