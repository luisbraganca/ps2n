// PCSX2 patch files (.pnach): reading them, and applying their EE patches to
// an executable's image at recompile time ([pnach] in the config; see
// docs/config.md). Written from PCSX2's documented behaviour, none of its code.
#pragma once

#include "config.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ps2n {

class ElfFile;

namespace pnach {

enum class Type { Byte, Short, Word, Double, Extended, BeShort, BeWord, BeDouble, Bytes };

// One `patch=place,cpu,address,type,data` line.
struct Line {
    std::filesystem::path file;
    int lineNo = 0;
    int place = 1;                 // 0: at the entry point, 1: every vsync, 2: both, 3: as 0 (and when enabled)
    bool ee = true;                // the cpu field: EE or IOP
    uint32_t address = 0;          // for Extended: the code, in the top digit, and its address
    Type type = Type::Word;
    uint64_t data = 0;             // the value; unused for Bytes
    std::vector<uint8_t> bytes;    // Bytes: the bytes to write, in order
    std::string where() const;     // "file.pnach:12"
};

struct Group {
    std::string name;              // empty for the lines before the first [group]
    std::vector<Line> lines;
    std::vector<int> dpatches;     // the lines of its dpatch= entries
    std::string aspectRatio;       // gsaspectratio=, if given
    std::string interlaceMode;     // gsinterlacemode=, if given
    std::vector<std::string> malformed;   // lines PCSX2 skips as malformed: "file:line: why"
};

struct File {
    std::filesystem::path path;
    std::vector<Group> groups;     // groups[0]: the lines before the first [group]
};

File parse(std::string_view text, const std::filesystem::path& path);
File load(const std::filesystem::path& path);

// What applying the chosen lines did to one executable.
struct Result {
    std::vector<Patch> patches;    // the words they change, each with the ELF's word as expect
    size_t skipped = 0;            // extended lines a false condition skipped
    size_t unknown = 0;            // conditions on memory the ELF does not initialise (taken as false)
    std::vector<std::string> wrote;                          // Line::where() of each line that wrote
    std::vector<std::pair<std::string, uint32_t>> outside;   // a write to memory the ELF does not initialise
};

// The lines a config chooses: every file's lines before its first group, and
// the groups named in [pnach] groups, in file order.
class Patches {
public:
    // Throws Error for a group none of the files has, and for a chosen line
    // PCSX2 would apply but this reader cannot.
    static Patches load(const std::vector<std::filesystem::path>& files, const std::vector<std::string>& groups);

    // The aspect ratio the chosen groups ask for ("16:9": the last valid
    // gsaspectratio), or "".
    const std::string& aspectRatio() const { return aspectRatio_; }
    // What PCSX2 would skip or ignore too: malformed lines, unknown codes.
    const std::vector<std::string>& warnings() const { return warnings_; }
    // "SLES-50539_992B46DC.pnach [Widescreen 16:9]", for messages.
    std::string describe() const;

    // Applies the lines to `elf` as PCSX2 writes them to memory while that
    // executable runs: places 0, 2 and 3 once at the entry point, when `boot`
    // (the executable PCSX2 loaded the file for), then places 1 and 2 as at
    // every vsync, until memory stops changing.
    Result apply(const ElfFile& elf, bool boot) const;

private:
    std::vector<std::vector<Line>> groups_;   // each chosen group's lines: extended codes start afresh in each
    std::vector<std::string> chosen_;
    std::vector<std::string> warnings_;
    std::string aspectRatio_;
};

// One executable of the game and its config, whose [[patches]] get the words
// the pnach lines change.
struct Program {
    const ElfFile* elf;
    Config* cfg;
    bool boot;
};

// Applies the patches to every executable of the game, adding what they
// change to each config's patches. Throws Error where a word is patched by
// both. Returns the warnings: a file named for another executable than the
// boot one, and lines that wrote to no executable's initialised memory.
std::vector<std::string> applyToGame(const Patches& patches, const std::vector<std::filesystem::path>& files,
                                     const std::vector<Program>& programs, std::string& summary);

// PCSX2's name for an executable's checksum in <SERIAL>_<CRC>.pnach: the
// exclusive or of the file's 32-bit words.
uint32_t pcsx2Crc(const std::vector<uint8_t>& elfBytes);

} // namespace pnach
} // namespace ps2n
