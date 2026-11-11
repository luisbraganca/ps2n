// SPDX-License-Identifier: GPL-3.0-or-later
// The static recompiler: what the analysis finds and what the generator writes.
//
// Whether generated code behaves like the program it was made from is not
// tested here but on the PCSX2 core, by the lockstep tests (tests/tests.cmake).
#include "testing.h"

#include "analysis.h"
#include "codegen.h"

#include "ps2n/assembler.h"
#include "ps2n/codehash.h"
#include "ps2n/elf.h"
#include "ps2n/util.h"
#include "ps2n/waitloop.h"

#include <string>
#include <vector>

using namespace ps2n;
using namespace ps2n::recomp;

namespace {

ElfFile assemble(const char* source) {
    Assembler as;
    as.assemble(source, "test.s");
    return ElfFile::parse(as.buildElf(), "test.elf");
}

std::string generateFor(const ElfFile& elf, GenerateStats* stats = nullptr) {
    const Program prog = analyze(elf);
    GenerateOptions options;
    options.id = "test";
    return generate(prog, options, stats);
}

bool contains(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

#define CHECK_CONTAINS(text, what)                                                             \
    do {                                                                                       \
        if (!contains(text, what)) ::testing::fail(__FILE__, __LINE__, std::string("the generated code lacks: ") + (what)); \
    } while (0)

const char* const kCallsAndBranches = R"(
        .text
        .globl _start
        .ent _start
_start: addiu $sp, $sp, -16        # 00100000
        beq   $a0, $zero, 1f       # 00100004
        nop                        # 00100008
        jal   func                 # 0010000C
        nop                        # 00100010
1:      syscall                    # 00100014
        jr    $ra                  # 00100018
        addiu $sp, $sp, 16         # 0010001C
        .end _start
        nop                        # 00100020  padding
        nop                        # 00100024
        .ent func
func:   jr    $ra                  # 00100028
        nop                        # 0010002C
        .end func
)";

} // namespace

TEST_CASE(blocks_begin_where_the_rule_says) {
    const ElfFile elf = assemble(kCallsAndBranches);
    const Program prog = analyze(elf);

    CHECK_EQ(prog.functions.size(), size_t{2});
    const Function& start = prog.functions[0];
    CHECK_EQ(start.start, 0x00100000u);
    CHECK_EQ(start.end, 0x00100020u);
    CHECK_EQ(start.name, std::string("_start"));
    // The start; after the branch and its delay slot; the branch's target,
    // which is also where the call returns; after the system call.
    CHECK(start.leaders == (std::vector<uint32_t>{0x00100000u, 0x0010000Cu, 0x00100014u, 0x00100018u}));

    const Function& func = prog.functions[1];
    CHECK_EQ(func.start, 0x00100028u);
    CHECK_EQ(func.end, 0x00100030u);
    CHECK(func.leaders == (std::vector<uint32_t>{0x00100028u}));

    CHECK(prog.entryAt(0x00100014u) == &start);
    CHECK(prog.entryAt(0x00100010u) == nullptr);   // a delay slot nothing jumps to
    CHECK(prog.entryAt(0x00100020u) == nullptr);   // padding
}

TEST_CASE(generated_code_counts_time_in_blocks) {
    const ElfFile elf = assemble(kCallsAndBranches);
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);

    // addiu 9 + beq 11 + nop 9 eighths of a cycle; both ways the block ends
    // after the delay slot.
    CHECK_CONTAINS(code, "const bool taken = ru64(A0) == uint64_t{0};");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 29u, taken ? 0x00100014u : 0x0010000Cu);");
    // The link is written before the delay slot, the block ends before the call.
    CHECK_CONTAINS(code, "setLink(RA, 0x00100014u);");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 18u, 0x00100028u);");
    CHECK_CONTAINS(code, "PS2N_CALL(fn_00100028, 0x00100028u, 0x00100014u);");
    CHECK_CONTAINS(code, "PS2N_SYSTEM(acc, 9u, 0x00100014u, 0x0000000Cu);");
    // The target of a return is read before the delay slot runs.
    CHECK_CONTAINS(code, "const uint32_t target = ru32(RA);");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 18u, target);");
    CHECK_CONTAINS(code, "return target;");
    // Every block leader is an entry.
    CHECK_CONTAINS(code, "case 0x0010000Cu: goto L_0010000C;");
    CHECK_CONTAINS(code, "case 0x00100018u: goto L_00100018;");

    CHECK_EQ(stats.functions, size_t{2});
    CHECK_EQ(stats.tails, size_t{0});
    CHECK_EQ(stats.leaves, size_t{0});
    CHECK_EQ(stats.delegated, size_t{0});
}

