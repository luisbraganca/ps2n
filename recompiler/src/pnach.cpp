#include "pnach.h"

#include "ps2n/elf.h"
#include "ps2n/util.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <set>

namespace ps2n::pnach {

namespace {

// A whole field of hexadecimal digits, as PCSX2 reads addresses and data
// (no 0x prefix).
std::optional<uint64_t> parseHex(std::string_view s, size_t maxDigits) {
    if (s.empty() || s.size() > maxDigits) return std::nullopt;
    uint64_t v = 0;
    for (char c : s) {
        const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return std::nullopt;
        v = v << 4 | static_cast<uint64_t>(d);
    }
    return v;
}

std::optional<Type> parseType(std::string_view s) {
    static const std::pair<const char*, Type> kTypes[] = {
        {"byte", Type::Byte},       {"short", Type::Short},       {"word", Type::Word},
        {"double", Type::Double},   {"extended", Type::Extended}, {"beshort", Type::BeShort},
        {"beword", Type::BeWord},   {"bedouble", Type::BeDouble}, {"bytes", Type::Bytes},
    };
    for (const auto& [name, t] : kTypes)
        if (s == name) return t;
    return std::nullopt;
}

// The fields after "patch="; an error message, or "" with `out` filled in.
std::string parsePatch(std::string_view value, Line& out) {
    std::vector<std::string> f = split(value, ',');
    if (f.size() != 5) return "a patch needs five fields: place,cpu,address,type,data";
    for (std::string& s : f) s = std::string(trim(s));
    const auto place = parseInt(f[0]);
    if (!place) return "the place must be a number";
    out.place = static_cast<int>(*place);
    if (f[1] != "EE" && f[1] != "IOP") return "the cpu must be EE or IOP";
    out.ee = f[1] == "EE";
    const auto addr = parseHex(f[2], 8);
    if (!addr) return "the address must be hexadecimal digits";
    out.address = static_cast<uint32_t>(*addr);
    const auto type = parseType(f[3]);
    if (!type) return "unknown type '" + f[3] + "'";
    out.type = *type;
    if (out.type == Type::Bytes) {   // two digits a byte; an odd last digit counts for nothing
        if (f[4].size() < 2) return "bytes data must be hexadecimal digits";
        for (size_t i = 0; i + 1 < f[4].size(); i += 2) {
            const auto b = parseHex(std::string_view(f[4]).substr(i, 2), 2);
            if (!b) return "bytes data must be hexadecimal digits";
            out.bytes.push_back(static_cast<uint8_t>(*b));
        }
        return "";
    }
    const auto data = parseHex(f[4], 16);
    if (!data) return "the data must be hexadecimal digits";
    out.data = *data;
    return "";
}

// gsaspectratio: two whole numbers, N:D with D not 0, as PCSX2 takes it.
bool validAspect(std::string_view s) {
    const size_t colon = s.find(':');
    if (colon == std::string_view::npos) return false;
    const auto n = parseInt(s.substr(0, colon)), d = parseInt(s.substr(colon + 1));
    return n && d && *n >= 0 && *d > 0 && s.substr(0, colon).find_first_not_of("0123456789") == std::string_view::npos &&
           s.substr(colon + 1).find_first_not_of("0123456789") == std::string_view::npos;
}

} // namespace

std::string Line::where() const { return file.filename().string() + ":" + std::to_string(lineNo); }

File parse(std::string_view text, const std::filesystem::path& path) {
    File file;
    file.path = path;
    file.groups.emplace_back();   // the lines before the first [group]
    int lineNo = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        if (const size_t c = line.find("//"); c != std::string_view::npos) line = trim(line.substr(0, c));
        if (line.empty()) continue;
        Group& group = file.groups.back();
        const std::string at = path.filename().string() + ":" + std::to_string(lineNo) + ": ";
        if (line.front() == '[') {
            if (line.back() != ']') group.malformed.push_back(at + "a group's name needs a closing ]");
            else file.groups.emplace_back().name = std::string(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            group.malformed.push_back(at + "not a key=value line");
            continue;
        }
        // Keys as PCSX2 knows them, case and all; it ignores any other.
        const std::string_view key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "patch") {
            Line l;
            l.file = path;
            l.lineNo = lineNo;
            if (std::string err = parsePatch(value, l); !err.empty()) group.malformed.push_back(at + err);
            else group.lines.push_back(std::move(l));
        } else if (key == "dpatch") {
            group.dpatches.push_back(lineNo);
        } else if (key == "gsaspectratio") {
            group.aspectRatio = std::string(value);
        } else if (key == "gsinterlacemode") {
            group.interlaceMode = std::string(value);
        }
    }
    return file;
}

