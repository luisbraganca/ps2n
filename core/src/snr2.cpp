// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps2n/snr2.h"

#include "ps2n/util.h"

#include <algorithm>
#include <cstring>

namespace ps2n {

namespace {

constexpr uint32_t kMagic = 0x32524E53u;

uint32_t word(const std::vector<uint8_t>& bytes, size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) throw Error("SNR2: truncated file");
    uint32_t v;
    std::memcpy(&v, bytes.data() + at, 4);
    return v;
}

void checkTable(const std::vector<uint8_t>& bytes, uint32_t at, uint32_t count, uint32_t size) {
    if (count > (bytes.size() - std::min<size_t>(at, bytes.size())) / 12 || at > bytes.size() ||
        static_cast<uint64_t>(at) + static_cast<uint64_t>(count) * 12 > size)
        throw Error("SNR2: table points outside the module");
}

std::string symbolName(const std::vector<uint8_t>& bytes, uint32_t at) {
    if (at >= bytes.size()) throw Error("SNR2: symbol name points outside the module");
    const auto end = std::find(bytes.begin() + at, bytes.end(), 0);
    if (end == bytes.end()) throw Error("SNR2: unterminated symbol name");
    return std::string(bytes.begin() + at, end);
}

} // namespace

Snr2Module Snr2Module::load(const std::filesystem::path& path) {
    return parse(readFile(path), path.filename().string());
}

Snr2Module Snr2Module::parse(std::vector<uint8_t> bytes, std::string fileName) {
    if (bytes.size() < 0x38 || word(bytes, 0) != kMagic) throw Error(fileName + ": not an SNR2 module");
    Snr2Module m;
    m.name = std::move(fileName);
    const uint32_t relocations = word(bytes, 0x04), relocationCount = word(bytes, 0x08);
    m.symbolTable = word(bytes, 0x0C);
    m.symbolCount = word(bytes, 0x10);
    const uint32_t imageName = word(bytes, 0x14), stream = word(bytes, 0x34);
    m.size = word(bytes, 0x2C);
    if (m.size < 0x38 || m.size > bytes.size()) throw Error(m.name + ": invalid SNR2 module size");
    bytes.resize(m.size);
    checkTable(bytes, relocations, relocationCount, m.size);
    checkTable(bytes, m.symbolTable, m.symbolCount, m.size);
    if (stream && stream >= m.size) throw Error(m.name + ": relocation stream points outside the module");
    m.crc = crc32(bytes.data(), bytes.size());

    for (uint32_t i = 0; i < m.symbolCount; ++i) {
        const uint32_t at = m.symbolTable + 12 * i;
        ElfSymbol s;
        s.name = symbolName(bytes, word(bytes, at));
        s.value = word(bytes, at + 4);
        const uint32_t info = word(bytes, at + 8);
        s.type = static_cast<uint8_t>((info >> 16) & 15);
        s.bind = elf::STB_GLOBAL;
        m.symbols.push_back(std::move(s));
    }

    m.codeStart = 0x40;
    while (m.codeStart + 4 <= m.size && word(bytes, m.codeStart) == 0) m.codeStart += 4;
    m.codeEnd = m.size;
    for (uint32_t at : {relocations, m.symbolTable, stream, imageName})
        if (at > m.codeStart) m.codeEnd = std::min(m.codeEnd, at);
    m.codeEnd &= ~3u;
    if (m.codeStart >= m.codeEnd) throw Error(m.name + ": no aligned code range in SNR2 module");
    for (uint32_t at : {word(bytes, 0x18), word(bytes, 0x1C)})
        if (at) m.entryPoints.push_back(at);

    struct Site {
        uint32_t type, offset;
    };
    std::vector<Site> sites;
    for (uint32_t p = stream, at = 0; stream && p < m.size;) {
        const uint8_t b = bytes[p++];
        if (b & 0xFC) {
            at += b & 0xFC;
        } else {
            const uint32_t absolute = word(bytes, p);
            p += 4;
            if (!absolute) break;
            at = absolute;
        }
        if (at > m.size - 4) throw Error(m.name + ": internal relocation points outside the module");
        sites.push_back({b & 3u, at});
    }
    for (size_t i = 0; i < sites.size(); ++i) {
        const auto [kind, at] = sites[i];
        Snr2Link link;
        link.offset = at;
        if (kind == 0) {
            link.type = elf::R_MIPS_LO16;
            link.value = word(bytes, at) & 0xFFFF;
        } else if (kind == 1) {
            const auto low = std::find_if(sites.begin() + static_cast<std::ptrdiff_t>(i + 1), sites.end(),
                                          [](const Site& site) { return site.type == 0; });
            if (low == sites.end()) throw Error(m.name + ": SNR2 high relocation has no following low relocation");
            link.type = elf::R_MIPS_HI16;
            link.value = ((word(bytes, at) & 0xFFFF) << 16) +
                         static_cast<uint32_t>(static_cast<int16_t>(word(bytes, low->offset) & 0xFFFF));
        } else if (kind == 2) {
            link.type = elf::R_MIPS_26;
            link.value = (word(bytes, at) & 0x03FFFFFFu) << 2;
        } else {
            link.type = elf::R_MIPS_32;
            link.value = word(bytes, at);
        }
        m.links.push_back(link);
    }

    for (uint32_t i = 0; i < relocationCount; ++i) {
        const uint32_t at = relocations + 12 * i;
        const uint32_t info = word(bytes, at + 4), symbol = info >> 8;
        if (symbol >= m.symbolCount) throw Error(m.name + ": external relocation has an invalid symbol index");
        Snr2Link link;
        link.offset = word(bytes, at);
        link.type = info & 0xFF;
        link.symbol = symbol;
        link.addend = word(bytes, at + 8);
        if (link.offset > m.size - 4) throw Error(m.name + ": external relocation points outside the module");
        m.links.push_back(link);
    }
    std::sort(m.links.begin(), m.links.end(), [](const Snr2Link& a, const Snr2Link& b) { return a.offset < b.offset; });
    for (size_t i = 1; i < m.links.size(); ++i)
        if (m.links[i - 1].offset == m.links[i].offset) throw Error(m.name + ": duplicate SNR2 relocation sites");
    m.bytes = std::move(bytes);
    return m;
}

