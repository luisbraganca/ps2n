// Config, function maps, analysis and code generation.
#include "testing.h"

#include "analysis.h"
#include "codegen.h"
#include "config.h"
#include "function_map.h"

#include "ps2n/assembler.h"
#include "ps2n/elf.h"
#include "ps2n/util.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace ps2n;
namespace fs = std::filesystem;

namespace {

fs::path tempDir() {
    static const fs::path d = [] {
        fs::path p = fs::temp_directory_path() / ("ps2n_tests_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(p);
        return p;
    }();
    return d;
}

fs::path writeTemp(const std::string& name, const std::string& content) {
    const fs::path p = tempDir() / name;
    std::ofstream(p, std::ios::binary) << content;
    return p;
}

ElfFile assemble(const std::string& src, bool symbols = true) {
    AsmOptions opt;
    opt.symbols = symbols;
    Assembler as(opt);
    as.assemble(src, "test.s");
    return ElfFile::parse(as.buildElf(), "test.elf");
}

Config configFor(const ElfFile&) {
    Config c;
    c.configDir = tempDir();
    c.elf = tempDir() / "test.elf";
    c.name = "test";
    c.outDir = tempDir() / "out";
    return c;
}

const Function* byName(const Program& p, const std::string& n) {
    for (const auto& f : p.functions)
        if (f.name == n) return &f;
    return nullptr;
}

const char* kSwitchProgram = R"(
        .text
        .globl _start
        .ent _start
_start: jal   classify
        li    $a0, 2
1:      b     1b
        nop
        .end _start
        .ent classify
classify:
        sltiu $t0, $a0, 3
        beq   $t0, $zero, 9f
        sll   $t1, $a0, 2
        lui   $at, %hi(table)
        addu  $at, $at, $t1
        lw    $t2, %lo(table)($at)
        jr    $t2
        nop
c0:     jr    $ra
        li    $v0, 10
c1:     jr    $ra
        li    $v0, 11
c2:     jr    $ra
        li    $v0, 12
9:      jr    $ra
        li    $v0, -1
        .end classify
        .rodata
table:  .word c0, c1, c2
)";

// An executable laid out as Metrowerks CodeWarrior links one: code and data in
// one section, "main", with symbols that say where the code ends.
struct TestSymbol {
    const char* name;
    uint32_t value, size;
    uint8_t type, bind;
};

const std::vector<TestSymbol> kMergedSymbols = {
    {"_start", 0x00100000, 0x18, elf::STT_FUNC, elf::STB_GLOBAL},
    {"__data_start", 0x00100020, 0, elf::STT_NOTYPE, elf::STB_GLOBAL},
    {"table", 0x00100020, 8, elf::STT_OBJECT, elf::STB_GLOBAL},
};
constexpr uint32_t kRwx = elf::SHF_ALLOC | elf::SHF_WRITE | elf::SHF_EXECINSTR;

std::vector<uint8_t> mergedElf(uint32_t sectionFlags, const std::vector<TestSymbol>& symbols) {
    const std::vector<uint32_t> image = {
        0x3c020010u,   // _start: lui  $v0, 0x0010
        0x8c420020u,   //         lw   $v0, 0x20($v0)      table[0]
        0x00400008u,   //         jr   $v0
        0x00000000u,   //         nop
        0x03e00008u,   // back:   jr   $ra                 only table[0] leads here
        0x24020007u,   //         li   $v0, 7
        0, 0,          //         padding
        0x00100010u,   // table:  .word back
        0x24020001u,   //         .word 0x24020001         (li $v0, 1, were it code)
    };
    std::string strtab(1, '\0');
    std::vector<uint8_t> symtab(16, 0);   // the null symbol
    for (const TestSymbol& s : symbols) {
        uint8_t e[16] = {};
        const auto nameOff = static_cast<uint32_t>(strtab.size());
        strtab += s.name;
        strtab.push_back('\0');
        std::memcpy(e, &nameOff, 4);
        std::memcpy(e + 4, &s.value, 4);
        std::memcpy(e + 8, &s.size, 4);
        e[12] = static_cast<uint8_t>((s.bind << 4) | s.type);
        e[14] = 1;   // in main
        symtab.insert(symtab.end(), e, e + 16);
    }
    const std::string shstr("\0main\0.symtab\0.strtab\0.shstrtab\0", 32);
    const size_t imageOff = 0x100, imageSize = image.size() * 4;
    const size_t symOff = imageOff + imageSize, strOff = symOff + symtab.size(), shstrOff = strOff + strtab.size();
    const size_t shoff = (shstrOff + shstr.size() + 3) & ~size_t(3);
    std::vector<uint8_t> b(shoff + 40 * 5, 0);
    auto put16 = [&](size_t off, uint16_t v) { std::memcpy(b.data() + off, &v, 2); };
    auto put32 = [&](size_t off, size_t v) {
        const auto w = static_cast<uint32_t>(v);
        std::memcpy(b.data() + off, &w, 4);
    };
    const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 1, 1, 1};
    std::memcpy(b.data(), ident, 16);
    put16(16, elf::ET_EXEC);
    put16(18, elf::EM_MIPS);
    put32(20, 1);
    put32(24, 0x00100000);   // entry
    put32(28, 52);           // program headers
    put32(32, shoff);
    put16(40, 52);
    put16(42, 32);
    put16(44, 1);
    put16(46, 40);
    put16(48, 5);
    put16(50, 4);            // .shstrtab
    put32(52, elf::PT_LOAD);
    put32(56, imageOff);
    put32(60, 0x00100000);
    put32(64, 0x00100000);
    put32(68, imageSize);
    put32(72, imageSize);
    put32(76, elf::PF_R | elf::PF_W | elf::PF_X);
    std::memcpy(b.data() + imageOff, image.data(), imageSize);
    std::memcpy(b.data() + symOff, symtab.data(), symtab.size());
    std::memcpy(b.data() + strOff, strtab.data(), strtab.size());
    std::memcpy(b.data() + shstrOff, shstr.data(), shstr.size());
    auto sh = [&](size_t i, size_t name, uint32_t type, uint32_t flags, size_t addr, size_t off, size_t size, size_t link,
                  size_t entsize) {
        const size_t h = shoff + 40 * i;
        put32(h, name);
        put32(h + 4, type);
        put32(h + 8, flags);
        put32(h + 12, addr);
        put32(h + 16, off);
        put32(h + 20, size);
        put32(h + 24, link);
        put32(h + 36, entsize);
    };
    sh(1, 1, elf::SHT_PROGBITS, sectionFlags, 0x00100000, imageOff, imageSize, 0, 0);
    sh(2, 6, elf::SHT_SYMTAB, 0, 0, symOff, symtab.size(), 3, 16);
    sh(3, 14, elf::SHT_STRTAB, 0, 0, strOff, strtab.size(), 0, 0);
    sh(4, 22, elf::SHT_STRTAB, 0, 0, shstrOff, shstr.size(), 0, 0);
    return b;
}

} // namespace

