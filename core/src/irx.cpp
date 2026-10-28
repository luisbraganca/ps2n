#include "ps2uc/irx.h"
#include "ps2uc/util.h"

namespace ps2uc {

namespace {

constexpr uint32_t kImportMagic = 0x41E00000u;
constexpr uint32_t kExportMagic = 0x41C00000u;
constexpr uint32_t kJrRa = 0x03E00008u;

struct KnownExport {
    const char* library;
    uint32_t index;
    const char* name;
};
constexpr KnownExport kKnownExports[] = {
#include "irx_exports.inc"
};

std::string libraryName(const ElfFile& elf, uint32_t addr) {
    std::string s;
    for (uint32_t i = 0; i < 8; ++i) {
        uint8_t c = 0;
        if (!elf.read8(addr + i, c) || c == 0) break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

} // namespace

const char* irxKnownExportName(std::string_view library, uint32_t index) {
    for (const auto& e : kKnownExports)
        if (e.index == index && library == e.library) return e.name;
    return nullptr;
}

IrxInfo parseIrx(const ElfFile& elf) {
    IrxInfo info;
    const ElfSection* iopmod = nullptr;
    for (const auto& s : elf.sections)
        if (s.type == elf::SHT_SCE_IOPMOD) iopmod = &s;

    const auto& b = elf.bytes();
    size_t off = 0;
    if (iopmod) {
        off = iopmod->offset;
    } else {
        for (const auto& s : elf.segments)
            if (s.type == elf::PT_SCE_IOPMOD) off = s.offset;
    }
    if (off == 0 && !elf.isIrx()) throw Error(elf.name() + ": not an IRX module (no .iopmod section)");
    if (off != 0) {
        auto r32 = [&](size_t o) -> uint32_t {
            if (o + 4 > b.size()) throw Error(elf.name() + ": truncated .iopmod");
            return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
        };
        info.moduleInfo = r32(off);
        info.entry = r32(off + 4);
        info.gp = r32(off + 8);
        info.textSize = r32(off + 12);
        info.dataSize = r32(off + 16);
        info.bssSize = r32(off + 20);
        info.version = static_cast<uint16_t>(b.at(off + 24) | (b.at(off + 25) << 8));
        for (size_t i = off + 26; i < b.size() && b[i]; ++i) info.name.push_back(static_cast<char>(b[i]));
        // ps2sdk leaves the header's name empty; `moduleinfo` points to its
        // IRX_ID {const char *name; u16 version} in the image instead.
        uint32_t namePtr = 0;
        if (info.name.empty() && info.moduleInfo != 0xFFFFFFFFu && elf.read32(info.moduleInfo, namePtr))
            info.name = elf.readCString(namePtr, 64).value_or("");
    } else {
        info.entry = elf.entry;
    }

    // Import / export tables live in .text; scan it word by word.
    for (const auto& range : elf.codeRanges()) {
        for (uint32_t a = range.start; a + 20 <= range.end; a += 4) {
            uint32_t w = 0;
            if (!elf.read32(a, w)) break;
            if (w != kImportMagic && w != kExportMagic) continue;
            uint32_t next = 0, verMode = 0;
            elf.read32(a + 4, next);
            elf.read32(a + 8, verMode);
            if (next != 0) continue;
            const std::string lib = libraryName(elf, a + 12);
            if (lib.empty()) continue;
            const auto version = static_cast<uint16_t>(verMode & 0xFFFF);
            if (w == kImportMagic) {
                uint32_t p = a + 20;
                for (;;) {
                    uint32_t i0 = 0, i1 = 0;
                    if (!elf.read32(p, i0) || !elf.read32(p + 4, i1)) break;
                    if (i0 == 0 && i1 == 0) break;
                    if (i0 != kJrRa || (i1 & 0xFFFF0000u) != 0x24000000u) break;
                    IrxImport imp;
                    imp.library = lib;
                    imp.version = version;
                    imp.stubAddr = p;
                    imp.index = i1 & 0xFFFF;
                    if (const char* n = irxKnownExportName(lib, imp.index)) imp.name = n;
                    info.imports.push_back(std::move(imp));
                    p += 8;
                }
                a = p;
            } else {
                IrxExportTable t;
                t.library = lib;
                t.version = version;
                t.tableAddr = a;
                uint32_t p = a + 20;
                for (uint32_t f = 0; elf.read32(p, f) && f != 0; p += 4) t.functions.push_back(f);
                info.exports.push_back(std::move(t));
                a = p;
            }
        }
    }
    return info;
}

} // namespace ps2uc
