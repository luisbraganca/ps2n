// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps2n-recomp: the static recompiler's command line.

#include "analysis.h"
#include "bios.h"
#include "codegen.h"

#include "ps2n/assembler.h"
#include "ps2n/elf.h"
#include "ps2n/util.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace ps2n;
using namespace ps2n::recomp;

namespace {

void usage() {
    std::puts(
        "ps2n-recomp: PS2Native's static recompiler for EE executables\n"
        "\n"
        "  ps2n-recomp compile <elf> --out <file.cpp> [options]\n"
        "      Translates the executable's code into C++ against ps2n/recomp.h.\n"
        "      --id <name>        names the generated namespace, ps2n_image_<name>\n"
        "                         (default: made from the file name)\n"
        "      --capture <file>   what an earlier run reported (the host's -capture\n"
        "                         file): block entry points the analysis missed, and\n"
        "                         code found in memory in the place of a function.\n"
        "                         May be given more than once\n"
        "      --no-symbols       ignore the executable's symbol table\n"
        "      --no-comments      leave the disassembly out of the output\n"
        "      --parts <n>        for a program too large to compile as one file:\n"
        "                         spread the functions over n files beside the\n"
        "                         output, <name>_part001.cpp and so on, with a\n"
        "                         header <name>.h; the output then holds the tables\n"
        "      --outside <file.cpp>  also write the image <id>_outside: the code the\n"
        "                         captures hold outside the executable's code (code it\n"
        "                         copied into kernel memory, for one), followed from\n"
        "                         where it ran. It may hold nothing\n"
        "\n"
        "  ps2n-recomp info <elf> [--capture <file>] [--no-symbols]\n"
        "      Prints what the analysis finds, without writing anything.\n"
        "\n"
        "  ps2n-recomp bios <BIOS image> --out-dir <dir> [--capture <file>]... [--no-comments]\n"
        "      Translates the BIOS's EE code: the files of the ROM the EE runs, each\n"
        "      where it runs it, as four images: bios_rom0 and bios_rom1 (the reset\n"
        "      code and the set-up of main memory, in the ROM, through its cached\n"
        "      mirror and at BFC00000), bios_kernel (the kernel) and bios_loader\n"
        "      (the idle thread and the program loader). Writes <dir>/<id>.cpp for\n"
        "      each. The code is followed from where it is entered, not swept, and\n"
        "      --capture gives the places runs entered it (the host's -bios-capture\n"
        "      files). What this writes is derived from the BIOS image: it belongs\n"
        "      in the build tree, never in the repository.\n"
        "\n"
        "  ps2n-recomp bios-info <BIOS image> [--capture <file>]...\n"
        "      Prints what the analysis finds in each of the BIOS's images, and how\n"
        "      many of the captured entries it finds without being given them.\n"
        "\n"
        "  ps2n-recomp capture --out <file> <capture>...\n"
        "      Puts what several runs reported (the host's -capture files) into one\n"
        "      file, each address and each piece of code once. The output may be one\n"
        "      of the inputs: a program that is rebuilt after every run then keeps a\n"
        "      capture that only grows.\n"
        "\n"
        "  ps2n-recomp registry --out <file.cpp> <id>...\n"
        "      Writes the list of images a host is linked with.\n"
        "\n"
        "  ps2n-recomp assemble <file.s> --out <file.elf>\n"
        "      Assembles a test program written for PS2Native's own small R5900\n"
        "      assembler (core/include/ps2n/assembler.h) into an EE executable.\n"
        "\n"
        "  ps2n-recomp disasm <file> --at <address> [--count <n>] [--base <address>]\n"
        "      Prints the instructions at an address: the address, the word, what\n"
        "      it is counted as costing in eighths of a cycle, the instruction.\n"
        "      The file is an executable, or with --base any image whose first\n"
        "      byte belongs at that address (a BIOS image: --base 0xBFC00000).");
}

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::filesystem::path out;
    std::filesystem::path outDir;
    std::filesystem::path outside;
    std::string id;
    std::vector<std::filesystem::path> captures;
    bool symbols = true;
    bool comments = true;
    unsigned parts = 1;
    std::optional<uint32_t> at, base;
    unsigned count = 32;
};