File load(const std::filesystem::path& path) { return parse(readTextFile(path), path); }

uint32_t pcsx2Crc(const std::vector<uint8_t>& elfBytes) {
    uint32_t crc = 0;
    for (size_t i = 0; i + 4 <= elfBytes.size(); i += 4) {
        uint32_t w;
        std::memcpy(&w, elfBytes.data() + i, 4);
        crc ^= w;
    }
    return crc;
}

Patches Patches::load(const std::vector<std::filesystem::path>& files, const std::vector<std::string>& groups) {
    Patches p;
    std::vector<File> loaded;
    for (const auto& f : files) loaded.push_back(pnach::load(f));
    std::vector<bool> found(groups.size(), false);
    std::set<std::string> taken;   // a name's first group wins, as in PCSX2
    std::vector<std::string> problems;
    for (const File& file : loaded) {
        const std::string fileName = file.path.filename().string();
        std::string names;   // the file's chosen groups, for describe()
        for (const Group& g : file.groups) {
            bool chosen = g.name.empty();
            for (size_t i = 0; i < groups.size(); ++i)
                if (g.name == groups[i]) chosen = found[i] = true;
            if (!chosen) continue;
            const std::string label = fileName + (g.name.empty() ? "" : " [" + g.name + "]");
            for (const std::string& m : g.malformed) p.warnings_.push_back(m + ": skipped, as PCSX2 skips it");
            if (!g.name.empty()) {
                if (g.lines.empty() && g.dpatches.empty()) {
                    p.warnings_.push_back(label + " has no patches: PCSX2 drops such a group, its settings too");
                    continue;
                }
                if (!taken.insert(g.name).second) {
                    p.warnings_.push_back(label + ": a group of that name came first, and PCSX2 keeps that one");
                    continue;
                }
                names += (names.empty() ? "" : ", ") + g.name;
            }
            for (int d : g.dpatches)
                problems.push_back(fileName + ":" + std::to_string(d) + ": dpatch (a patch placed by matching code) is not supported");
            std::vector<Line> lines;
            for (const Line& l : g.lines) {
                if (l.place < 0 || l.place > 3) {
                    p.warnings_.push_back(l.where() + ": place " + std::to_string(l.place) + " never applies, in PCSX2 either");
                    continue;
                }
                if (!l.ee) {
                    problems.push_back(l.where() + ": patches to the IOP are not supported");
                    continue;
                }
                if (l.type == Type::Extended) {
                    const uint32_t code = l.address >> 28;
                    if (code == 3 || (code >= 5 && code <= 7)) {
                        static const char* const kNames[] = {"", "", "", "an increment or decrement", "", "a copy", "a pointer write",
                                                             "a bitwise operation"};
                        problems.push_back(l.where() + strprintf(": extended code %X (%s) is not supported", code, kNames[code]));
                        continue;
                    }
                    if (code >= 8 && code != 0xD && code != 0xE)
                        p.warnings_.push_back(l.where() + strprintf(": extended code %X does nothing, in PCSX2 either", code));
                }
                lines.push_back(l);
            }
            p.groups_.push_back(std::move(lines));
            if (!g.aspectRatio.empty()) {
                if (validAspect(g.aspectRatio)) p.aspectRatio_ = g.aspectRatio;
                else p.warnings_.push_back(label + ": gsaspectratio '" + g.aspectRatio + "' is not N:D, which PCSX2 rejects");
            }
            if (!g.interlaceMode.empty())
                p.warnings_.push_back(label + ": gsinterlacemode is not applied (the runtime's deinterlacing is chosen with --deinterlace)");
        }
        p.chosen_.push_back(fileName + (names.empty() ? "" : " [" + names + "]"));
    }
    for (size_t i = 0; i < groups.size(); ++i) {
        if (found[i]) continue;
        std::string have;
        for (const File& file : loaded)
            for (const Group& g : file.groups)
                if (!g.name.empty()) have += (have.empty() ? "" : ", ") + g.name;
        throw Error("[pnach] groups: no group '" + groups[i] + "' in the files" + (have.empty() ? "" : " (they have: " + have + ")"));
    }
    if (!problems.empty()) {
        std::string msg = "[pnach]: the chosen patches have lines ps2n cannot apply:";
        for (const std::string& s : problems) msg += "\n  " + s;
        throw Error(msg);
    }
    return p;
}

