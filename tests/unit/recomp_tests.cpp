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
    CHECK_CONTAINS(code, "const bool taken = g4 == uint64_t{0};");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 29u, taken ? 0x00100014u : 0x0010000Cu);");
    // The link is written before the delay slot, the block ends before the call.
    CHECK_CONTAINS(code, "g31 = 0x00100014u;");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 18u, 0x00100028u);");
    CHECK_CONTAINS(code, "PS2N_CALL(fn_00100028, 0x00100028u, 0x00100014u);");
    // A system call ends its block, made or not: the runtime counts the
    // block's cost either way, and a new block begins right after it.
    CHECK_CONTAINS(code, "PS2N_SYSCALL(acc, 9u, 0x00100014u, 0x0000000Cu);");
    // (An empty block would still count one cycle.)
    CHECK(code.find("PS2N_SPLIT_END(acc, 0u, 0x00100018u);") == std::string::npos);
    // At every label the function asks what the runtime knows of the block
    // that begins there, by the leader's place in the image's list.
    CHECK_CONTAINS(code, "L_00100000:\n\tPS2N_BLOCK_START(0, 0x00100000u);");
    CHECK_CONTAINS(code, "L_00100018:\n\tPS2N_BLOCK_START(3, 0x00100018u);");
    CHECK_CONTAINS(code, "L_00100028:\n\tPS2N_BLOCK_START(4, 0x00100028u);");
    // The target of a return is read before the delay slot runs.
    CHECK_CONTAINS(code, "const uint32_t target = static_cast<uint32_t>(g31);");
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

    // start, end, function; leaders 0 to 3 of the image's five; no variants.
    CHECK_CONTAINS(code, "{0x00100000u, 0x00100020u, &fn_00100000, 0, 4, 0, 0},");
    CHECK_CONTAINS(code, "{0x00100028u, 0x00100030u, &fn_00100028, 4, 1, 0, 0},");
    CHECK_CONTAINS(code, "0x00100000u, 0x0010000Cu, 0x00100014u, 0x00100018u, 0x00100028u,");
    CHECK_CONTAINS(code, "uint32_t blocks[5];");

    // For each leader, a fingerprint of the words from it to the next leader
    // or its function's end: here the first three instructions, and the
    // last two of the first function.
    const auto segment = [&](uint32_t from, uint32_t to) {
        CodeHash hash;
        for (uint32_t a = from; a < to; a += 4) {
            uint32_t w = 0;
            CHECK(elf.read32(a, w));
            hash.add(w);
        }
        return strprintf("0x%016llXull,", static_cast<unsigned long long>(hash.value));
    };
    CHECK_CONTAINS(code, "static const uint64_t segment_hashes[] = {\n\t" + segment(0x00100000u, 0x0010000Cu));
    CHECK_CONTAINS(code, segment(0x00100018u, 0x00100020u));
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
    // the block is counted whole: 4 instructions, 36 eighths. Unless the
    // runtime cut the block short where the second begins: then it ends
    // there, and the second function is entered.
    CHECK_EQ(stats.tails, size_t{1});
    CHECK_EQ(stats.tailInstructions, size_t{3});
    CHECK_CONTAINS(code, "if ((ps2n_end & kBlockEndMask) == 0x00100004u)");
    CHECK_CONTAINS(code, "PS2N_SPLIT_END(acc, 9u, 0x00100004u);");
    CHECK_CONTAINS(code, "PS2N_JUMP(fn_00100004, 0x00100004u);");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 36u, target);");
    // Entered at its own start, the second function counts 3.
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 27u, target);");
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
    CHECK_CONTAINS(code, "const bool taken = g5 == uint64_t{0};");
    CHECK_CONTAINS(code, "return leave(0x00100004u, acc + 112u, ps2n_start);");   // the division's 112, the branch not yet counted
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 123u, 0x0010000Cu);");   // not taken: the delay slot does not run
}

TEST_CASE(a_branch_in_a_delay_slot_is_dropped_as_pcsx2s_recompiler_drops_it) {
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
    // The first branch then has no delay slot: its block is the branch alone.
    CHECK_EQ(stats.leaves, size_t{0});
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 11u, taken ? 0x0010000Cu : 0x00100008u);");
}