ElfFile Snr2Module::image() const {
    ElfFile image;
    image.name_ = name;
    image.bytes_ = bytes;
    image.entry = entryPoints.empty() ? codeStart : entryPoints.front();
    image.segments.push_back({elf::PT_LOAD, 0, 0, 0, size, size, elf::PF_R | elf::PF_W | elf::PF_X, 4});
    image.sections.push_back({".text", elf::SHT_PROGBITS, elf::SHF_ALLOC | elf::SHF_EXECINSTR, codeStart, codeStart,
                              codeEnd - codeStart, 0, 0, 4, 0});
    image.sections.push_back({".data", elf::SHT_PROGBITS, elf::SHF_ALLOC | elf::SHF_WRITE, codeEnd, codeEnd,
                              size - codeEnd, 0, 0, 4, 0});
    image.codeRanges_.push_back({codeStart, codeEnd, ".text"});
    image.dataRanges_.push_back({codeEnd, size, ".data"});
    image.symbols = symbols;
    for (const Snr2Link& link : links) {
        if (link.external()) continue;
        uint32_t value = word(image.bytes_, link.offset);
        if (link.type == elf::R_MIPS_HI16) value = (value & 0xFFFF0000u) | ((link.value + 0x8000u) >> 16);
        else if (link.type == elf::R_MIPS_LO16) value = (value & 0xFFFF0000u) | (link.value & 0xFFFF);
        std::memcpy(image.bytes_.data() + link.offset, &value, 4);
    }
    return image;
}

} // namespace ps2n