std::string Patches::describe() const {
    std::string s;
    for (const std::string& c : chosen_) s += (s.empty() ? "" : ", ") + c;
    return s;
}

namespace {

// The executable's initialised memory (its segments' file contents), with the
// patches' writes over it. Memory the ELF does not initialise (BSS, the heap)
// holds what the game puts there at run time, which no recompile-time patch
// can know or change.
class Memory {
public:
    explicit Memory(const ElfFile& elf) : elf_(elf) {}

    bool initialised(uint32_t a) const {
        for (const ElfSegment& s : elf_.segments)
            if (s.type == elf::PT_LOAD && a >= s.vaddr && a - s.vaddr < s.filesz) return true;
        return false;
    }

    // Little-endian, as the EE reads it.
    bool read(uint32_t a, size_t n, uint64_t& out) const {
        out = 0;
        for (size_t i = 0; i < n; ++i) {
            const uint32_t at = a + static_cast<uint32_t>(i);
            if (!initialised(at)) return false;
            uint8_t b = 0;
            if (const auto it = written_.find(at); it != written_.end()) b = it->second;
            else elf_.read8(at, b);
            out |= uint64_t(b) << (8 * i);
        }
        return true;
    }

    bool write(uint32_t a, const std::vector<uint8_t>& bytes) {
        for (size_t i = 0; i < bytes.size(); ++i)
            if (!initialised(a + static_cast<uint32_t>(i))) return false;
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint32_t at = a + static_cast<uint32_t>(i);
            uint64_t old = 0;
            read(at, 1, old);
            if (old != bytes[i]) ++changes_;
            written_[at] = bytes[i];
        }
        return true;
    }

    uint64_t changes() const { return changes_; }

    // The words that changed, each with the ELF's word as expect.
    std::vector<Patch> patches() const {
        std::vector<Patch> out;
        uint32_t last = 1;   // not a word address
        for (const auto& [a, b] : written_) {
            const uint32_t w = a & ~3u;
            if (w == last) continue;
            last = w;
            uint32_t orig = 0;
            elf_.read32(w, orig);
            uint64_t now = 0;
            read(w, 4, now);
            if (static_cast<uint32_t>(now) != orig) out.push_back({w, static_cast<uint32_t>(now), orig});
        }
        return out;
    }

private:
    const ElfFile& elf_;
    std::map<uint32_t, uint8_t> written_;
    uint64_t changes_ = 0;
};

std::vector<uint8_t> littleEndian(uint64_t v, size_t n) {
    std::vector<uint8_t> b(n);
    for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(v >> (8 * i));
    return b;
}

std::vector<uint8_t> bigEndian(uint64_t v, size_t n) {
    std::vector<uint8_t> b = littleEndian(v, n);
    std::reverse(b.begin(), b.end());
    return b;
}

// An EE address as main RAM sees it: the uncached mirrors fold onto it.
uint32_t ramAddress(uint32_t a) { return a >= 0x20000000u && a < 0x40000000u ? a & 0x1FFFFFFFu : a; }

