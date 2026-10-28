// Config, function maps, analysis and code generation.
#include "testing.h"

#include "analysis.h"
#include "codegen.h"
#include "config.h"
#include "function_map.h"

#include "ps2uc/assembler.h"
#include "ps2uc/elf.h"
#include "ps2uc/util.h"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace ps2uc;
namespace fs = std::filesystem;

namespace {

fs::path tempDir() {
    static const fs::path d = [] {
        fs::path p = fs::temp_directory_path() / ("ps2uc_tests_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

int main(int argc, char** argv) {
    const int rc = ::testing::runAll(argc, argv);
    std::error_code ec;
    fs::remove_all(tempDir(), ec);
    return rc;
}