Args parse(int argc, char** argv) {
    Args a;
    if (argc < 2) throw Error("no command given");
    a.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw Error(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--out") a.out = value();
        else if (arg == "--out-dir") a.outDir = value();
        else if (arg == "--outside") a.outside = value();
        else if (arg == "--id") a.id = value();
        else if (arg == "--capture") a.captures.emplace_back(value());
        else if (arg == "--no-symbols") a.symbols = false;
        else if (arg == "--no-comments") a.comments = false;
        else if (arg == "--parts") {
            const std::string text = value();
            const auto n = parseInt(text);
            if (!n || *n < 1 || *n > 999) throw Error("--parts takes a number from 1 to 999, not '" + text + "'");
            a.parts = static_cast<unsigned>(*n);
        }
        else if (arg == "--at" || arg == "--base" || arg == "--count") {
            const std::string text = value();
            const auto n = parseInt(text);
            if (!n || *n < 0 || *n > 0xFFFFFFFFll) throw Error(arg + " takes a number, not '" + text + "'");
            if (arg == "--at") a.at = static_cast<uint32_t>(*n);
            else if (arg == "--base") a.base = static_cast<uint32_t>(*n);
            else a.count = static_cast<unsigned>(*n);
        }
        else if (startsWith(arg, "--")) throw Error("unknown option " + arg);
        else a.positional.push_back(arg);
    }
    return a;
}

// What the captures say for the executable's own code. The code they hold
// outside it is for the image of its own (--outside).
Options analysisOptions(const Args& a, const ElfFile& elf) {
    Options o;
    o.useSymbols = a.symbols;
    for (const auto& file : a.captures) o.capture.add(loadCaptureFile(file));
    std::erase_if(o.capture.code, [&](const Capture::Code& c) { return !inCodeRanges(elf, c.start); });
    return o;
}

void printAnalysis(const ElfFile& elf, const Program& prog) {
    std::printf("%s: CRC %08X, entry %08X\n", elf.name().c_str(), pcsx2Crc(elf), elf.entry);
    for (const auto& r : prog.ranges) std::printf("  code %08X-%08X %s\n", r.start, r.end, r.name.c_str());
    std::printf("  %zu functions, %zu instructions, %zu block leaders\n", prog.functions.size(), prog.instructionCount, prog.leaderCount);
    for (const auto& [source, count] : prog.sourceCounts) std::printf("    %6zu from %s\n", count, sourceName(source));
    for (const Note& n : prog.notes) std::printf("  note: %s\n", n.message.c_str());
}

int compile(const Args& a) {
    if (a.positional.size() != 1) throw Error("compile takes one executable");
    if (a.out.empty()) throw Error("compile needs --out <file.cpp>");

    const std::filesystem::path path = a.positional[0];
    const ElfFile elf = ElfFile::load(path);
    const Program prog = analyze(elf, analysisOptions(a, elf));

    GenerateOptions go;
    go.id = a.id.empty() ? identifierFor(path.stem().string()) : a.id;
    go.comments = a.comments;
    go.parts = a.parts;
    if (go.parts > 1) go.headerName = a.out.stem().string() + ".h";
    GenerateStats stats;
    const std::vector<GeneratedFile> files = generateFiles(prog, go, &stats);

    // The main file is the output as named; the others are beside it, named
    // after it. A file that would not change is left alone, so that only the
    // parts that did change are compiled again.
    if (a.out.has_parent_path()) std::filesystem::create_directories(a.out.parent_path());
    for (const GeneratedFile& file : files) {
        const std::filesystem::path path = file.suffix == ".cpp" ? a.out : a.out.parent_path() / (a.out.stem().string() + file.suffix);
        writeFileIfChanged(path, file.text);
    }

    std::printf("%s: %zu functions, %zu instructions, %zu block leaders; %zu delegated (%.1f%%); "
                "%zu functions run on into the next (%zu instructions); left to the interpreter: %zu places, "
                "and %zu likely branches when taken; %zu variants from captured code\n",
                elf.name().c_str(), stats.functions, stats.instructions, stats.leaders, stats.delegated,
                stats.instructions ? 100.0 * static_cast<double>(stats.delegated) / static_cast<double>(stats.instructions) : 0.0, stats.tails,
                stats.tailInstructions, stats.leaves, stats.guards, stats.variants);
    for (const Note& n : prog.notes) std::printf("  note: %s\n", n.message.c_str());

    // The code the executable ran outside its own, as the captures hold it: an
    // image of its own, with the executable's CRC, which the runtime uses
    // with the executable's (ImageInfo::kind).
    if (!a.outside.empty()) {
        Capture captured;
        for (const auto& file : a.captures) captured.add(loadCaptureFile(file));
        captured.normalize();
        std::vector<Note> notes;
        std::vector<uint32_t> starts;
        const ElfFile pieces = codeOutside(elf, captured, notes, starts);
        Program other;
        other.elf = &pieces;
        if (!starts.empty()) {
            Options o;
            o.useSymbols = false;
            o.followCode = true;
            o.capture.entries = starts;
            other = analyze(pieces, o);
        }
        GenerateOptions og;
        og.id = go.id + "_outside";
        og.comments = a.comments;
        og.kind = 2;
        og.name = pieces.name();
        og.crc = pcsx2Crc(elf);
        GenerateStats otherStats;
        const std::vector<GeneratedFile> otherFiles = generateFiles(other, og, &otherStats);
        if (a.outside.has_parent_path()) std::filesystem::create_directories(a.outside.parent_path());
        writeFileIfChanged(a.outside, otherFiles.front().text);
        std::printf("%s: %zu functions, %zu instructions of the code it ran outside its executable\n", og.id.c_str(), otherStats.functions,
                    otherStats.instructions);
        for (const Note& n : notes) std::printf("  note: %s\n", n.message.c_str());
        for (const Note& n : other.notes) std::printf("  note: %s\n", n.message.c_str());
    }
    return 0;
}