TEST_CASE(what_cannot_be_compiled_in_a_delay_slot_leaves_the_branch_to_the_interpreter) {
    // An instruction that ends a block, where PCSX2's recompiler expects none.
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: addiu $v0, $zero, 1        # 00100000
        jr    $ra                  # 00100004
        ei                         # 00100008
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.leaves, size_t{1});
    // From the branch, with what was counted before it, in the block that began at the label.
    CHECK_CONTAINS(code, "return leave(0x00100004u, acc + 9u, ps2n_start);");
}

TEST_CASE(di_takes_effect_after_the_instruction_that_follows_it) {
    // As under PCSX2's recompiler (recDI).
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: di                         # 00100000
        addiu $v0, $zero, 1        # 00100004
        jr    $ra                  # 00100008
        nop                        # 0010000C
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.leaves, size_t{0});
    CHECK_EQ(stats.delayedDisables, size_t{1});
    const size_t after = code.find("g2 = val::ADDIU(uint64_t{0}, 1);");
    const size_t effect = code.find("DI();");
    CHECK(after != std::string::npos && effect != std::string::npos && after < effect);
    // di 7 + addiu 9 + jr 9 + nop 9 eighths: DI is counted where it stands.
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 34u, target);");
}

TEST_CASE(di_in_a_delay_slot_takes_effect_there) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: jr    $ra                  # 00100000
        di                         # 00100004
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.leaves, size_t{0});
    CHECK_EQ(stats.delayedDisables, size_t{0});
    CHECK_CONTAINS(code, "DI();");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 16u, target);");
}

TEST_CASE(di_before_a_branch_or_a_label_is_left_to_the_interpreter) {
    // Before a branch DI never takes effect under PCSX2's recompiler; before
    // an instruction where a block can begin, that instruction still belongs
    // to DI's block. The interpreter knows both.
    const ElfFile branch = assemble(R"(
        .text
        .globl _start
_start: di                         # 00100000
        jr    $ra                  # 00100004
        nop                        # 00100008
)");
    GenerateStats stats;
    std::string code = generateFor(branch, &stats);
    CHECK_EQ(stats.leaves, size_t{1});
    CHECK_CONTAINS(code, "return leave(0x00100000u, acc + 0u, ps2n_start);");

    const ElfFile label = assemble(R"(
        .text
        .globl _start
_start: beq   $a0, $zero, 1f       # 00100000
        nop                        # 00100004
        di                         # 00100008
1:      addiu $v0, $zero, 1        # 0010000C
        jr    $ra                  # 00100010
        nop                        # 00100014
)");
    GenerateStats stats2;
    code = generateFor(label, &stats2);
    CHECK_EQ(stats2.leaves, size_t{1});
    CHECK_CONTAINS(code, "return leave(0x00100008u, acc + 0u, ps2n_start);");
}

TEST_CASE(a_jump_through_a_register_goes_through_the_entry_switch) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: jr    $t0                  # 00100000
        nop                        # 00100004
)");
    const std::string code = generateFor(elf);
    CHECK_CONTAINS(code, "const uint32_t target = static_cast<uint32_t>(g8);");
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
_start: tlbwi                      # 00100000
        jr    $ra                  # 00100004
        tlbp                       # 00100008
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.delegated, size_t{2});
    CHECK_CONTAINS(code, "PS2N_DELEGATE(0x00100000u, 0x");
    CHECK_CONTAINS(code, "PS2N_DELEGATE_IN_SLOT(0x00100008u, 0x");
}

TEST_CASE(cop0_moves_call_their_semantics_and_hints_do_nothing) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: mfc0  $v0, $12             # 00100000
        mtc0  $v0, $9              # 00100004
        sync.p                     # 00100008
        mfc0  $a1, $9              # 0010000C
        jr    $ra                  # 00100010
        sync                       # 00100014
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.delegated, size_t{0});
    // The COP0 register is a number, not the name of a general register.
    CHECK_CONTAINS(code, "MFC0(V0, 12);");
    CHECK_CONTAINS(code, "MTC0(V0, 9);");
    CHECK_CONTAINS(code, "MFC0(A1, 9);");
    // A hint is counted like any instruction and carries out nothing.
    CHECK(code.find("HINT") == std::string::npos);
    // Before a write or a read of the count register the cycles of the block
    // so far are added to the cycle count, the instruction's own included:
    // mfc0 7 + mtc0 7, then sync 9 + mfc0 7. The block's end counts the rest.
    CHECK_CONTAINS(code, "PS2N_FLUSH(acc, 14u);\n\tMTC0(V0, 9);");
    CHECK_CONTAINS(code, "PS2N_FLUSH(acc, 16u);\n\tMFC0(A1, 9);");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 18u, target);");
}

