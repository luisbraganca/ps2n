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
        "\n"
        "  ps2n-recomp info <elf> [--capture <file>] [--no-symbols]\n"
        "      Prints what the analysis finds, without writing anything.\n"
        "\n"
        "  ps2n-recomp registry --out <file.cpp> <id>...\n"
        "      Writes the list of images a host is linked with.\n"
        "\n"
        "  ps2n-recomp assemble <file.s> --out <file.elf>\n"
        "      Assembles a test program written for PS2Native's own small R5900\n"
        "      assembler (core/include/ps2n/assembler.h) into an EE executable.");
}

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::filesystem::path out;
    std::string id;
    std::vector<std::filesystem::path> captures;
    bool symbols = true;
    bool comments = true;
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
    GenerateStats stats;
    const std::string source = generate(prog, go, &stats);

    if (a.out.has_parent_path()) std::filesystem::create_directories(a.out.parent_path());
    writeFileIfChanged(a.out, source);

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
    std::vector<std::pair<size_t, r5900::Op>> byCount;
    for (const auto& [op, count] : stats.delegatedByOp) byCount.emplace_back(count, op);
    std::sort(byCount.rbegin(), byCount.rend());
    for (size_t i = 0; i < byCount.size() && i < 12; ++i)
        std::printf("    %6zu %s\n", byCount[i].first, byCount[i].second == r5900::Op::Invalid ? "(unknown)" : r5900::mnemonic(byCount[i].second));
    std::printf("  %zu functions run on into the next (%zu instructions compiled twice)\n", stats.tails, stats.tailInstructions);
    std::printf("  %zu places where the rest of a block is left to the interpreter\n", stats.leaves);
    std::printf("  %zu likely branches left to the interpreter when taken, for what is in their delay slot\n", stats.guards);
    std::printf("  %zu branches that close an idle loop\n", stats.idleLoops);
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
        if (a.command == "registry") return registry(a);
        if (a.command == "assemble") return assemble(a);
        throw Error("unknown command '" + a.command + "'");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ps2n-recomp: %s\n", e.what());
        return 1;
    }
}