TEST_CASE(config_full) {
    const fs::path p = writeTemp("full.toml", R"(
[input]
elf = "game.elf"
ghidra_map = "map.csv"

[output]
dir = "gen"
name = "mygame"
mode = "single"
comments = false
extra_sources = ["overrides.cpp"]

[analysis]
prologues = true
preempt_loops = false
entries = [0x00100000, "0x00100100"]
[[analysis.functions]]
name = "weird"
start = 0x00101000
size = 0x40

[stubs]
auto = false
bind = { "printf" = "printf", "0x00123456" = "ret0" }
exclude = ["memcpy"]

[functions]
skip = ["foo", "0x00200000"]
override = { "bar" = "my_bar" }

[[patches]]
address = 0x00100abc
value = 0
expect = 0x0c040000
)");
    const Config c = Config::load(p);
    CHECK_EQ(c.elf, (tempDir() / "game.elf").lexically_normal());
    CHECK_EQ(c.functionMaps.size(), size_t(1));
    CHECK_EQ(c.outDir, (tempDir() / "gen").lexically_normal());
    CHECK_EQ(c.name, std::string("mygame"));
    CHECK(c.singleFile);
    CHECK(!c.comments);
    CHECK_EQ(c.extraSources.size(), size_t(1));
    CHECK(c.scanPrologues);
    CHECK(!c.preemptLoops);
    CHECK_EQ(c.entries.size(), size_t(2));
    CHECK_EQ(c.entries[1], 0x00100100u);
    CHECK_EQ(c.functions.size(), size_t(1));
    CHECK_EQ(c.functions[0].end.value(), 0x00101040u);
    CHECK(!c.autoStubs);
    CHECK_EQ(c.stubBindings.size(), size_t(2));
    bool sawAddress = false;
    for (const auto& [ref, h] : c.stubBindings)
        if (ref.address && *ref.address == 0x00123456u && h == "ret0") sawAddress = true;
    CHECK(sawAddress);
    CHECK_EQ(c.skip.size(), size_t(2));
    CHECK_EQ(c.overrides.size(), size_t(1));
    CHECK_EQ(c.patches.size(), size_t(1));
    CHECK_EQ(c.patches[0].expect.value(), 0x0c040000u);
}