TEST_CASE(the_tables_describe_the_code) {
    const ElfFile elf = assemble(kCallsAndBranches);
    const std::string code = generateFor(elf);

    CodeHash hash;
    for (uint32_t a = 0x00100000u; a < 0x00100020u; a += 4) {
        uint32_t w = 0;
        CHECK(elf.read32(a, w));
        hash.add(w);
    }
    // start, end, extent, function, hash; resume points 0 to 2; no variants.
    CHECK_CONTAINS(code, strprintf("{0x00100000u, 0x00100020u, 0x00100020u, &fn_00100000, 0x%016llXull, 0, 3, 0, 0},",
                                   static_cast<unsigned long long>(hash.value)));
    CHECK_CONTAINS(code, "0x0010000Cu, 0x00100014u, 0x00100018u,");
    CHECK_CONTAINS(code, strprintf("0x%08Xu,\n\t0x00100000u, 0x00100030u,", pcsx2Crc(elf)));

    // The CRC is PCSX2's: the exclusive-or of the file's 32-bit words.
    uint32_t crc = 0;
    const std::vector<uint8_t>& bytes = elf.bytes();
    for (size_t i = 0; i + 4 <= bytes.size(); i += 4)
        crc ^= static_cast<uint32_t>(bytes[i]) | static_cast<uint32_t>(bytes[i + 1]) << 8 | static_cast<uint32_t>(bytes[i + 2]) << 16 |
               static_cast<uint32_t>(bytes[i + 3]) << 24;
    CHECK_EQ(pcsx2Crc(elf), crc);
}

TEST_CASE(a_function_that_runs_on_is_followed_to_the_end_of_its_block) {
    // `second` is a function start in the middle of a block.
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
        .globl second
_start: addiu $v0, $zero, 1        # 00100000
second: addiu $v0, $v0, 1          # 00100004
        jr    $ra                  # 00100008
        nop                        # 0010000C
)");
    const Program prog = analyze(elf);
    CHECK_EQ(prog.functions.size(), size_t{2});
    CHECK_EQ(prog.functions[0].end, 0x00100004u);

    GenerateStats stats;
    GenerateOptions options;
    options.id = "test";
    const std::string code = generate(prog, options, &stats);

    // The first function compiles the second's instructions again, so that
    // the block is counted whole: 4 instructions, 36 eighths.
    CHECK_EQ(stats.tails, size_t{1});
    CHECK_EQ(stats.tailInstructions, size_t{3});
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 36u, target);");
    // Entered at its own start, the second function counts 3.
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 27u, target);");
    // The first is then checked against all the memory it was compiled from.
    CHECK_CONTAINS(code, "{0x00100000u, 0x00100004u, 0x00100010u, &fn_00100000,");
}

TEST_CASE(a_function_that_ends_between_blocks_jumps_to_the_next) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
        .globl second
_start: syscall                    # 00100000
second: jr    $ra                  # 00100004
        nop                        # 00100008
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.tails, size_t{0});
    CHECK_CONTAINS(code, "PS2N_JUMP(fn_00100004, 0x00100004u);");
}

TEST_CASE(a_likely_branch_with_a_break_after_it_is_only_left_when_taken) {
    // What GCC makes of a division.
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: div   $a0, $a1             # 00100000
        beql  $a1, $zero, 1f       # 00100004
        break 7                    # 00100008
1:      mflo  $v0                  # 0010000C
        jr    $ra                  # 00100010
        nop                        # 00100014
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.guards, size_t{1});
    CHECK_EQ(stats.leaves, size_t{0});
    CHECK_CONTAINS(code, "const bool taken = ru64(A1) == uint64_t{0};");
    CHECK_CONTAINS(code, "return leave(0x00100004u, acc + 112u);");   // the division's 112, the branch not yet counted
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 123u, 0x0010000Cu);");   // not taken: the delay slot does not run
}

