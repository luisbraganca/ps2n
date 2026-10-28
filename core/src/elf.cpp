#include "ps2n/elf.h"
#include "ps2n/util.h"

#include <algorithm>
#include <cstring>

namespace ps2n {

namespace {

uint16_t rd16(const std::vector<uint8_t>& b, size_t off) {
    if (off + 2 > b.size()) throw Error("ELF: truncated file");
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

uint32_t rd32(const std::vector<uint8_t>& b, size_t off) {
    if (off + 4 > b.size()) throw Error("ELF: truncated file");
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

std::string strAt(const std::vector<uint8_t>& b, size_t off) {
    std::string s;
    while (off < b.size() && b[off] != 0) s.push_back(static_cast<char>(b[off++]));
    return s;
}

} // namespace

ElfFile ElfFile::load(const std::filesystem::path& path) {
    return parse(readFile(path), path.filename().string());
}

ElfFile ElfFile::parse(std::vector<uint8_t> bytes, std::string displayName) {
    ElfFile f;
    f.name_ = std::move(displayName);
    f.bytes_ = std::move(bytes);
    f.parseAll();
    return f;
}

void ElfFile::parseAll() {
    const auto& b = bytes_;
    if (b.size() < 52 || std::memcmp(b.data(), "\x7f" "ELF", 4) != 0)
        throw Error(name_ + ": not an ELF file");
    if (b[4] != 1) throw Error(name_ + ": only 32-bit ELF files are supported (PS2 executables are ELF32)");
    if (b[5] != 1) throw Error(name_ + ": not little-endian; PS2 executables are little-endian MIPS");

    type = rd16(b, 16);
    machine = rd16(b, 18);
    entry = rd32(b, 24);
    const uint32_t phoff = rd32(b, 28);
    const uint32_t shoff = rd32(b, 32);
    flags = rd32(b, 36);
    const uint16_t phentsize = rd16(b, 42);
    const uint16_t phnum = rd16(b, 44);
    const uint16_t shentsize = rd16(b, 46);
    const uint16_t shnum = rd16(b, 48);
    const uint16_t shstrndx = rd16(b, 50);
    if (machine != elf::EM_MIPS) throw Error(name_ + ": not a MIPS ELF (e_machine=" + std::to_string(machine) + ")");

    for (uint32_t i = 0; i < phnum && phentsize >= 32; ++i) {
        const size_t o = phoff + static_cast<size_t>(i) * phentsize;
        ElfSegment s;
        s.type = rd32(b, o);
        s.offset = rd32(b, o + 4);
        s.vaddr = rd32(b, o + 8);
        s.paddr = rd32(b, o + 12);
        s.filesz = rd32(b, o + 16);
        s.memsz = rd32(b, o + 20);
        s.flags = rd32(b, o + 24);
        s.align = rd32(b, o + 28);
        if (s.type == elf::PT_LOAD && static_cast<uint64_t>(s.offset) + s.filesz > b.size())
            throw Error(name_ + ": program header points past the end of the file");
        segments.push_back(s);
    }

    if (shoff != 0 && shnum != 0 && shentsize >= 40) {
        for (uint32_t i = 0; i < shnum; ++i) {
            const size_t o = shoff + static_cast<size_t>(i) * shentsize;
            ElfSection s;
            s.name = std::to_string(rd32(b, o));   // resolved below
            s.type = rd32(b, o + 4);
            s.flags = rd32(b, o + 8);
            s.addr = rd32(b, o + 12);
            s.offset = rd32(b, o + 16);
            s.size = rd32(b, o + 20);
            s.link = rd32(b, o + 24);
            s.info = rd32(b, o + 28);
            s.align = rd32(b, o + 32);
            s.entsize = rd32(b, o + 36);
            sections.push_back(s);
        }
        const size_t strOff = shstrndx < sections.size() ? sections[shstrndx].offset : 0;
        for (auto& s : sections) {
            const auto nameOff = static_cast<size_t>(std::stoul(s.name));
            s.name = strOff ? strAt(b, strOff + nameOff) : std::string();
        }

        for (size_t si = 0; si < sections.size(); ++si) {
            const ElfSection& sec = sections[si];
            if ((sec.type == elf::SHT_SYMTAB || sec.type == elf::SHT_DYNSYM) && sec.link < sections.size()) {
                const ElfSection& strs = sections[sec.link];
                const uint32_t n = sec.entsize ? sec.size / sec.entsize : sec.size / 16;
                for (uint32_t i = 0; i < n; ++i) {
                    const size_t o = sec.offset + static_cast<size_t>(i) * 16;
                    ElfSymbol sym;
                    sym.name = strAt(b, strs.offset + rd32(b, o));
                    sym.value = rd32(b, o + 4);
                    sym.size = rd32(b, o + 8);
                    const uint8_t info = b.at(o + 12);
                    sym.type = info & 15;
                    sym.bind = info >> 4;
                    sym.other = b.at(o + 13);
                    sym.shndx = rd16(b, o + 14);
                    symbols.push_back(std::move(sym));
                }
            } else if (sec.type == elf::SHT_REL) {
                const uint32_t n = sec.size / 8;
                for (uint32_t i = 0; i < n; ++i) {
                    const size_t o = sec.offset + static_cast<size_t>(i) * 8;
                    ElfRelocation r;
                    r.offset = rd32(b, o);
                    const uint32_t info = rd32(b, o + 4);
                    r.type = info & 0xFF;
                    r.symbol = info >> 8;
                    r.section = static_cast<uint16_t>(sec.info);
                    relocations.push_back(r);
                }
            }
        }
    }

    // Executable ranges: section headers are precise; fall back to segments.
    for (const auto& s : sections)
        if (s.alloc() && s.executable() && !s.vuMicrocode() && s.size && !s.nobits())
            codeRanges_.push_back({s.addr, s.addr + s.size, s.name});
    if (codeRanges_.empty()) {
        for (const auto& s : segments)
            if (s.type == elf::PT_LOAD && s.executable() && s.filesz)
                codeRanges_.push_back({s.vaddr, s.vaddr + s.filesz, "segment@" + hex32(s.vaddr)});
    }
    std::sort(codeRanges_.begin(), codeRanges_.end(), [](const AddrRange& a, const AddrRange& b) { return a.start < b.start; });
}

const ElfSegment* ElfFile::segmentFor(uint32_t vaddr) const {
    for (const auto& s : segments)
        if (s.type == elf::PT_LOAD && vaddr >= s.vaddr && vaddr - s.vaddr < s.memsz) return &s;
    return nullptr;
}

bool ElfFile::isMapped(uint32_t vaddr) const { return segmentFor(vaddr) != nullptr; }

bool ElfFile::read8(uint32_t vaddr, uint8_t& out) const {
    const ElfSegment* s = segmentFor(vaddr);
    if (!s) return false;
    const uint32_t off = vaddr - s->vaddr;
    out = off < s->filesz ? bytes_[s->offset + off] : 0;
    return true;
}

bool ElfFile::read32(uint32_t vaddr, uint32_t& out) const {
    const ElfSegment* s = segmentFor(vaddr);
    if (!s || vaddr - s->vaddr + 4 > s->memsz) return false;
    const uint32_t off = vaddr - s->vaddr;
    if (off + 4 <= s->filesz) {
        out = rd32(bytes_, s->offset + off);
    } else {
        out = 0;
        for (uint32_t i = 0; i < 4; ++i)
            if (off + i < s->filesz) out |= static_cast<uint32_t>(bytes_[s->offset + off + i]) << (8 * i);
    }
    return true;
}

std::optional<std::string> ElfFile::readCString(uint32_t vaddr, size_t maxLen) const {
    std::string s;
    for (size_t i = 0; i < maxLen; ++i) {
        uint8_t c;
        if (!read8(vaddr + static_cast<uint32_t>(i), c)) return std::nullopt;
        if (c == 0) return s;
        s.push_back(static_cast<char>(c));
    }
    return std::nullopt;
}

std::vector<AddrRange> ElfFile::codeRanges() const { return codeRanges_; }

std::vector<AddrRange> ElfFile::dataRanges() const {
    std::vector<AddrRange> out;
    bool fromSections = false;
    for (const auto& s : sections) {
        if (!s.alloc() || (s.executable() && !s.vuMicrocode()) || s.nobits() || s.size == 0) continue;
        out.push_back({s.addr, s.addr + s.size, s.name});
        fromSections = true;
    }
    if (!fromSections) {
        for (const auto& s : segments)
            if (s.type == elf::PT_LOAD && !s.executable() && s.filesz)
                out.push_back({s.vaddr, s.vaddr + s.filesz, "segment@" + hex32(s.vaddr)});
    }
    return out;
}

bool ElfFile::isCode(uint32_t vaddr) const {
    for (const auto& r : codeRanges_)
        if (r.contains(vaddr)) return true;
    return false;
}

const ElfSection* ElfFile::sectionByName(const std::string& n) const {
    for (const auto& s : sections)
        if (s.name == n) return &s;
    return nullptr;
}

const ElfSection* ElfFile::sectionContaining(uint32_t vaddr) const {
    for (const auto& s : sections)
        if (s.alloc() && s.contains(vaddr)) return &s;
    return nullptr;
}

std::optional<uint32_t> ElfFile::symbolValue(const std::string& n) const {
    for (const auto& s : symbols)
        if (s.name == n) return s.value;
    return std::nullopt;
}

std::optional<uint32_t> ElfFile::gpValue() const {
    if (auto v = symbolValue("_gp")) return v;
    for (const auto& s : sections)
        if (s.type == elf::SHT_MIPS_REGINFO && s.size >= 24) return rd32(bytes_, s.offset + 20);
    return std::nullopt;
}

uint32_t ElfFile::crc32() const { return ps2n::crc32(bytes_.data(), bytes_.size()); }

} // namespace ps2n