TEST_CASE(config_array_bindings_and_errors) {
    const Config c = Config::load(writeTemp("arr.toml", "[input]\nelf = \"a.elf\"\n[stubs]\nbind = [\"printf\", \"sceCdRead@0x00123456\"]\n"));
    CHECK_EQ(c.stubBindings.size(), size_t(2));
    CHECK_EQ(c.stubBindings[1].second, std::string("sceCdRead"));
    CHECK_EQ(c.stubBindings[1].first.address.value(), 0x00123456u);

    CHECK_THROWS(Config::load(writeTemp("nokey.toml", "[input]\nelf = \"a.elf\"\nbogus = 1\n")));
    CHECK_THROWS(Config::load(writeTemp("noelf.toml", "[output]\nname = \"x\"\n")));
    CHECK_THROWS(Config::load(writeTemp("mode.toml", "[input]\nelf = \"a.elf\"\n[output]\nmode = \"triple\"\n")));
    CHECK_THROWS(Config::load(writeTemp("syntax.toml", "[input\nelf = \"a.elf\"\n")));
    try {
        Config::load(writeTemp("where.toml", "[input]\nelf = \"a.elf\"\n\n[analysis]\nsymbols = 3\n"));
        ::testing::fail(__FILE__, __LINE__, "expected an error");
    } catch (const Error& e) {
        CHECK(std::string(e.what()).find(":5") != std::string::npos);   // reports the line
    }
}

TEST_CASE(config_programs) {
    const Config c = Config::load(writeTemp("multi.toml", "[input]\nelf = \"boot.elf\"\nprograms = [\"MAIN.ELF\", \"sub/driving.toml\"]\n"));
    CHECK_EQ(c.programs.size(), size_t(2));
    CHECK_EQ(c.programs[0], (tempDir() / "MAIN.ELF").lexically_normal());
    CHECK_EQ(c.programs[1], (tempDir() / "sub" / "driving.toml").lexically_normal());
    // Folder and namespace names, by CMake's MAKE_C_IDENTIFIER rule.
    CHECK_EQ(programId("ACTION.UK"), std::string("ACTION_UK"));
    CHECK_EQ(programId("disc/007-2.ELF"), std::string("_007_2_ELF"));
}

TEST_CASE(function_map_formats) {
    auto ours = loadFunctionMap(writeTemp("ours.csv", "Name,Start,End,Size\nmain,0x00100000,0x00100040,64\nhelper,0x00100040,0x00100050,16\n"));
    CHECK_EQ(ours.size(), size_t(2));
    CHECK_EQ(ours[0].name, std::string("main"));
    CHECK_EQ(ours[1].end.value(), 0x00100050u);

    auto ghidraFns = loadFunctionMap(writeTemp("gf.csv", "\"Name\",\"Location\",\"Function Signature\",\"Function Size\"\n"
                                                         "\"FUN_00100000\",\"00100000\",\"undefined FUN_00100000(void)\",\"64\"\n"));
    CHECK_EQ(ghidraFns.size(), size_t(1));
    CHECK_EQ(ghidraFns[0].start, 0x00100000u);
    CHECK_EQ(ghidraFns[0].end.value(), 0x00100040u);

    auto symtab = loadFunctionMap(writeTemp("st.csv", "Name,Location,Type,Namespace\nmain,ram:00100000,Function,Global\nlabel,ram:00100010,Label,Global\n"));
    CHECK_EQ(symtab.size(), size_t(1));
    CHECK(!symtab[0].end.has_value());

    CHECK_THROWS(loadFunctionMap(writeTemp("bad.csv", "foo,bar\n1,2\n")));
    const auto entries = loadEntriesFile(writeTemp("missing.toml", "# comment 0xdeadbeef\nentries = [\n  0x00100010,\n  0x00100020,\n]\n"));
    CHECK_EQ(entries.size(), size_t(2));
}