TEST_CASE(a_branch_whose_delay_slot_is_a_branch_is_left_to_the_interpreter) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: beq   $a0, $zero, 1f       # 00100000
        beq   $a1, $zero, 1f       # 00100004
        nop                        # 00100008
1:      jr    $ra                  # 0010000C
        nop                        # 00100010
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.leaves, size_t{1});
    CHECK_CONTAINS(code, "return leave(0x00100000u, acc + 0u);");
}

TEST_CASE(a_jump_through_a_register_goes_through_the_entry_switch) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: jr    $t0                  # 00100000
        nop                        # 00100004
)");
    const std::string code = generateFor(elf);
    CHECK_CONTAINS(code, "const uint32_t target = ru32(T0);");
    CHECK_CONTAINS(code, "pc = target;");
    CHECK_CONTAINS(code, "goto ps2n_dispatch;");
    CHECK_CONTAINS(code, "default: PS2N_JUMP(ps2n::ee::jump, pc);");
}

TEST_CASE(padding_after_a_return_is_not_a_block) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: jr    $ra                  # 00100000
        nop                        # 00100004
        nop                        # 00100008  padding
        nop                        # 0010000C
        addiu $v0, $zero, 1        # 00100010  code nothing refers to
        jr    $ra                  # 00100014
        nop                        # 00100018
)");
    const Program prog = analyze(elf);
    CHECK_EQ(prog.functions.size(), size_t{1});
    // The padding is skipped; the code after it could be reached by a jump
    // computed at run time, so it is a block.
    CHECK(prog.functions[0].leaders == (std::vector<uint32_t>{0x00100000u, 0x00100010u}));
}

TEST_CASE(what_ps2native_has_no_semantics_for_is_delegated) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: mfc0  $v0, $12             # 00100000
        jr    $ra                  # 00100004
        mtc0  $v0, $12             # 00100008
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.delegated, size_t{2});
    CHECK_CONTAINS(code, "delegate(0x00100000u, 0x");
    CHECK_CONTAINS(code, "delegateInDelaySlot(0x00100008u, 0x");
}

TEST_CASE(vu0_control_register_transfers_call_their_semantics) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: cfc2  $v0, $vi16           # 00100000
        ctc2.i $t0, $vi1           # 00100004
        qmfc2 $v1, $vf1            # 00100008  still delegated
        jr    $ra                  # 0010000C
        cfc2.i $zero, $vi1         # 00100010
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    // The interlock bit is the last argument.
    CHECK_CONTAINS(code, "CFC2(V0, 16, false);");
    CHECK_CONTAINS(code, "CTC2(T0, 1, true);");
    CHECK_CONTAINS(code, "CFC2(ZERO, 1, true);");
    CHECK_EQ(stats.delegated, size_t{1});
    CHECK_CONTAINS(code, "delegate(0x00100008u, 0x");
}

TEST_CASE(fpu_instructions_call_the_fpu_semantics) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: add.s $f0, $f1, $f2        # 00100000
        lwc1  $f3, 16($sp)         # 00100004
        c.lt.s $f1, $f2            # 00100008
        bc1t  1f                   # 0010000C
        mfc1  $v0, $f0             # 00100010
        sqrt.s $f4, $f5            # 00100014
