// ps2-uncompiler command line.
#include "analysis.h"
#include "codegen.h"
#include "config.h"
#include "report.h"
#include "version.h"

#include "ps2uc/assembler.h"
#include "ps2uc/elf.h"
#include "ps2uc/irx.h"
#include "ps2uc/r5900.h"
#include "ps2uc/util.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace ps2uc;

namespace {

constexpr const char* kHelp =
    "usage: ps2-uncompiler <command> [options]\n"
    "\n"
    "commands:\n"
    "  recompile <config.toml>   generate C++ for the ELF named in the config\n"
    "      --elf PATH              override [input].elf\n"
    "      --out DIR               override [output].dir\n"
    "      --name NAME             override [output].name\n"
    "      --files N               number of recomp_funcs_*.cpp files (fixed, for build systems)\n"
    "      --single                one source file\n"
    "      --no-project            do not write CMakeLists.txt\n"
    "      --no-comments           omit per-instruction disassembly comments\n"
    "      --quiet                 print nothing but errors\n"
    "  analyze <game.elf | config.toml>\n"
    "      --map CSV               add a Ghidra function map (repeatable)\n"
    "      --list                  print every function found\n"
    "      --write-config FILE     write a starter config.toml for this ELF\n"
    "  disasm <file.elf> [--function NAME|ADDR] [--range START:END] [--cpp NAME|ADDR]\n"
    "  irx <module.irx>          print the IOP module header, imports and exports\n"
    "  assemble <input.s> -o <output.elf> [--base ADDR] [--strip]\n"
    "  stubs                     list the runtime stub handlers\n"
    "  version\n";

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>> options;
    bool has(const std::string& k) const { return options.count(k) != 0; }
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = options.find(k);
        return it == options.end() || it->second.empty() ? def : it->second.back();
    }
};