int info(const Args& a) {
    if (a.positional.size() != 1) throw Error("info takes one executable");
    const ElfFile elf = ElfFile::load(a.positional[0]);
    const Program prog = analyze(elf, analysisOptions(a, elf));
    printAnalysis(elf, prog);

    GenerateOptions go;
    go.id = "info";
    GenerateStats stats;
    generate(prog, go, &stats);
    std::printf("  %zu instructions delegated to the core's interpreter functions (%.1f%%)\n", stats.delegated,
                stats.instructions ? 100.0 * static_cast<double>(stats.delegated) / static_cast<double>(stats.instructions) : 0.0);
    std::printf("  %zu instructions work on local variables holding the registers (%.1f%%; with what is compiled twice)\n", stats.onLocals,
                stats.instructions ? 100.0 * static_cast<double>(stats.onLocals) / static_cast<double>(stats.instructions) : 0.0);
    std::printf("  %zu block leaders where the locals go on holding registers (%zu registers in all)\n", stats.labelsHolding,
                stats.registersAtLabels);
    std::vector<std::pair<size_t, r5900::Op>> byCount;
    for (const auto& [op, count] : stats.delegatedByOp) byCount.emplace_back(count, op);
    std::sort(byCount.rbegin(), byCount.rend());
    for (size_t i = 0; i < byCount.size() && i < 12; ++i)
        std::printf("    %6zu %s\n", byCount[i].first, byCount[i].second == r5900::Op::Invalid ? "(unknown)" : r5900::mnemonic(byCount[i].second));
    std::printf("  %zu functions run on into the next (%zu instructions compiled twice)\n", stats.tails, stats.tailInstructions);
    std::printf("  %zu places where the rest of a block is left to the interpreter\n", stats.leaves);
    std::printf("  %zu likely branches left to the interpreter when taken, for what is in their delay slot\n", stats.guards);
    std::printf("  %zu COP2 instructions; of them %zu VU0 instructions, which compiled code carries out itself where the runtime"
                " says nothing is to be done about VU0 first, and %zu the runtime carries out whole\n",
                stats.cop2, stats.vu0, stats.cop2 - stats.vu0);
    std::printf("  %zu DI compiled in one piece with the instruction after them\n", stats.delayedDisables);
    std::printf("  %zu branches that may close an idle loop\n", stats.idleLoops);
    return 0;
}

// What runs reported, put together.
Capture loadCaptures(const Args& a) {
    Capture all;
    for (const auto& file : a.captures) all.add(loadCaptureFile(file));
    all.normalize();
    return all;
}

// The analysis of one of the BIOS's images: its code followed from where it
// is known to be entered (and from `more`), and, with `withCapture`, from
// where runs entered it.
Program analyzeBios(const BiosImage& image, const std::vector<uint32_t>& more, const Capture& capture, bool withCapture) {
    Options o;
    o.useSymbols = false;
    o.followCode = true;
    o.entries = image.entries;
    o.entries.insert(o.entries.end(), more.begin(), more.end());
    if (withCapture) {
        for (uint32_t e : capture.entries)
            if (image.code.isMapped(e)) o.capture.entries.push_back(e);
        for (const Capture::Code& c : capture.code)
            if (image.code.isMapped(c.start)) o.capture.code.push_back(c);
    }
    return analyze(image.code, o);
}