TEST_CASE(cop2_instructions_are_carried_out_by_the_runtime) {
    // Whether VU0 is brought up to date before one, and whether the cycles so
    // far are added before that, PCSX2's recompiler decides from the whole
    // block; only the runtime knows the block.
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: cfc2  $v0, $vi16           # 00100000
        ctc2.i $t0, $vi1           # 00100004
        qmfc2 $v1, $vf1            # 00100008
        jr    $ra                  # 0010000C
        lqc2  $vf1, 16($sp)        # 00100010
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.cop2, size_t{4});
    CHECK_EQ(stats.delegated, size_t{0});
    // Each is handed the eighths counted up to and including itself, and
    // gives back what is left of them.
    CHECK_CONTAINS(code, "PS2N_COP2(acc, 9u, 0x00100000u);");
    CHECK_CONTAINS(code, "PS2N_COP2(acc, 9u, 0x00100004u);");
    CHECK_CONTAINS(code, "PS2N_COP2(acc, 9u, 0x00100008u);");
    CHECK_CONTAINS(code, "PS2N_COP2(acc, 23u, 0x00100010u);");   // the jump's 9 and the load's 14
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 0u, target);");
}

TEST_CASE(loads_and_stores_tell_the_runtime_when_memory_is_not_plain) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: lui   $v0, 0x1000          # 00100000
        lw    $v1, 0($v0)          # 00100004  an EE timer: PCSX2's recompiler ends the block after it
        sw    $v1, 16($sp)         # 00100008
        jr    $ra                  # 0010000C
        lw    $a0, 0($a1)          # 00100010
)");
    const Program prog = analyze(elf);
    // The instruction after the timer read is where the next block begins.
    CHECK(prog.functions[0].leaders == (std::vector<uint32_t>{0x00100000u, 0x00100008u}));

    GenerateOptions go;
    go.id = "test";
    const std::string code = generate(prog, go);
    CHECK_CONTAINS(code, "PS2N_LOAD_ENDS(ends, g3, int32_t, mem32_t, static_cast<uint32_t>(g2) + static_cast<uint32_t>(0), 0x00100004u, PS2N_PUT(2));");
    CHECK_CONTAINS(code, "if (ends) [[unlikely]]");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 23u, 0x00100008u);");
    CHECK_CONTAINS(code, "PS2N_STORE(mem32_t, static_cast<uint32_t>(g29) + static_cast<uint32_t>(16), g3, 0x00100008u, (void)0);");
    CHECK_CONTAINS(code, "PS2N_LOAD(g4, int32_t, mem32_t, static_cast<uint32_t>(g5) + static_cast<uint32_t>(0), 0x00100010u, (void)0);");
}