Args parseArgs(int argc, char** argv, int first, std::initializer_list<const char*> valued) {
    Args a;
    for (int i = first; i < argc; ++i) {
        const std::string s = argv[i];
        if (startsWith(s, "-") && s.size() > 1) {
            bool takesValue = false;
            for (const char* v : valued)
                if (s == v) takesValue = true;
            if (takesValue) {
                if (i + 1 >= argc) throw Error(s + " needs a value");
                a.options[s].push_back(argv[++i]);
            } else {
                a.options[s];
            }
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

uint32_t parseAddressArg(const std::string& s) {
    auto v = parseInt(s.find("0x") == 0 || s.find("0X") == 0 ? s : "0x" + s);
    if (!v || *v < 0 || *v > 0xFFFFFFFFLL) throw Error("bad address '" + s + "'");
    return static_cast<uint32_t>(*v);
}

Config loadConfigOrElf(const std::string& path) {
    if (endsWith(toLower(path), ".toml")) return Config::load(path);
    return Config::forElf(path);
}

int cmdRecompile(int argc, char** argv) {
    const Args a = parseArgs(argc, argv, 2, {"--elf", "--out", "--name", "--files"});
    if (a.positional.size() != 1) throw Error("recompile needs exactly one config file (see --help)");
    Config cfg = loadConfigOrElf(a.positional[0]);
    if (a.has("--elf")) cfg.elf = std::filesystem::absolute(a.get("--elf"));
    if (a.has("--out")) cfg.outDir = std::filesystem::absolute(a.get("--out"));
    if (a.has("--name")) cfg.name = a.get("--name");
    if (a.has("--files")) {
        cfg.files = static_cast<int>(parseInt(a.get("--files")).value_or(0));
        cfg.singleFile = false;
    }
    if (a.has("--single")) cfg.singleFile = true;
    if (a.has("--no-project")) cfg.project = false;
    if (a.has("--no-comments")) cfg.comments = false;
    const bool quiet = a.has("--quiet");

    const ElfFile elf = ElfFile::load(cfg.elf);
    if (elf.isIrx()) throw Error(cfg.elf.string() + " is an IRX (IOP) module; recompile the EE executable instead (try `ps2-uncompiler irx`)");
    const Program prog = analyze(elf, cfg);
    const GenerateResult res = generate(prog, cfg);
    writeFileIfChanged(cfg.outDir / "report.txt", buildReport(prog, cfg));

    if (!quiet) {
        std::printf("%s: %s\n", cfg.elf.filename().string().c_str(), summarize(prog).c_str());
        for (size_t i = 0; i < prog.warnings.size() && i < 8; ++i)
            std::printf("  warning: %s\n", prog.warnings[i].message.c_str());
        if (prog.warnings.size() > 8) std::printf("  ... %zu more warnings in report.txt\n", prog.warnings.size() - 8);
        std::printf("wrote %zu file(s) to %s (%zu changed)\n", res.sources.size() + 1 + (cfg.project ? 1 : 0), cfg.outDir.string().c_str(),
                    res.written.size());
        if (cfg.project)
            std::printf("build:  cmake -S %s -B %s/build && cmake --build %s/build\n", cfg.outDir.string().c_str(),
                        cfg.outDir.string().c_str(), cfg.outDir.string().c_str());
    }
    return 0;
}

std::string starterConfig(const Program& prog, const Config& cfg) {
    std::string s;
    s += "# Starter config written by `ps2-uncompiler analyze`. See docs/config.md.\n\n";
    s += "[input]\nelf = \"" + cfg.elf.generic_string() + "\"\n";
    s += "# ghidra_map = \"functions.csv\"   # export with tools/ghidra/ExportPs2ucFunctions.java\n\n";
    s += "[output]\ndir = \"out/" + cfg.name + "\"\nname = \"" + cfg.name + "\"\nmode = \"multi\"\ncomments = true\n\n";
    s += "[analysis]\n# entries = [0x00100000]\n# entries_files = [\"ps2uc_missing.toml\"]   # written by the runtime\n\n";
    s += "[stubs]\nauto = true\n";
    std::string matched;
    for (const auto& f : prog.functions)
        if (f.binding == Binding::Stub) matched += "#   " + f.name + strprintf(" @ 0x%08x\n", f.start);
    if (!matched.empty()) s += "# auto-bound to runtime stubs:\n" + matched;
    s += "# bind = { \"0x00123456\" = \"ret0\" }\n# exclude = [\"memcpy\"]\n\n";
    s += "[functions]\n# skip = [\"some_function\"]\n# override = { \"some_function\" = \"my_replacement\" }\n";
    return s;
}

int cmdAnalyze(int argc, char** argv) {
    const Args a = parseArgs(argc, argv, 2, {"--map", "--write-config"});
    if (a.positional.size() != 1) throw Error("analyze needs an ELF or a config file");
    Config cfg = loadConfigOrElf(a.positional[0]);
    if (auto it = a.options.find("--map"); it != a.options.end())
        for (const auto& m : it->second) cfg.functionMaps.emplace_back(std::filesystem::absolute(m));
    const ElfFile elf = ElfFile::load(cfg.elf);
    std::printf("%s: ELF32 %s, entry 0x%08x, %zu sections, %zu symbols, crc32 %08x\n", cfg.elf.string().c_str(),
                elf.isIrx() ? "IOP module (IRX)" : "executable", elf.entry, elf.sections.size(), elf.symbols.size(), elf.crc32());
    for (const auto& r : elf.codeRanges()) std::printf("  code %-10s 0x%08x-0x%08x %8u bytes\n", r.name.c_str(), r.start, r.end, r.size());
    if (elf.isIrx()) {
        std::printf("this is an IOP module; use `ps2-uncompiler irx` to inspect it\n");
        return 0;
    }
    const Program prog = analyze(elf, cfg);
    std::printf("%s\n", summarize(prog).c_str());
    for (const auto& [src, n] : prog.sourceCounts) std::printf("  %-28s %zu\n", sourceName(src), n);
    if (a.has("--list")) {
        for (const auto& f : prog.functions)
            std::printf("0x%08x 0x%08x %-40s %s%s\n", f.start, f.end, f.name.c_str(), sourceName(f.source),
                        f.binding == Binding::Stub ? " [stub]" : "");
    }
    for (const auto& w : prog.warnings) std::printf("  warning: %s\n", w.message.c_str());
    if (a.has("--write-config")) {
        const std::string path = a.get("--write-config");
        if (std::filesystem::exists(path)) throw Error(path + " already exists; not overwriting it");
        writeFileIfChanged(path, starterConfig(prog, cfg));
        std::printf("wrote %s\n", path.c_str());
    }
    return 0;
}

int cmdDisasm(int argc, char** argv) {
    const Args a = parseArgs(argc, argv, 2, {"--function", "--range", "--cpp"});
    if (a.positional.size() != 1) throw Error("disasm needs an ELF");
    Config cfg = loadConfigOrElf(a.positional[0]);
    const ElfFile elf = ElfFile::load(cfg.elf);
    const Program prog = analyze(elf, cfg);

    auto findFn = [&](const std::string& key) -> const Function* {
        for (const auto& f : prog.functions)
            if (f.name == key) return &f;
        return prog.functionContaining(parseAddressArg(key));
    };
    if (a.has("--cpp")) {
        const Function* f = findFn(a.get("--cpp"));
        if (!f) throw Error("no function " + a.get("--cpp"));
        std::printf("%s", emitFunction(prog, *f, true).c_str());
        return 0;
    }
    uint32_t start = 0, end = 0;
    if (a.has("--function")) {
        const Function* f = findFn(a.get("--function"));
        if (!f) throw Error("no function " + a.get("--function"));
        start = f->start;
        end = f->end;
    } else if (a.has("--range")) {
        const auto parts = split(a.get("--range"), ':');
        if (parts.size() != 2) throw Error("--range wants START:END");
        start = parseAddressArg(parts[0]);
        end = parseAddressArg(parts[1]);
    }
    for (const auto& f : prog.functions) {
        if (end && (f.end <= start || f.start >= end)) continue;
        std::printf("\n%s:   # 0x%08x-0x%08x, %s\n", f.name.c_str(), f.start, f.end, sourceName(f.source));
        for (uint32_t addr = std::max(f.start, start); addr < f.end && (!end || addr < end); addr += 4) {
            const r5900::Insn in(prog.word(addr), addr);
            if (auto it = prog.labelNames.find(addr); it != prog.labelNames.end()) std::printf("%s:\n", it->second.c_str());
            std::string extra;
            if (in.is(r5900::F_JUMP) || in.isBranch()) extra = "   # " + prog.describe(in.staticTarget());
            std::printf("  %08x:  %08x  %s%s\n", addr, in.raw, r5900::disassemble(in).c_str(), extra.c_str());
        }
    }
    return 0;
}

int cmdIrx(int argc, char** argv) {
    const Args a = parseArgs(argc, argv, 2, {});
    if (a.positional.size() != 1) throw Error("irx needs a module file");
    const ElfFile elf = ElfFile::load(a.positional[0]);
    const IrxInfo info = parseIrx(elf);
    std::printf("module   %s v%d.%02d\nentry    0x%08x\ngp       0x%08x\ntext     %u bytes\ndata     %u bytes\nbss      %u bytes\n",
                info.name.c_str(), info.version >> 8, info.version & 0xFF, info.entry, info.gp, info.textSize, info.dataSize,
                info.bssSize);
    std::string lib;
    for (const auto& imp : info.imports) {
        if (imp.library != lib) {
            std::printf("\nimports from %s (v%d.%02d)\n", imp.library.c_str(), imp.version >> 8, imp.version & 0xFF);
            lib = imp.library;
        }
        std::printf("  %3u  %s\n", imp.index, imp.name.empty() ? "?" : imp.name.c_str());
    }
    for (const auto& ex : info.exports) {
        std::printf("\nexports library %s (v%d.%02d), %zu entries\n", ex.library.c_str(), ex.version >> 8, ex.version & 0xFF,
                    ex.functions.size());
        for (size_t i = 0; i < ex.functions.size(); ++i) std::printf("  %3zu  0x%08x\n", i, ex.functions[i]);
    }
    return 0;
}

int cmdAssemble(int argc, char** argv) {
    const Args a = parseArgs(argc, argv, 2, {"-o", "--base"});
    if (a.positional.size() != 1 || !a.has("-o")) throw Error("usage: assemble <input.s> -o <output.elf>");
    AsmOptions opt;
    if (a.has("--base")) opt.textBase = parseAddressArg(a.get("--base"));
    opt.symbols = !a.has("--strip");
    Assembler as(opt);
    as.assemble(readTextFile(a.positional[0]), a.positional[0]);
    const auto bytes = as.buildElf();
    std::ofstream out(a.get("-o"), std::ios::binary | std::ios::trunc);
    if (!out) throw Error("cannot write " + a.get("-o"));
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fputs(kHelp, stderr);
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "recompile") return cmdRecompile(argc, argv);
        if (cmd == "analyze") return cmdAnalyze(argc, argv);
        if (cmd == "disasm") return cmdDisasm(argc, argv);
        if (cmd == "irx") return cmdIrx(argc, argv);
        if (cmd == "assemble") return cmdAssemble(argc, argv);
        if (cmd == "stubs") {
            for (const auto& s : runtimeStubNames()) std::printf("%s\n", s.c_str());
            return 0;
        }
        if (cmd == "version" || cmd == "--version") {
            std::printf("%s\n", PS2UC_VERSION_STRING);
            return 0;
        }
        if (cmd == "help" || cmd == "--help" || cmd == "-h") {
            std::fputs(kHelp, stdout);
            return 0;
        }
        std::fprintf(stderr, "unknown command '%s'\n\n%s", cmd.c_str(), kHelp);
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