1:      jr    $ra                  # 00100018
        madd.s $f6, $f7, $f8       # 0010001C
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.delegated, size_t{0});
    CHECK_CONTAINS(code, "ADD_S(0, 1, 2);");
    CHECK_CONTAINS(code, "LWC1(3, SP, 16);");
    CHECK_CONTAINS(code, "C_LT_S(1, 2);");
    CHECK_CONTAINS(code, "const bool taken = fpuCondition();");
    CHECK_CONTAINS(code, "MFC1(V0, 0);");
    CHECK_CONTAINS(code, "SQRT_S(4, 5);");
    CHECK_CONTAINS(code, "MADD_S(6, 7, 8);");
    // The square root is carried into the block the label begins; the block
    // ends with the return and the multiply-add in its delay slot.
    CHECK_CONTAINS(code, strprintf("acc += %uu;", static_cast<unsigned>(r5900::cycles(r5900::Op::SQRT_S))));
    CHECK_CONTAINS(code, strprintf("PS2N_BLOCK_END(acc, %uu, target);",
                                   static_cast<unsigned>(r5900::cycles(r5900::Op::JR) + r5900::cycles(r5900::Op::MADD_S))));
}

TEST_CASE(captured_entries_become_block_leaders) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: addiu $v0, $zero, 1        # 00100000
        addiu $v0, $v0, 1          # 00100004
        jr    $ra                  # 00100008
        nop                        # 0010000C
)");
    Options options;
    options.capture.entries = {0x00100004u, 0x00200000u};
    const Program prog = analyze(elf, options);
    CHECK(prog.functions[0].leaders == (std::vector<uint32_t>{0x00100000u, 0x00100004u}));
    CHECK_EQ(prog.notes.size(), size_t{1});   // the address outside the code is reported, not used

    GenerateOptions go;
    go.id = "test";
    const std::string code = generate(prog, go);
    // Falling into the new leader keeps the block whole: 9 carried over.
    CHECK_CONTAINS(code, "acc += 9u;");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 27u, target);");
}

TEST_CASE(idle_loops_are_marked) {
    // A loop that only loads and tests does the same thing every time round:
    // its time is skipped. One that counts does not.
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: lw    $v0, 0($a0)          # 00100000
        beqz  $v0, _start          # 00100004
        nop                        # 00100008
1:      addiu $v1, $v1, 1          # 0010000C
        bne   $v1, $a1, 1b         # 00100010
        nop                        # 00100014
2:      b     2b                   # 00100018
        nop                        # 0010001C
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.idleLoops, size_t{2});
    // lw 14 + beq 11 + nop 9: the last number is the block when it began at the loop's start.
    CHECK_CONTAINS(code, "PS2N_BLOCK_END_LOOP(acc, 34u, taken ? 0x00100000u : 0x0010000Cu, taken, 34u);");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 29u, taken ? 0x0010000Cu : 0x00100018u);");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END_LOOP(acc, 20u, 0x00100018u, true, 20u);");

    // The test itself, as PCSX2's recompiler has it.
    r5900::WaitLoopScan scan;
    scan.add(0x8C820000u);   // lw $v0, 0($a0): reads $a0, which the loop never writes
    CHECK(scan.idle);
    scan.add(0x24840004u);   // addiu $a0, $a0, 4: writes a register the loop has read
    CHECK(!scan.idle);

    r5900::WaitLoopScan constant;
    constant.add(0x3C021000u);   // lui $v0, 0x1000: a constant
    constant.add(0x8C430000u);   // lw $v1, 0($v0): loaded through it
    constant.add(0x30630001u);   // andi $v1, $v1, 1: still nothing but loaded values
    CHECK(constant.idle);
    constant.add(0xAC430000u);   // sw: a store changes the machine
    CHECK(!constant.idle);
}