TEST_CASE(a_block_is_never_carried_across_a_page_boundary_unasked) {
    // PCSX2's recompiler ends a block at a 4 KB page boundary, so the
    // boundary is a leader, and the code that reaches it asks whether its
    // block ends there.
    std::string source = "        .text\n        .globl _start\n_start:\n";
    for (int i = 0; i < 1026; i++) source += "        addiu $v0, $v0, 1\n";
    source += "        jr $ra\n        nop\n";
    const ElfFile elf = assemble(source.c_str());
    const Program prog = analyze(elf);
    CHECK(prog.functions[0].leaders == (std::vector<uint32_t>{0x00100000u, 0x00101000u}));

    GenerateOptions go;
    go.id = "test";
    go.comments = false;
    const std::string code = generate(prog, go);
    CHECK_CONTAINS(code, "if ((ps2n_end & kBlockEndMask) != 0x00101000u)");
    CHECK_CONTAINS(code, "acc += 9216u;");                      // 1024 instructions of 9
    CHECK_CONTAINS(code, "goto M_00101000;");
    CHECK_CONTAINS(code, "PS2N_SPLIT_END(acc, 9216u, 0x00101000u);");
    CHECK_CONTAINS(code, "L_00101000:\n\tPS2N_BLOCK_START(1, 0x00101000u);\nM_00101000:\n");
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
    CHECK_CONTAINS(code, "f0 = val::ADD_S(f1, f2);");
    CHECK_CONTAINS(code, "PS2N_LOAD_F(f3, static_cast<uint32_t>(g29) + static_cast<uint32_t>(16), 0x00100004u, PS2N_PUT_F(0));");
    CHECK_CONTAINS(code, "fpu::setCondition(val::C_LT_S(f1, f2));");
    CHECK_CONTAINS(code, "const bool taken = fpuCondition();");
    CHECK_CONTAINS(code, "g2 = val::MFC1(f0);");
    CHECK_CONTAINS(code, "f4 = val::SQRT_S(f5);");
    CHECK_CONTAINS(code, "f6 = val::MADD_S(facc, f7, f8);");
    // The FPU's registers are held in locals as the general ones are, and put
    // back with them at the end of the block.
    CHECK_CONTAINS(code, "PS2N_GET_F(1);");
    CHECK_CONTAINS(code, "PS2N_GET_ACC();");
    CHECK_CONTAINS(code, "PS2N_PUT(2); PS2N_PUT_F(0); PS2N_PUT_F(3);");
    CHECK_CONTAINS(code, "uint32_t f0 = 0, f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0, f6 = 0, f7 = 0, f8 = 0, facc = 0;");
    // The square root is carried into the code after the label, unless the
    // block was cut short there; the block ends with the return and the
    // multiply-add in its delay slot.
    CHECK_CONTAINS(code, strprintf("acc += %uu;", static_cast<unsigned>(r5900::cycles(r5900::Op::SQRT_S))));
    CHECK_CONTAINS(code, strprintf("PS2N_SPLIT_END(acc, %uu, 0x00100018u);", static_cast<unsigned>(r5900::cycles(r5900::Op::SQRT_S))));
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
    // Reaching the new leader from the instruction before it, the block goes
    // on with 9 carried over, unless it was cut short there.
    CHECK_CONTAINS(code, "if ((ps2n_end & kBlockEndMask) != 0x00100004u)");
    CHECK_CONTAINS(code, "acc += 9u;");
    CHECK_CONTAINS(code, "PS2N_SPLIT_END(acc, 9u, 0x00100004u);");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 27u, target);");
}