// One pass over the chosen lines of the given places, as PCSX2 makes one at
// the entry point and one every vsync. `count`: record what it did in `r`.
void runPass(const std::vector<std::vector<Line>>& groups, bool vsync, Memory& mem, Result& r, bool count) {
    for (const std::vector<Line>& group : groups) {
        uint32_t skip = 0;              // extended lines a false condition skips
        uint32_t held = 0;              // extended lines a condition that held covers
        const Line* pending = nullptr;  // the first line of a two-line code
        bool pendingGuarded = false;    // ...and whether one covered it
        for (const Line& l : group) {
            const bool now = vsync ? (l.place == 1 || l.place == 2) : (l.place == 0 || l.place == 2 || l.place == 3);
            if (!now) continue;
            bool guarded = false;   // under a condition that held
            auto put = [&](uint32_t addr, const std::vector<uint8_t>& bytes) {
                const bool ok = mem.write(ramAddress(addr), bytes);
                if (!count) return;
                if (ok) r.wrote.push_back(l.where());
                else r.outside.push_back({l.where(), addr, bytes, vsync, guarded});
            };
            switch (l.type) {
            case Type::Byte: put(l.address, littleEndian(l.data, 1)); continue;
            case Type::Short: put(l.address, littleEndian(l.data, 2)); continue;
            case Type::Word: put(l.address, littleEndian(l.data, 4)); continue;
            case Type::Double: put(l.address, littleEndian(l.data, 8)); continue;
            case Type::BeShort: put(l.address, bigEndian(l.data, 2)); continue;
            case Type::BeWord: put(l.address, bigEndian(l.data, 4)); continue;
            case Type::BeDouble: put(l.address, bigEndian(l.data, 8)); continue;
            case Type::Bytes: put(l.address, l.bytes); continue;
            case Type::Extended: break;
            }
            // Extended codes: a false condition skips the extended lines after
            // it, and a two-line code takes the next one, whatever it is.
            if (skip) {
                --skip;
                if (count) ++r.skipped;
                continue;
            }
            guarded = held > 0;
            if (held) --held;
            if (pending) {   // 4aaaaaaa nnnnssss, vvvvvvvv iiiiiiii: n words, s words apart, the value growing by i
                guarded = guarded || pendingGuarded;
                const uint32_t base = pending->address & 0x0FFFFFFF;
                const auto n = static_cast<uint32_t>(pending->data >> 16) & 0xFFFF, step = static_cast<uint32_t>(pending->data) & 0xFFFF;
                uint32_t value = l.address;
                for (uint32_t k = 0; k < n; ++k, value += static_cast<uint32_t>(l.data)) put(base + 4 * step * k, littleEndian(value, 4));
                pending = nullptr;
                continue;
            }
            const uint32_t code = l.address >> 28, addr = l.address & 0x0FFFFFFF;
            if (code <= 2) {
                put(addr, littleEndian(l.data, size_t(1) << code));
                continue;
            }
            if (code == 4) {
                pending = &l;
                pendingGuarded = guarded;
                continue;
            }
            if (code != 0xD && code != 0xE) continue;   // does nothing
            // Daaaaaaa nntzvvvv, and Ezyyvvvv taaaaaaa as the same test: the
            // halfword (z 0) or byte (z 1) at a against v, by t (0 equal, 1
            // not equal, 2 less, 3 greater, 4 none of v's bits, 5 some, 6 the
            // two both zero, 7 either not). False skips the next nn lines (00: one).
            uint32_t at, lines, kind, size;
            uint64_t v;
            if (code == 0xD) {
                at = addr;
                lines = static_cast<uint32_t>(l.data >> 24) & 0xFF;
                kind = static_cast<uint32_t>(l.data >> 20) & 0xF;
                size = static_cast<uint32_t>(l.data >> 16) & 0xF;
                v = l.data & 0xFFFF;
            } else {
                size = (l.address >> 24) & 0xF;
                lines = (l.address >> 16) & 0xFF;
                v = l.address & 0xFFFF;
                kind = static_cast<uint32_t>(l.data >> 28) & 0xF;
                at = static_cast<uint32_t>(l.data) & 0x0FFFFFFF;
            }
            if (size > 1 || kind > 7) continue;   // does nothing, as in PCSX2
            if (size == 1) v &= 0xFF;
            uint64_t m = 0;
            bool pass = false;
            if (!mem.read(ramAddress(at), size == 1 ? 1 : 2, m)) {
                if (count) ++r.unknown;   // memory the game fills at run time: taken as false
            } else {
                switch (kind) {
                case 0: pass = m == v; break;
                case 1: pass = m != v; break;
                case 2: pass = m < v; break;
                case 3: pass = m > v; break;
                case 4: pass = (m & v) == 0; break;
                case 5: pass = (m & v) != 0; break;
                case 6: pass = (m | v) == 0; break;
                default: pass = (m | v) != 0; break;
                }
            }
            if (pass) held = lines ? lines : 1;
            else skip = lines ? lines : 1;
        }
    }
}

} // namespace

Result Patches::apply(const ElfFile& elf, bool boot) const {
    Result r;
    Memory mem(elf);
    if (boot) runPass(groups_, false, mem, r, true);
    // Every vsync PCSX2 writes them again; a condition may hold after a write.
    for (int i = 0; i < 16; ++i) {
        const uint64_t before = mem.changes();
        runPass(groups_, true, mem, r, i == 0);
        if (mem.changes() == before) break;
    }
    r.patches = mem.patches();
    return r;
}