// All of the BIOS's images. The code of one goes into another (the reset
// code into the ROM's cached mirror, and into the kernel and the loader once
// it has copied them): where it leaves one image for another, the other is
// entered, and followed from there too.
std::vector<Program> analyzeBiosImages(const Bios& b, const Capture& capture, bool withCapture) {
    std::vector<std::vector<uint32_t>> more(b.images.size());
    std::vector<Program> progs;
    for (;;) {
        progs.clear();
        for (size_t i = 0; i < b.images.size(); ++i) progs.push_back(analyzeBios(b.images[i], more[i], capture, withCapture));
        bool grew = false;
        for (size_t i = 0; i < b.images.size(); ++i) {
            for (uint32_t exit : progs[i].exits) {
                for (size_t j = 0; j < b.images.size(); ++j) {
                    if (j == i || !b.images[j].code.isMapped(exit)) continue;
                    if (std::find(more[j].begin(), more[j].end(), exit) != more[j].end()) continue;
                    more[j].push_back(exit);
                    grew = true;
                }
            }
        }
        if (!grew) return progs;
    }
}

int bios(const Args& a) {
    if (a.positional.size() != 1) throw Error("bios takes one BIOS image");
    if (a.outDir.empty()) throw Error("bios needs --out-dir <dir>");
    const Bios b = readBios(std::filesystem::path(a.positional[0]));
    const Capture capture = loadCaptures(a);

    std::filesystem::create_directories(a.outDir);
    const std::vector<Program> progs = analyzeBiosImages(b, capture, true);
    for (size_t i = 0; i < b.images.size(); ++i) {
        const BiosImage& image = b.images[i];
        const Program& prog = progs[i];
        GenerateOptions go;
        go.id = image.id;
        go.comments = a.comments;
        go.kind = 1;
        go.name = b.name + ", " + image.what;
        go.crc = b.crc;
        GenerateStats stats;
        const std::vector<GeneratedFile> files = generateFiles(prog, go, &stats);
        writeFileIfChanged(a.outDir / (image.id + ".cpp"), files.front().text);
        std::printf("%s: %zu functions, %zu instructions, %zu block leaders; %zu delegated; left to the interpreter: %zu places, "
                    "and %zu likely branches when taken\n",
                    image.id.c_str(), stats.functions, stats.instructions, stats.leaders, stats.delegated, stats.leaves, stats.guards);
        for (const Note& n : prog.notes) std::printf("  note: %s\n", n.message.c_str());
    }
    return 0;
}

int biosInfo(const Args& a) {
    if (a.positional.size() != 1) throw Error("bios-info takes one BIOS image");
    const Bios b = readBios(std::filesystem::path(a.positional[0]));
    const Capture capture = loadCaptures(a);

    std::printf("%s: CRC %08X (of the whole ROM)\n", b.name.c_str(), b.crc);
    const std::vector<Program> progs = analyzeBiosImages(b, capture, true);
    const std::vector<Program> alones = analyzeBiosImages(b, capture, false);
    for (size_t i = 0; i < b.images.size(); ++i) {
        const BiosImage& image = b.images[i];
        const Program& prog = progs[i];
        std::printf("  %s: %s\n", image.id.c_str(), image.what.c_str());
        for (const auto& r : prog.ranges) std::printf("    %s at %08X-%08X\n", r.name.c_str(), r.start, r.end);
        std::printf("    %zu functions, %zu instructions, %zu block leaders\n", prog.functions.size(), prog.instructionCount, prog.leaderCount);
        for (const auto& [source, count] : prog.sourceCounts) std::printf("      %6zu from %s\n", count, sourceName(source));

        // How much of what runs entered the analysis finds by itself.
        const Program& alone = alones[i];
        size_t inside = 0, found = 0;
        for (uint32_t e : capture.entries) {
            if (!image.code.isMapped(e)) continue;
            ++inside;
            const Function* f = alone.functionContaining(e);
            if (f && f->isLeader(e)) ++found;
        }
        if (inside) std::printf("    captured entries in it: %zu, of which the analysis finds %zu without being given them\n", inside, found);

        GenerateOptions go;
        go.id = "info";
        go.kind = 1;
        GenerateStats stats;
        generate(prog, go, &stats);
        std::printf("    %zu instructions delegated to the core's interpreter functions; %zu places where the rest of a block is "
                    "left to the interpreter; %zu likely branches left to it when taken\n",
                    stats.delegated, stats.leaves, stats.guards);
        std::vector<std::pair<size_t, r5900::Op>> byCount;
        for (const auto& [op, count] : stats.delegatedByOp) byCount.emplace_back(count, op);
        std::sort(byCount.rbegin(), byCount.rend());
        for (size_t i = 0; i < byCount.size() && i < 8; ++i)
            std::printf("      %6zu %s\n", byCount[i].first, byCount[i].second == r5900::Op::Invalid ? "(unknown)" : r5900::mnemonic(byCount[i].second));
        for (const Note& n : prog.notes) std::printf("    note: %s\n", n.message.c_str());
    }
    return 0;
}