TEST_CASE(idle_loops_are_marked) {
    // A loop that only loads and tests does the same thing every time round:
    // its time is skipped, if the block that runs it began at its start and
    // was not cut short, which the runtime knows. One that counts is never one.
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
    // lw 14 + beq 11 + nop 9; the last argument says when the branch goes back.
    CHECK_CONTAINS(code, "PS2N_BLOCK_END_LOOP(acc, 34u, taken ? 0x00100000u : 0x0010000Cu, taken);");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 29u, taken ? 0x0010000Cu : 0x00100018u);");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END_LOOP(acc, 20u, 0x00100018u, true);");

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
    CHECK_CONTAINS(files[1].text, "extern uint32_t blocks[5];");
    CHECK_CONTAINS(files[1].text, "\nuint32_t PS2N_CC fn_00100000(uint32_t pc);");
    CHECK_CONTAINS(files[1].text, "\nuint32_t PS2N_CC fn_00100028(uint32_t pc);");

    // The main file holds the tables and no function.
    CHECK_CONTAINS(files[0].text, "#include \"test.h\"");
    CHECK_CONTAINS(files[0].text, "\nuint32_t blocks[5];");
    CHECK_CONTAINS(files[0].text, "static const FunctionInfo functions[] = {");
    CHECK_CONTAINS(files[0].text, "const ImageInfo image = {");
    CHECK(!contains(files[0].text, "PS2N_BLOCK_START"));

    // The functions follow each other in address order: eight instructions in
    // the first part, two in the second. A call across parts is the same call.
    CHECK_CONTAINS(files[2].text, "#include \"test.h\"");
    CHECK_CONTAINS(files[2].text, "\nuint32_t PS2N_CC fn_00100000(uint32_t pc)\n{");
    CHECK_CONTAINS(files[2].text, "PS2N_CALL(fn_00100028, 0x00100028u, 0x00100014u);");
    CHECK(!contains(files[2].text, "fn_00100028(uint32_t pc)\n{"));
    CHECK_CONTAINS(files[3].text, "\nuint32_t PS2N_CC fn_00100028(uint32_t pc)\n{");

    // One part is one file, with everything local to it, as before.
    CHECK(generatedSuffixes(1) == std::vector<std::string>{".cpp"});
    CHECK(generatedSuffixes(2) == (std::vector<std::string>{".cpp", ".h", "_part001.cpp", "_part002.cpp"}));
    const std::string single = generateFor(elf);
    CHECK_CONTAINS(single, "static uint32_t PS2N_CC fn_00100000(uint32_t pc)\n{");
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

    // The scan the runtime feeds as it forms a block: every instruction up to
    // and including the branch, each with the word after it.
    r5900::TimeoutLoopScan scan;
    for (uint32_t a = 0x00100004u; a <= 0x00100010u; a += 4) scan.add(read(a), read(a + 4));
    CHECK(scan.possible);
    CHECK_EQ(scan.reg, 2);

    // Generated code has nothing for it: the runtime finds such a block when
    // it forms it and lets its time pass itself. The loop is compiled all the
    // same, for when PCSX2's setting switches the skip off.
    const std::string code = generateFor(elf);
    CHECK_CONTAINS(code, "L_00100004:\n\tPS2N_BLOCK_START(1, 0x00100004u);");
    CHECK_CONTAINS(code, "g2 = val::ADDIU(g2, -1);");
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

    // The function and its variant. The variant always asks the runtime
    // whether the block that begins is its code.
    CHECK_CONTAINS(code, "PS2N_BLOCK_START(2, 0x00100010u);");
    CHECK_CONTAINS(code, "static uint32_t PS2N_CC fn_00100010_v0(uint32_t pc)");
    CHECK_CONTAINS(code, "PS2N_BLOCK_START_VARIANT(2, 0x00100010u, 0);");
    CHECK_CONTAINS(code, "g2 = val::ADDIU(uint64_t{0}, 1);");
    CHECK_CONTAINS(code, "g2 = val::ADDIU(uint64_t{0}, 2);");
    // Callers are compiled against the function; the runtime passes them on.
    CHECK_CONTAINS(code, "PS2N_CALL(fn_00100010, 0x00100010u, 0x00100008u);");

    // The variant is told from the function by the fingerprints of its own
    // words, one for each of the function's leaders.
    CodeHash hash;
    hash.add(0x03E00008u);
    hash.add(0x24020002u);
    CHECK_CONTAINS(code, strprintf("static const uint64_t variant_hashes_0[] = {\n\t0x%016llXull,", static_cast<unsigned long long>(hash.value)));
    CHECK_CONTAINS(code, "{&fn_00100010_v0, variant_hashes_0},");
    CHECK_CONTAINS(code, "&fn_00100010, 2, 1, 0, 1},");   // its one leader is the image's third; one variant, the first
}

TEST_CASE(registers_are_kept_in_locals_within_straight_line_code) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: addiu $v0, $zero, 5        # 00100000
        addu  $v1, $v0, $a0        # 00100004
        mult  $v1, $a1             # 00100008
        sw    $v1, 0($sp)          # 0010000C
        jr    $ra                  # 00100010
        nop                        # 00100014
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    // The function has a local for each register it works on.
    CHECK_CONTAINS(code, "uint64_t g2 = 0, g3 = 0, g4 = 0, g29 = 0, g31 = 0;");
    // Computed on the locals: $zero is a literal, and a register is taken
    // from the core the first time it is read.
    CHECK_CONTAINS(code, "g2 = val::ADDIU(uint64_t{0}, 5);");
    CHECK_CONTAINS(code, "PS2N_GET(4);\n\tg3 = val::ADDU(g2, g4);");
    // An instruction carried out through register numbers works on the
    // core's copy: what it may read is put back first, and nothing else.
    CHECK_CONTAINS(code, "PS2N_PUT(3);\n\tMULT(ZERO, V1, A1);");
    // The local still holds what was put back. A store names the registers
    // to put back should its address not be plain memory.
    CHECK_CONTAINS(code, "PS2N_STORE(mem32_t, static_cast<uint32_t>(g29) + static_cast<uint32_t>(0), g3, 0x0010000Cu, PS2N_PUT(2));");
    // A function with loads or stores keeps the address table in a local.
    CHECK_CONTAINS(code, "\tconst vtlb_private::VTLBVirtual* const ps2n_vmap = addressTable();\n");
    // The block's end puts back what is still changed, before the event test.
    CHECK_CONTAINS(code, "PS2N_PUT(2);\n\t\tPS2N_BLOCK_END(acc,");
    CHECK_EQ(stats.onLocals, size_t{3});
}

