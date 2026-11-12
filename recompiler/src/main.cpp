// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps2n-recomp: the static recompiler's command line.

#include "analysis.h"
#include "codegen.h"

#include "ps2n/assembler.h"
#include "ps2n/elf.h"
#include "ps2n/util.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
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
        "\n"
        "  ps2n-recomp info <elf> [--capture <file>] [--no-symbols]\n"
        "      Prints what the analysis finds, without writing anything.\n"
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

Options analysisOptions(const Args& a) {
    Options o;
    o.useSymbols = a.symbols;
    for (const auto& file : a.captures) o.capture.add(loadCaptureFile(file));
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
    const Program prog = analyze(elf, analysisOptions(a));

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
    return 0;
}

int info(const Args& a) {
    if (a.positional.size() != 1) throw Error("info takes one executable");
    const ElfFile elf = ElfFile::load(a.positional[0]);
    const Program prog = analyze(elf, analysisOptions(a));
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
    std::printf("  %zu COP2 instructions, which the runtime carries out whole\n", stats.cop2);
    std::printf("  %zu DI compiled in one piece with the instruction after them\n", stats.delayedDisables);
    std::printf("  %zu branches that may close an idle loop\n", stats.idleLoops);
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