std::vector<std::string> applyToGame(const Patches& patches, const std::vector<std::filesystem::path>& files,
                                     const std::vector<Program>& programs, std::string& summary) {
    std::vector<std::string> warnings = patches.warnings();
    // PCSX2 loads a game's file by the boot executable's checksum.
    for (const Program& p : programs) {
        if (!p.boot) continue;
        const uint32_t crc = pcsx2Crc(p.elf->bytes());
        for (const auto& f : files) {
            const std::string stem = f.stem().string();
            const size_t us = stem.rfind('_');
            if (us == std::string::npos) continue;
            const auto named = parseHex(std::string_view(stem).substr(us + 1), 8);
            if (stem.size() - us - 1 == 8 && named && *named != crc)
                warnings.push_back(strprintf("%s is named for an executable whose PCSX2 checksum is %08X, but %s's is %08X: "
                                             "is it for another version of the game?",
                                             f.filename().string().c_str(), static_cast<uint32_t>(*named),
                                             p.cfg->elf.filename().string().c_str(), crc));
        }
    }
    std::set<std::string> wrote;
    std::vector<Outside> missed, bootMissed;
    Config* bootCfg = nullptr;
    summary = patches.describe() + ":";
    for (size_t i = 0; i < programs.size(); ++i) {
        const Program& p = programs[i];
        const Result r = patches.apply(*p.elf, p.boot);
        for (const Patch& w : r.patches) {
            for (const Patch& c : p.cfg->patches)
                if (c.address == w.address)
                    throw Error(strprintf("[pnach] and [[patches]] both patch the word at 0x%08x in %s", w.address,
                                          p.cfg->elf.filename().string().c_str()));
            p.cfg->patches.push_back(w);
        }
        wrote.insert(r.wrote.begin(), r.wrote.end());
        missed.insert(missed.end(), r.outside.begin(), r.outside.end());
        if (p.boot) {
            bootCfg = p.cfg;
            bootMissed = r.outside;
        }
        summary += strprintf("%s %s %zu word%s", i ? "," : "", p.cfg->elf.filename().string().c_str(), r.patches.size(),
                             r.patches.size() == 1 ? "" : "s");
        if (r.unknown)
            summary += strprintf(" (%zu condition%s on memory it does not initialise)", r.unknown, r.unknown == 1 ? "" : "s");
    }
    // A line that wrote to no executable writes memory the game fills at run
    // time (BSS, the heap): the runtime writes it while the boot executable
    // runs, as PCSX2 does while the file is loaded for it. A write made under
    // a condition is left out: the condition held for one executable's image,
    // and only a test at run time, which the runtime does not make, would say
    // when it holds.
    std::set<std::string> applied, warned;
    std::vector<std::pair<std::string, MemoryPatch>> runtime;   // each line's write, the passes merged
    for (const Outside& o : bootMissed) {
        if (wrote.count(o.where) || o.guarded) continue;
        applied.insert(o.where);
        for (size_t at = 0; at < o.bytes.size(); at += 8) {
            MemoryPatch m;
            m.address = o.address + static_cast<uint32_t>(at);
            m.size = static_cast<uint32_t>(std::min<size_t>(8, o.bytes.size() - at));
            for (uint32_t k = 0; k < m.size; ++k) m.value |= uint64_t(o.bytes[at + k]) << (8 * k);
            auto same = std::find_if(runtime.begin(), runtime.end(), [&](const auto& e) {
                return e.first == o.where && e.second.address == m.address && e.second.size == m.size && e.second.value == m.value;
            });
            if (same == runtime.end()) same = runtime.emplace(runtime.end(), o.where, m);
            (o.vsync ? same->second.everyVsync : same->second.atEntry) = true;
        }
    }
    if (bootCfg)
        for (const auto& [where, m] : runtime) bootCfg->memoryPatches.push_back(m);
    if (!runtime.empty())
        summary += strprintf(", %zu write%s to memory the game fills at run time, made by the runtime", runtime.size(),
                             runtime.size() == 1 ? "" : "s");
    for (const Outside& o : missed)
        if (!wrote.count(o.where) && !applied.count(o.where) && warned.insert(o.where).second)
            warnings.push_back(strprintf("%s writes to 0x%08x, which no executable of the game initialises, %s: not applied",
                                         o.where.c_str(), o.address,
                                         o.guarded ? "under a condition (the runtime makes no tests)"
                                                   : "in another executable than the boot one (the runtime writes only while that runs)"));
    return warnings;
}

} // namespace ps2n::pnach