TEST_CASE(analysis_resolves_jump_tables) {
    const ElfFile elf = assemble(kSwitchProgram);
    const Config cfg = configFor(elf);
    const Program p = analyze(elf, cfg);
    const Function* f = byName(p, "classify");
    CHECK(f != nullptr);
    if (!f) return;
    CHECK_EQ(f->jumpTables.size(), size_t(1));
    if (!f->jumpTables.empty()) CHECK_EQ(f->jumpTables.begin()->second.size(), size_t(3));
    CHECK(f->unresolvedJumps.empty());
    CHECK_EQ(p.resolvedJumpTables, size_t(1));
    // Call return sites are resume points.
    const Function* start = byName(p, "_start");
    CHECK(start && start->resume.count(start->start + 8));
}

// GCC under register pressure: the table address is computed once, spilled to
// the stack, and reloaded next to the jump.
TEST_CASE(analysis_resolves_spilled_jump_tables) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
        .ent _start
_start: addiu $sp, $sp, -32
        lui   $v1, %hi(table)
        addiu $v1, $v1, %lo(table)
        sw    $v1, 20($sp)
        sltiu $a1, $a0, 3
        beqz  $a1, 9f
        sll   $v0, $a0, 2
        lw    $v1, 20($sp)
        addu  $v0, $v1, $v0
        lw    $v0, 0($v0)
        jr    $v0
        nop
c0:     b     9f
        li    $v0, 10
c1:     b     9f
        li    $v0, 11
c2:     li    $v0, 12
9:      jr    $ra
        addiu $sp, $sp, 32
        .end _start
        .rodata
table:  .word c0, c1, c2
)");
    const Program p = analyze(elf, configFor(elf));
    const Function* f = byName(p, "_start");
    CHECK(f != nullptr);
    if (!f) return;
    CHECK_EQ(f->jumpTables.size(), size_t(1));
    if (!f->jumpTables.empty()) CHECK_EQ(f->jumpTables.begin()->second.size(), size_t(3));
    CHECK(f->unresolvedJumps.empty());
}

TEST_CASE(analysis_stripped_elf) {
    // Without symbols, functions come from the entry point, JAL targets and code pointers.
    const ElfFile elf = assemble(R"(
        .text
_start: jal   helper
        nop
        la    $a0, callback
        jalr  $a0
        nop
1:      b     1b
        nop
helper: jr    $ra
        li    $v0, 1
        nop
        nop
callback:
        addiu $sp, $sp, -16
        jr    $ra
        addiu $sp, $sp, 16
)", false);
    const Config cfg = configFor(elf);
    const Program p = analyze(elf, cfg);
    CHECK(p.functionAt(elf.entry) != nullptr);
    CHECK(p.functionAt(0x00100020) != nullptr);   // helper (JAL target)
    CHECK(p.functionAt(0x00100030) != nullptr);   // callback (lui/addiu pointer)
    CHECK(p.sourceCounts.count(Source::Jal));
}