TEST_CASE(a_large_program_is_spread_over_several_files) {
    const ElfFile elf = assemble(kCallsAndBranches);
    const Program prog = analyze(elf);

    GenerateOptions go;
    go.id = "test";
    go.parts = 2;
    go.headerName = "test.h";
    const std::vector<GeneratedFile> files = generateFiles(prog, go);
    CHECK_EQ(files.size(), size_t{4});
    CHECK(files[0].suffix == ".cpp" && files[1].suffix == ".h" && files[2].suffix == "_part001.cpp" && files[3].suffix == "_part002.cpp");

    // The header declares what the parts share. Nothing is local to a file.
    CHECK_CONTAINS(files[1].text, "#pragma once");
    CHECK_CONTAINS(files[1].text, "extern const ps2n::ee::ImageInfo image;");
    CHECK_CONTAINS(files[1].text, "extern uint8_t state[2];");
    CHECK_CONTAINS(files[1].text, "\nuint32_t fn_00100000(uint32_t pc);");
    CHECK_CONTAINS(files[1].text, "\nuint32_t fn_00100028(uint32_t pc);");

    // The main file holds the tables and no function.
    CHECK_CONTAINS(files[0].text, "#include \"test.h\"");
    CHECK_CONTAINS(files[0].text, "uint8_t state[2];");
    CHECK_CONTAINS(files[0].text, "static const FunctionInfo functions[] = {");
    CHECK_CONTAINS(files[0].text, "const ImageInfo image = {");
    CHECK(!contains(files[0].text, "PS2N_ENTER"));

    // The functions follow each other in address order: eight instructions in
    // the first part, two in the second. A call across parts is the same call.
    CHECK_CONTAINS(files[2].text, "#include \"test.h\"");
    CHECK_CONTAINS(files[2].text, "\nuint32_t fn_00100000(uint32_t pc)\n{");
    CHECK_CONTAINS(files[2].text, "PS2N_CALL(fn_00100028, 0x00100028u, 0x00100014u);");
    CHECK(!contains(files[2].text, "fn_00100028(uint32_t pc)\n{"));
    CHECK_CONTAINS(files[3].text, "\nuint32_t fn_00100028(uint32_t pc)\n{");

    // One part is one file, with everything local to it, as before.
    CHECK(generatedSuffixes(1) == std::vector<std::string>{".cpp"});
    CHECK(generatedSuffixes(2) == (std::vector<std::string>{".cpp", ".h", "_part001.cpp", "_part002.cpp"}));
    const std::string single = generateFor(elf);
    CHECK_CONTAINS(single, "static uint32_t fn_00100000(uint32_t pc)\n{");
    CHECK_THROWS(generate(prog, go));   // generate() is for one part
}

TEST_CASE(timeout_loops_are_not_run) {
    // PCSX2's recompiler does not run a loop of nops that counts a register
    // down; it lets eight cycles a count pass instead. The test for such a
    // loop is its own (ps2n/waitloop.h).
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: lui   $v0, 0x10            # 00100000
1:      nop                        # 00100004  ps2sdk's delay loop
        nop                        # 00100008
        addiu $v0, $v0, -1         # 0010000C
        bne   $v0, $zero, 1b       # 00100010
        nop                        # 00100014
2:      nop                        # 00100018  the decrement in the delay slot: an ordinary loop
        bne   $v1, $zero, 2b       # 0010001C
        addiu $v1, $v1, -1         # 00100020
        jr    $ra                  # 00100024
        nop                        # 00100028
)");
    const Program prog = analyze(elf);
    const auto read = [&](uint32_t a) { return prog.inCode(a) ? prog.word(a) : 0xFFFFFFFFu; };

    const r5900::TimeoutLoop loop = r5900::findTimeoutLoop(0x00100004u, read);
    CHECK(loop.found);
    CHECK_EQ(loop.reg, 2u);                 // $v0
    CHECK_EQ(loop.end, 0x00100018u);        // after the delay slot
    CHECK(!r5900::findTimeoutLoop(0x00100000u, read).found);   // the block must begin where the branch goes
    CHECK(!r5900::findTimeoutLoop(0x00100008u, read).found);
    CHECK(!r5900::findTimeoutLoop(0x00100018u, read).found);   // the decrement comes after the branch

    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.timeoutLoops, size_t{1});
    // Only when a block begins at the label: nothing counted since the last block ended.
    CHECK_CONTAINS(code, "L_00100004:\n");
    CHECK_CONTAINS(code, "if (acc == 0)");
    CHECK_CONTAINS(code, "const uint32_t next = timeoutLoop(V0, 0x00100004u, 0x00100018u);");
    CHECK_CONTAINS(code, "if (next == 0x00100018u)");
    CHECK_CONTAINS(code, "goto L_00100018;");
    CHECK_CONTAINS(code, "if (next != 0x00100004u)");
    // The loop is compiled all the same, for the first time round, which the
    // block before runs into, and for when PCSX2's setting switches the skip off.
    CHECK_CONTAINS(code, "ADDIU(V0, V0, -1);");
}