int capture(const Args& a) {
    if (a.out.empty()) throw Error("capture needs --out <file>");
    if (a.positional.empty()) throw Error("capture takes one or more capture files");

    Capture all;
    for (const std::string& file : a.positional) {
        // The output may be named among the inputs before it exists.
        if (!std::filesystem::exists(file) && std::filesystem::path(file) == a.out) continue;
        all.add(loadCaptureFile(file));
    }
    all.normalize();

    if (a.out.has_parent_path()) std::filesystem::create_directories(a.out.parent_path());
    writeFileIfChanged(a.out, writeCapture(all, "What runs of a program reported for the recompiler, put together by ps2n-recomp capture."));
    std::printf("%s: %zu addresses at which a block was interpreted, %zu pieces of code found in memory\n", a.out.string().c_str(),
                all.entries.size(), all.code.size());
    return 0;
}

int registry(const Args& a) {
    if (a.out.empty()) throw Error("registry needs --out <file.cpp>");
    if (a.out.has_parent_path()) std::filesystem::create_directories(a.out.parent_path());
    writeFileIfChanged(a.out, generateRegistry(a.positional));
    return 0;
}

int assemble(const Args& a) {
    if (a.positional.size() != 1) throw Error("assemble takes one source file");
    if (a.out.empty()) throw Error("assemble needs --out <file.elf>");
    const std::filesystem::path path = a.positional[0];
    Assembler as;
    as.assemble(readTextFile(path), path.string());
    const std::vector<uint8_t> elf = as.buildElf();
    if (a.out.has_parent_path()) std::filesystem::create_directories(a.out.parent_path());
    writeFileIfChanged(a.out, std::string_view(reinterpret_cast<const char*>(elf.data()), elf.size()));
    return 0;
}

int disasm(const Args& a) {
    if (a.positional.size() != 1) throw Error("disasm takes one file");
    if (!a.at) throw Error("disasm needs --at <address>");

    // The word at an address, from an executable's loaded image or from a raw
    // image that begins at --base.
    std::vector<uint8_t> raw;
    std::optional<ElfFile> elf;
    if (a.base) raw = readFile(a.positional[0]);
    else elf = ElfFile::load(a.positional[0]);
    const auto word = [&](uint32_t address, uint32_t& out) {
        if (elf) return elf->read32(address, out);
        const uint32_t offset = address - *a.base;
        if (address < *a.base || static_cast<size_t>(offset) + 4 > raw.size()) return false;
        out = static_cast<uint32_t>(raw[offset]) | static_cast<uint32_t>(raw[offset + 1]) << 8 | static_cast<uint32_t>(raw[offset + 2]) << 16 |
              static_cast<uint32_t>(raw[offset + 3]) << 24;
        return true;
    };

    for (unsigned i = 0; i < a.count; ++i) {
        const uint32_t address = *a.at + i * 4;
        uint32_t w = 0;
        if (!word(address, w)) {
            std::printf("%08X  (not in the file)\n", address);
            break;
        }
        const r5900::Insn in(w, address);
        std::printf("%08X  %08X  %3u  %s\n", address, w, static_cast<unsigned>(r5900::cycles(in.op)), r5900::disassemble(in).c_str());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h" || std::string(argv[1]) == "help") {
            usage();
            return argc < 2 ? 2 : 0;
        }
        const Args a = parse(argc, argv);
        if (a.command == "compile") return compile(a);
        if (a.command == "info") return info(a);
        if (a.command == "bios") return bios(a);
        if (a.command == "bios-info") return biosInfo(a);
        if (a.command == "capture") return capture(a);
        if (a.command == "registry") return registry(a);
        if (a.command == "assemble") return assemble(a);
        if (a.command == "disasm") return disasm(a);
        throw Error("unknown command '" + a.command + "'");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ps2n-recomp: %s\n", e.what());
        return 1;
    }
}