// Code and data in one writable, executable section: the code ends at the
// first object, when every function lies below every object.
TEST_CASE(elf_splits_merged_code_and_data) {
    const ElfFile elf = ElfFile::parse(mergedElf(kRwx, kMergedSymbols), "merged.elf");
    const auto code = elf.codeRanges();
    const auto data = elf.dataRanges();
    CHECK_EQ(code.size(), size_t(1));
    CHECK_EQ(data.size(), size_t(1));
    if (code.size() == 1 && data.size() == 1) {
        CHECK_EQ(code[0].start, 0x00100000u);
        CHECK_EQ(code[0].end, 0x00100020u);
        CHECK_EQ(data[0].start, 0x00100020u);
        CHECK_EQ(data[0].end, 0x00100028u);
    }
    // An object among the functions: the boundary is unknown, so it all stays code.
    auto interleaved = kMergedSymbols;
    interleaved.push_back({"pool", 0x00100008, 4, elf::STT_OBJECT, elf::STB_LOCAL});
    const ElfFile mixed = ElfFile::parse(mergedElf(kRwx, interleaved), "mixed.elf");
    CHECK(mixed.isCode(0x00100024));
    CHECK(mixed.dataRanges().empty());
    // A read-only code section is code, as its header says.
    const ElfFile readOnly = ElfFile::parse(mergedElf(elf::SHF_ALLOC | elf::SHF_EXECINSTR, kMergedSymbols), "ro.elf");
    CHECK(readOnly.isCode(0x00100024));
    CHECK(readOnly.dataRanges().empty());
}

// The analysis makes no function of the data, and finds the code addresses it holds.
TEST_CASE(analysis_merged_code_and_data) {
    const ElfFile elf = ElfFile::parse(mergedElf(kRwx, kMergedSymbols), "merged.elf");
    const Program p = analyze(elf, configFor(elf));
    CHECK(p.functionContaining(0x00100020) == nullptr);   // __data_start is a label, not a function
    CHECK_EQ(p.functions.size(), size_t(1));
    const Function* start = byName(p, "_start");
    CHECK(start != nullptr);
    if (!start) return;
    CHECK_EQ(start->end, 0x00100018u);
    CHECK(start->resume.count(0x00100010));   // table[0]: where the jr can go
    CHECK(start->switchCandidates.count(0x00100010));
}

TEST_CASE(analysis_back_edges_and_preemption) {
    const char* src = R"(
        .text
        .globl _start
        .ent _start
_start: li    $t0, 10
1:      addiu $t0, $t0, -1
        bne   $t0, $zero, 1b
        nop
        jr    $ra
        nop
        .end _start
)";
    const ElfFile elf = assemble(src);
    Config cfg = configFor(elf);
    Program p = analyze(elf, cfg);
    CHECK(p.functions[0].resume.count(0x00100004));
    cfg.preemptLoops = false;
    p = analyze(elf, cfg);
    CHECK(!p.functions[0].resume.count(0x00100004));
    CHECK(p.functions[0].labels.count(0x00100004));
}

TEST_CASE(analysis_bindings) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
        .ent _start
_start: jal   printf
        nop
        jal   memcpy
        nop
        jr    $ra
        nop
        .end _start
        .globl printf
        .ent printf
printf: jr    $ra
        nop
        .end printf
        .globl memcpy
        .ent memcpy
memcpy: jr    $ra
        nop
        .end memcpy
        .globl helper
        .ent helper
helper: jr    $ra
        nop
        .end helper
)");
    Config cfg = configFor(elf);
    cfg.stubExclude = {"memcpy"};
    cfg.skip.push_back(parseFunctionRef("helper"));
    Program p = analyze(elf, cfg);
    CHECK(byName(p, "printf")->binding == Binding::Stub);
    CHECK(byName(p, "memcpy")->binding == Binding::Recompile);
    CHECK(byName(p, "helper")->binding == Binding::Skip);

    cfg.stubBindings.push_back({parseFunctionRef(hex32(byName(p, "helper")->start)), "ret1"});
    cfg.skip.clear();
    p = analyze(elf, cfg);
    CHECK(byName(p, "helper")->binding == Binding::Stub);
    CHECK_EQ(byName(p, "helper")->handler, std::string("ret1"));

    cfg.stubBindings.push_back({parseFunctionRef("helper"), "no_such_handler"});
    CHECK_THROWS(analyze(elf, cfg));
}