TEST_CASE(what_is_not_a_timeout_loop) {
    // One instruction word per case, in the place of the decrement or of the
    // branch of the loop: nop; <decrement>; <branch back to the nop>; nop.
    const auto found = [](uint32_t decrement, uint32_t branch, uint32_t slot = 0, uint32_t start = 0x00100000u) {
        const uint32_t words[] = {0, decrement, branch, slot};
        return r5900::findTimeoutLoop(start, [&](uint32_t a) {
            const uint32_t i = (a - start) / 4;
            return i < 4 ? words[i] : 0xFFFFFFFFu;
        }).found;
    };
    const uint32_t kDecrement = 0x2442FFFFu;   // addiu $v0, $v0, -1
    const uint32_t kBranch = 0x1440FFFDu;      // bne $v0, $zero, back by three instructions

    CHECK(found(kDecrement, kBranch));
    CHECK(found(0x2042FFFFu, kBranch));        // addi counts as well
    CHECK(found(0x2442FFF0u, kBranch));        // any negative step
    CHECK(!found(0x24420001u, kBranch));       // counting up
    CHECK(!found(0x2462FFFFu, kBranch));       // addiu $v0, $v1, -1: not in place
    CHECK(!found(kDecrement, 0x1443FFFDu));    // bne $v0, $v1: not against $zero
    CHECK(!found(kDecrement, 0x1460FFFDu));    // bne $v1, $zero: another register
    CHECK(!found(kDecrement, 0x1040FFFDu));    // beq
    CHECK(!found(kDecrement, 0x1440FFFEu));    // not back to the block's first instruction
    CHECK(!found(kDecrement, kBranch, 0x24630001u));   // something in the delay slot
    CHECK(!found(0x00021040u, kBranch));       // sll $v0, $v0, 1: something else in the loop
    CHECK(!found(0, kBranch));                 // no decrement at all
    // Cut by a 4 KB page boundary, as the recompiler cuts the block there:
    // the nop is the last instruction of a page.
    CHECK(!found(kDecrement, kBranch, 0, 0x00100FFCu));
    CHECK(found(kDecrement, kBranch, 0, 0x00100FF0u));   // ends with the page: whole
}

TEST_CASE(capture_files_are_read) {
    const Capture capture = parseCapture(R"(
# what a run reported
entries = [
  0x00100080,
  0x00100090, 0x001000a0,   # two on a line
]

[[code]]
start = 0x00100080
words = [
  0x03e00008, 0x24020002,
]

[[code]]
start = 0x00100100
words = [ 0x00000000 ]
)", "capture.toml");
    CHECK(capture.entries == (std::vector<uint32_t>{0x00100080u, 0x00100090u, 0x001000A0u}));
    CHECK_EQ(capture.code.size(), size_t{2});
    CHECK_EQ(capture.code[0].start, 0x00100080u);
    CHECK(capture.code[0].words == (std::vector<uint32_t>{0x03E00008u, 0x24020002u}));
    CHECK_EQ(capture.code[1].start, 0x00100100u);
    CHECK(capture.code[1].words == (std::vector<uint32_t>{0u}));

    CHECK(parseCapture("entries = []\n", "empty.toml").entries.empty());

    // Several runs' reports become one: every address and every piece of code
    // once, in order, and what is written reads back as the same.
    Capture all = capture;
    all.add(parseCapture("entries = [ 0x00100090, 0x00100010 ]\n[[code]]\nstart = 0x00100080\nwords = [ 0x03e00008, 0x24020002 ]\n"
                         "[[code]]\nstart = 0x00100080\nwords = [ 0x03e00008, 0x24020003 ]\n", "second.toml"));
    all.normalize();
    CHECK(all.entries == (std::vector<uint32_t>{0x00100010u, 0x00100080u, 0x00100090u, 0x001000A0u}));
    CHECK_EQ(all.code.size(), size_t{3});   // the same code twice counts once; other code in the same place stays
    const Capture again = parseCapture(writeCapture(all, "a comment"), "written.toml");
    CHECK(again.entries == all.entries);
    CHECK_EQ(again.code.size(), size_t{3});
    CHECK(again.code[1].words == (std::vector<uint32_t>{0x03E00008u, 0x24020003u}));

    CHECK_THROWS(parseCapture("entries = [ 0x10\n", "x"));                          // not closed
    CHECK_THROWS(parseCapture("entries = [ banana ]\n", "x"));                      // not a number
    CHECK_THROWS(parseCapture("words = [ 1 ]\n", "x"));                             // outside a block
    CHECK_THROWS(parseCapture("[[code]]\nwords = [ 1 ]\n", "x"));                   // no start
    CHECK_THROWS(parseCapture("[[code]]\nstart = 0x00100002\nwords = [ 1 ]\n", "x"));   // not an instruction's address
    CHECK_THROWS(parseCapture("something = 1\n", "x"));
}