TEST_CASE(a_likely_branch_puts_the_registers_back_on_both_sides) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: addiu $v0, $zero, 1        # 00100000
        beql  $a0, $zero, 1f       # 00100004
        addiu $v1, $zero, 2        # 00100008
        nop                        # 0010000C
1:      jr    $ra                  # 00100010
        nop                        # 00100014
)");
    const std::string code = generateFor(elf);
    // Taken: after the delay slot, with what it changed.
    CHECK_CONTAINS(code, "PS2N_PUT(2); PS2N_PUT(3);\n\t\t\tPS2N_BLOCK_END(acc,");
    // Not taken: the delay slot did not run.
    CHECK_CONTAINS(code, "else\n\t\t{\n\t\t\tPS2N_PUT(2);\n\t\t\tPS2N_BLOCK_END(acc,");
}

TEST_CASE(a_label_holds_only_what_every_way_into_it_holds) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: bne   $a0, $zero, 1f       # 00100000
        nop                        # 00100004
        addiu $v0, $v0, 1          # 00100008
1:      addiu $v0, $v0, 2          # 0010000C
        jr    $ra                  # 00100010
        nop                        # 00100014
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    // The code after the label reads $v0, but the branch, which can reach the
    // label as well, does not hold it: the label holds nothing. What the code
    // before it changed is put back, and the code after it takes the
    // register again.
    CHECK_CONTAINS(code, "g2 = val::ADDIU(g2, 1);");
    CHECK_CONTAINS(code, "PS2N_PUT(2);\n\tif ((ps2n_end & kBlockEndMask) != 0x0010000Cu)");
    CHECK_CONTAINS(code, "M_0010000C:\n\tPS2N_GET(2);\n\tg2 = val::ADDIU(g2, 2);");
    CHECK_EQ(stats.labelsHolding, size_t{0});
}

TEST_CASE(a_loop_keeps_what_it_reads_in_locals_across_its_label) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: lw    $a1, 0($a0)          # 00100000
1:      lw    $v0, 4($a1)          # 00100004
        addiu $a1, $a1, 8          # 00100008
        bne   $v0, $zero, 1b       # 0010000C
        nop                        # 00100010
        jr    $ra                  # 00100014
        nop                        # 00100018
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    // Both ways into the loop's label hold $a1, and the loop reads it: the
    // label holds it. What was changed is put back at the block's end, as
    // ever, but the loop does not take it from the core again.
    CHECK_CONTAINS(code, "M_00100004:\n\tPS2N_LOAD(g2, int32_t, mem32_t, static_cast<uint32_t>(g5) + static_cast<uint32_t>(4),");
    CHECK_CONTAINS(code, "PS2N_PUT(2); PS2N_PUT(5);\n\t\tPS2N_BLOCK_END(acc, 43u, taken ? 0x00100004u : 0x00100014u);");
    // An entry from outside the function takes it from the core first.
    CHECK_CONTAINS(code, "case 0x00100004u: goto E_00100004;");
    CHECK_CONTAINS(code, "E_00100004:\n\tPS2N_GET(5);\n\tgoto L_00100004;");
    // A call enters at the start, where nothing is held.
    CHECK_CONTAINS(code, "if (pc == 0x00100000u) [[likely]]\n\t\tgoto L_00100000;");
    CHECK_EQ(stats.labelsHolding, size_t{1});
    CHECK_EQ(stats.registersAtLabels, size_t{1});
}

TEST_CASE(registry_lists_the_images) {
    const std::string code = generateRegistry({"a", "b_c"});
    CHECK_CONTAINS(code, "&ps2n_image_a::image,");
    CHECK_CONTAINS(code, "&ps2n_image_b_c::image,");
    CHECK_CONTAINS(code, "const size_t ps2n_image_count = 2;");
    CHECK_THROWS(generateRegistry({"not an identifier"}));
}

int main(int argc, char** argv) { return ::testing::runAll(argc, argv); }