TEST_CASE(analysis_patches) {
    const ElfFile elf = assemble(".text\n_start: jr $ra\n  nop\n");
    Config cfg = configFor(elf);
    cfg.patches.push_back({0x00100004, 0x24020007, 0x00000000});   // nop -> li $v0, 7
    Program p = analyze(elf, cfg);
    CHECK_EQ(p.word(0x00100004), 0x24020007u);
    cfg.patches[0].expect = 0x12345678;
    CHECK_THROWS(analyze(elf, cfg));
}

TEST_CASE(codegen_shapes) {
    const ElfFile elf = assemble(kSwitchProgram);
    const Config cfg = configFor(elf);
    const Program p = analyze(elf, cfg);
    const std::string start = emitFunction(p, *byName(p, "_start"), true);
    CHECK(start.find("PS2_CALL(0x") != std::string::npos);
    CHECK(start.find("switch (cpu.pc)") != std::string::npos);
    CHECK(start.find("PS2_BACKEDGE") != std::string::npos);
    const std::string cls = emitFunction(p, *byName(p, "classify"), true);
    CHECK(cls.find("switch (target)") != std::string::npos);
    CHECK(cls.find("SLTIU(T0, A0, 3);") != std::string::npos);
    CHECK(cls.find("// ") != std::string::npos);
    const std::string bare = emitFunction(p, *byName(p, "classify"), false);
    CHECK(bare.find("// 001") == std::string::npos);
}

TEST_CASE(codegen_writes_project) {
    const ElfFile elf = assemble(kSwitchProgram);
    Config cfg = configFor(elf);
    cfg.files = 3;
    const Program p = analyze(elf, cfg);
    const GenerateResult r = generate(p, cfg);
    CHECK_EQ(r.functionFiles, size_t(3));
    for (const char* f : {"functions.h", "recomp_funcs_000.cpp", "recomp_funcs_002.cpp", "recomp_table.cpp", "recomp_main.cpp", "CMakeLists.txt"})
        CHECK(fs::exists(cfg.outDir / f));
    const std::string table = readTextFile(cfg.outDir / "recomp_table.cpp");
    CHECK(table.find("classify_") != std::string::npos);
    // Each row ends with the checksum the runtime checks RAM against.
    const Function* cls = byName(p, "classify");
    CodeChecksum sum;
    for (uint32_t a = cls->start; a < cls->end; a += 4) sum.add(p.word(a));
    CHECK(table.find(strprintf("0x%08xu},", sum.value)) != std::string::npos);
    // Regenerating identical output rewrites nothing.
    const GenerateResult again = generate(p, cfg);
    CHECK(again.written.empty());
}

TEST_CASE(codegen_writes_other_programs) {
    // A game's executables usually share their addresses: each gets its own folder and namespace.
    const ElfFile bootElf = assemble(kSwitchProgram);
    const ElfFile mainElf = assemble(kSwitchProgram);
    Config cfg = configFor(bootElf);
    cfg.outDir = tempDir() / "multi";
    cfg.files = 1;
    Config mainCfg = configFor(mainElf);
    mainCfg.name = "MAIN.ELF";
    mainCfg.files = 1;
    const Program p = analyze(bootElf, cfg);
    const Program q = analyze(mainElf, mainCfg);
    generate(p, cfg, {{&q, &mainCfg, "MAIN_ELF"}});
    for (const char* f : {"MAIN_ELF/functions.h", "MAIN_ELF/recomp_funcs_000.cpp", "MAIN_ELF/recomp_table.cpp"})
        CHECK(fs::exists(cfg.outDir / f));
    CHECK(readTextFile(cfg.outDir / "MAIN_ELF" / "recomp_funcs_000.cpp").find("namespace ps2game_MAIN_ELF {") != std::string::npos);
    CHECK(readTextFile(cfg.outDir / "MAIN_ELF" / "recomp_table.cpp").find("\"MAIN.ELF\"") != std::string::npos);
    CHECK(readTextFile(cfg.outDir / "recomp_table.cpp").find("&::ps2game_MAIN_ELF::kGameInfo") != std::string::npos);
    CHECK(readTextFile(cfg.outDir / "CMakeLists.txt").find("MAIN_ELF/recomp_table.cpp") != std::string::npos);
}