TEST_CASE(captured_code_becomes_a_variant) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
        .ent _start
_start: jal   answer               # 00100000
        nop                        # 00100004
1:      b     1b                   # 00100008
        nop                        # 0010000C
        .end _start
        .ent answer
answer: jr    $ra                  # 00100010
        addiu $v0, $zero, 1        # 00100014
        .end answer
)");
    Options options;
    options.capture.code.push_back({0x00100010u, {0x03E00008u, 0x24020002u}});   // jr $ra; addiu $v0, $zero, 2
    options.capture.code.push_back({0x00100010u, {0x03E00008u, 0x24020002u}});   // reported twice: one variant
    options.capture.code.push_back({0x00100010u, {0x03E00008u, 0x24020001u}});   // what the executable has: no variant
    options.capture.code.push_back({0x00100010u, {0x03E00008u}});                // not the function's size
    options.capture.code.push_back({0x00100004u, {0u, 0u}});                     // not a function's start
    const Program prog = analyze(elf, options);

    const Function* answer = prog.functionAt(0x00100010u);
    CHECK(answer != nullptr);
    CHECK_EQ(answer->variants.size(), size_t{1});
    CHECK_EQ(prog.notes.size(), size_t{2});

    GenerateStats stats;
    GenerateOptions go;
    go.id = "test";
    const std::string code = generate(prog, go, &stats);
    CHECK_EQ(stats.variants, size_t{1});
    CHECK_EQ(stats.functions, size_t{2});

    // The function and its variant: each runs in its own state.
    CHECK_CONTAINS(code, "PS2N_ENTER(state, image, 1, kTrusted, pc);");
    CHECK_CONTAINS(code, "static uint32_t fn_00100010_v0(uint32_t pc)");
    CHECK_CONTAINS(code, "PS2N_ENTER(state, image, 1, kFirstVariant + 0, pc);");
    CHECK_CONTAINS(code, "ADDIU(V0, ZERO, 1);");
    CHECK_CONTAINS(code, "ADDIU(V0, ZERO, 2);");
    // Callers are compiled against the function; the runtime passes them on.
    CHECK_CONTAINS(code, "PS2N_CALL(fn_00100010, 0x00100010u, 0x00100008u);");

    // The variant is told from the function by the hash of its own words.
    CodeHash hash;
    hash.add(0x03E00008u);
    hash.add(0x24020002u);
    CHECK_CONTAINS(code, strprintf("{&fn_00100010_v0, 0x%016llXull, 0x00100018u},", static_cast<unsigned long long>(hash.value)));
    CHECK_CONTAINS(code, "&fn_00100010, ");
    CHECK_CONTAINS(code, ", 1, 0, 0, 1},");   // no resume points of its own; one variant, the first
}

TEST_CASE(registry_lists_the_images) {
    const std::string code = generateRegistry({"a", "b_c"});
    CHECK_CONTAINS(code, "&ps2n_image_a::image,");
    CHECK_CONTAINS(code, "&ps2n_image_b_c::image,");
    CHECK_CONTAINS(code, "const size_t ps2n_image_count = 2;");
    CHECK_THROWS(generateRegistry({"not an identifier"}));
}

int main(int argc, char** argv) { return ::testing::runAll(argc, argv); }
