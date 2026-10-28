// Minimal ELF32 little-endian reader for PS2 executables (EE ELFs) and IOP
// modules (IRX, e_type 0xFF80). Only what static recompilation needs: program
// headers for loading, section headers and symbols for analysis.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ps2uc {

namespace elf {
constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t ET_SCE_IOPRELEXEC = 0xFF80;   // IRX module
constexpr uint16_t ET_SCE_EERELEXEC = 0xFF91;    // ERX module
constexpr uint16_t EM_MIPS = 8;

constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t PT_SCE_IOPMOD = 0x70000080;
constexpr uint32_t PF_X = 1;
constexpr uint32_t PF_W = 2;
constexpr uint32_t PF_R = 4;

constexpr uint32_t SHT_PROGBITS = 1;
constexpr uint32_t SHT_SYMTAB = 2;
constexpr uint32_t SHT_STRTAB = 3;
constexpr uint32_t SHT_NOBITS = 8;
constexpr uint32_t SHT_REL = 9;
constexpr uint32_t SHT_DYNSYM = 11;
constexpr uint32_t SHT_MIPS_REGINFO = 0x70000006;
constexpr uint32_t SHT_SCE_IOPMOD = 0x70000080;

constexpr uint32_t SHF_WRITE = 1;
constexpr uint32_t SHF_ALLOC = 2;
constexpr uint32_t SHF_EXECINSTR = 4;

constexpr uint8_t STT_NOTYPE = 0;
constexpr uint8_t STT_OBJECT = 1;
constexpr uint8_t STT_FUNC = 2;
constexpr uint8_t STT_SECTION = 3;
constexpr uint8_t STT_FILE = 4;
constexpr uint8_t STB_LOCAL = 0;
constexpr uint8_t STB_GLOBAL = 1;
constexpr uint8_t STB_WEAK = 2;

constexpr uint32_t R_MIPS_32 = 2;
constexpr uint32_t R_MIPS_26 = 4;
constexpr uint32_t R_MIPS_HI16 = 5;
constexpr uint32_t R_MIPS_LO16 = 6;
} // namespace elf

struct ElfSection {
    std::string name;
    uint32_t type = 0, flags = 0, addr = 0, offset = 0, size = 0, link = 0, info = 0, align = 0, entsize = 0;
    bool alloc() const { return flags & elf::SHF_ALLOC; }
    bool executable() const { return flags & elf::SHF_EXECINSTR; }
    bool nobits() const { return type == elf::SHT_NOBITS; }
    bool contains(uint32_t a) const { return a >= addr && a - addr < size; }
    // VU micro code (dvp-as puts it in .vutext, flagged executable): data to the EE.
    bool vuMicrocode() const { return name == ".vutext" || name.starts_with(".vutext."); }
};

struct ElfSegment {
    uint32_t type = 0, offset = 0, vaddr = 0, paddr = 0, filesz = 0, memsz = 0, flags = 0, align = 0;
    bool executable() const { return flags & elf::PF_X; }
};

struct ElfSymbol {
    std::string name;
    uint32_t value = 0, size = 0;
    uint8_t type = 0, bind = 0, other = 0;
    uint16_t shndx = 0;
    bool isFunction() const { return type == elf::STT_FUNC; }
};

struct ElfRelocation {
    uint32_t offset = 0;      // address being patched (section-relative for IRX)
    uint32_t type = 0;
    uint32_t symbol = 0;
    uint16_t section = 0;     // index of the section the relocation applies to
};

// Address range in the loaded image.
struct AddrRange {
    uint32_t start = 0;
    uint32_t end = 0;         // exclusive
    std::string name;
    bool contains(uint32_t a) const { return a >= start && a < end; }
    uint32_t size() const { return end - start; }
};

class ElfFile {
public:
    static ElfFile load(const std::filesystem::path& path);
    static ElfFile parse(std::vector<uint8_t> bytes, std::string displayName);

    const std::string& name() const { return name_; }
    const std::vector<uint8_t>& bytes() const { return bytes_; }

    uint16_t type = 0;
    uint16_t machine = 0;
    uint32_t entry = 0;
    uint32_t flags = 0;
    std::vector<ElfSection> sections;
    std::vector<ElfSegment> segments;
    std::vector<ElfSymbol> symbols;
    std::vector<ElfRelocation> relocations;

    bool isIrx() const { return type == elf::ET_SCE_IOPRELEXEC; }

    // Reads from the loaded image (PT_LOAD contents; memsz beyond filesz
    // reads as zero). Returns false outside every segment.
    bool read32(uint32_t vaddr, uint32_t& out) const;
    bool read8(uint32_t vaddr, uint8_t& out) const;
    std::optional<std::string> readCString(uint32_t vaddr, size_t maxLen = 256) const;
    bool isMapped(uint32_t vaddr) const;

    // Executable address ranges, from section headers when present (more
    // precise), else from PF_X segments.
    std::vector<AddrRange> codeRanges() const;
    // Allocated, non-executable ranges that hold initialised data.
    std::vector<AddrRange> dataRanges() const;
    bool isCode(uint32_t vaddr) const;

    const ElfSection* sectionByName(const std::string& n) const;
    const ElfSection* sectionContaining(uint32_t vaddr) const;
    std::optional<uint32_t> symbolValue(const std::string& n) const;
    // Value of _gp (from the symbol, or the .reginfo section).
    std::optional<uint32_t> gpValue() const;

    uint32_t crc32() const;

private:
    std::string name_;
    std::vector<uint8_t> bytes_;
    std::vector<AddrRange> codeRanges_;
    void parseAll();
    const ElfSegment* segmentFor(uint32_t vaddr) const;
};

} // namespace ps2uc