TEST_CASE(vu_code_files) {
    // What the runtime records (ps2n_missing.vu.toml), read back; lines left out are zero.
    const fs::path code = writeTemp("vu-code.toml",
                                    "[[image]]\nunit = 1\nentries = [0x0000, 0x0010]\ntargets = [0x0018]\ncode = \"\"\"\n"
                                    "0000 400002ff8000033c 000002ff8000033c\n"
                                    "0010 000002ff10010005 400002ff8000033c 000002ff8000033c\n\"\"\"\n"
                                    "[[image]]\nunit = 0\nentries = [0x0008]\ncode = \"\"\"\n0008 400002ff8000033c 000002ff8000033c\n\"\"\"\n");
    const std::vector<VuImage> images = loadVuCode(code);
    CHECK_EQ(images.size(), size_t(2));
    CHECK_EQ(images[0].unit, 1);
    CHECK_EQ(images[0].micro.size(), size_t(16384));
    CHECK_EQ(images[0].entries.size(), size_t(2));
    CHECK(images[0].targets == std::vector<uint32_t>{0x18});
    uint64_t w;
    std::memcpy(&w, images[0].micro.data() + 0x10, 8);
    CHECK_EQ(w, 0x000002ff10010005ull);
    std::memcpy(&w, images[0].micro.data() + 0x28, 8);
    CHECK_EQ(w, 0ull);
    CHECK_EQ(images[1].micro.size(), size_t(4096));
    CHECK_THROWS(loadVuCode(writeTemp("vu-unit.toml", "[[image]]\nunit = 2\ncode = \"\"\n")));
    CHECK_THROWS(loadVuCode(writeTemp("vu-entry.toml", "[[image]]\nunit = 1\nentries = [0x0004]\ncode = \"\"\n")));
    CHECK_THROWS(loadVuCode(writeTemp("vu-size.toml", "[[image]]\nunit = 0\ncode = \"\"\"\n0ff8 0 0\n\"\"\"\n")));   // past VU0's 4 KB
    // The same micro memory twice is one function, entered at the entries of both.
    VuImage again = images[0];
    again.entries = {0x08};
    again.targets = {};
    VuCodegenStats stats;
    const std::string cpp = generateVuCode({images[0], images[1], again}, "game", &stats);
    CHECK_EQ(stats.images, size_t(2));
    CHECK_EQ(stats.entries, size_t(4));
    CHECK(cpp.find("{1, 0x0008u, &vu1_") != std::string::npos);
    CHECK(cpp.find("{0, 0x0008u, &vu0_") != std::string::npos);
    // [vu] code_files, relative to the config.
    const Config c = Config::load(writeTemp("vu.toml", "[input]\nelf = \"a.elf\"\n[vu]\ncode_files = [\"vu-code.toml\"]\n"));
    CHECK_EQ(c.vuCodeFiles.size(), size_t(1));
    CHECK_EQ(c.vuCodeFiles[0], (tempDir() / "vu-code.toml").lexically_normal());
    // A recompile with them writes recomp_vu.cpp and points the boot program's
    // table at it; a file that is not there yet is a warning.
    const ElfFile elf = assemble(kSwitchProgram);
    Config cfg = configFor(elf);
    cfg.outDir = tempDir() / "vu";
    cfg.files = 1;
    cfg.vuCodeFiles = {code, tempDir() / "not-yet.vu.toml"};
    const GenerateResult r = generate(analyze(elf, cfg), cfg, {});
    CHECK_EQ(r.vu.images, size_t(2));
    CHECK_EQ(r.warnings.size(), size_t(1));
    CHECK(fs::exists(cfg.outDir / "recomp_vu.cpp"));
    CHECK(readTextFile(cfg.outDir / "recomp_table.cpp").find("kVuPrograms, kVuProgramCount") != std::string::npos);
    CHECK(readTextFile(cfg.outDir / "CMakeLists.txt").find("recomp_vu.cpp") != std::string::npos);
}

int main(int argc, char** argv) {
    const int rc = ::testing::runAll(argc, argv);
    std::error_code ec;
    fs::remove_all(tempDir(), ec);
    return rc;
}
