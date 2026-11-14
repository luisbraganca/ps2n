// SPDX-License-Identifier: GPL-3.0-or-later
// The static recompiler: what the analysis finds and what the generator writes.
//
// Whether generated code behaves like the program it was made from is not
// tested here but on the PCSX2 core, by the lockstep tests (tests/tests.cmake).
#include "testing.h"

#include "analysis.h"
#include "bios.h"
#include "codegen.h"
#include "vu_codegen.h"

#include "ps2n/assembler.h"
#include "ps2n/codehash.h"
#include "ps2n/elf.h"
#include "ps2n/util.h"
#include "ps2n/waitloop.h"

#include <cstring>
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

TEST_CASE(cop2_instructions_ask_the_runtime_what_their_block_does_first) {
    // Whether VU0 is brought up to date before one, and whether the cycles so
    // far are added before that, PCSX2's recompiler decides from the whole
    // block; only the runtime knows the block. The transfers compiled code
    // makes itself unless the runtime has something to do about VU0 first;
    // then the runtime does it all, with the changed registers put back first
    // and the one a transfer writes taken again after.
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
    CHECK_EQ(stats.cop2Moves, size_t{4});
    CHECK_EQ(stats.delegated, size_t{0});
    // Each is handed the eighths counted up to and including itself, and
    // gives back what is left of them. A control register gives 32 bits,
    // sign-extended; an integer register takes 16.
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 9u, 0x00100000u, (void)0, PS2N_GET(2), "
                         "g2 = static_cast<uint64_t>(static_cast<int64_t>(VU0.VI[16].SL)));");
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 9u, 0x00100004u, PS2N_PUT(2), (void)0, VU0.VI[1].US[0] = static_cast<uint16_t>(g8));");
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 9u, 0x00100008u, PS2N_PUT(2), PS2N_GET(3), "
                         "g3 = VU0.VF[1].UD[0]; PS2N_GPR_HIGH(3) = VU0.VF[1].UD[1]);");
    // The jump's 9 and the load's 14; $v0 and $v1, changed, are put back only on the runtime's way and a device's.
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 23u, 0x00100010u, PS2N_PUT(2); PS2N_PUT(3), (void)0, PS2N_LQC2(1, static_cast<uint32_t>(g29) + "
                         "static_cast<uint32_t>(16), 0x00100010u, PS2N_PUT(2); PS2N_PUT(3)));");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END(acc, 0u, target);");
    CHECK(code.find("PS2N_COP2(") == std::string::npos);
}

TEST_CASE(lq_and_sq_are_made_on_the_locals) {
    // As the COP2 transfers: a general register's local holds its bits 0 to
    // 63 and the core bits 64 to 127, and the address comes from a local.
    // LQ writes the whole register to the core as well, so its local holds
    // nothing changed and nothing is put back for it; SQ takes the low bits
    // from the local only where it holds them changed, and otherwise the
    // sixteen bytes the core has. A load into $zero still reads.
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: lq    $t0, 16($a0)         # 00100000
        sq    $t0, 32($a0)         # 00100004
        lq    $zero, 0($a1)        # 00100008
        daddu $t1, $t0, $t0        # 0010000C
        sq    $t1, 64($a0)         # 00100010
        jr    $ra                  # 00100014
        sq    $zero, 48($a0)       # 00100018
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.delegated, size_t{0});
    CHECK_CONTAINS(code, "PS2N_LQ(g8, 8, static_cast<uint32_t>(g4) + static_cast<uint32_t>(16), 0x00100000u, (void)0);");
    CHECK_CONTAINS(code, "PS2N_SQ_CORE(8, static_cast<uint32_t>(g4) + static_cast<uint32_t>(32), 0x00100004u, (void)0);");
    CHECK_CONTAINS(code, "PS2N_LQ(ps2n_unused, 0, static_cast<uint32_t>(g5) + static_cast<uint32_t>(0), 0x00100008u, (void)0);");
    CHECK_CONTAINS(code, "g9 = val::DADDU(g8, g8);");
    CHECK_CONTAINS(code, "PS2N_SQ(g9, 9, static_cast<uint32_t>(g4) + static_cast<uint32_t>(64), 0x00100010u, (void)0);");
    CHECK_CONTAINS(code, "PS2N_SQ_CORE(0, static_cast<uint32_t>(g4) + static_cast<uint32_t>(48), 0x00100018u, (void)0);");
    CHECK(code.find("PS2N_ACCESS") == std::string::npos);
    CHECK(code.find("PS2N_PUT(8)") == std::string::npos);
    CHECK(code.find("PS2N_PUT(9)") != std::string::npos); // before the block ends
}

TEST_CASE(cfc2_and_ctc2_are_moves_but_for_the_control_registers_whose_writing_does_more) {
    // As ops_cop2.cpp has them: R gives 23 bits and takes them under the
    // exponent of 1.0; VPU_STAT is read-only. Writing the status flag
    // register also rewrites the copies PCSX2's VU recompiler keeps, so that
    // CTC2 is the runtime's, as are those to FBRST and CMSAR1.
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: cfc2  $a0, $vi1            # 00100000
        cfc2  $a1, $vi20           # 00100004
        ctc2  $a0, $vi20           # 00100008
        ctc2  $a1, $vi16           # 0010000C
        ctc2  $a1, $vi29           # 00100010
        jr    $ra                  # 00100014
        nop                        # 00100018
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.cop2, size_t{5});
    CHECK_EQ(stats.cop2Moves, size_t{4});
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 9u, 0x00100000u, (void)0, PS2N_GET(4), g4 = uint64_t{VU0.VI[1].US[0]});");
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 9u, 0x00100004u, PS2N_PUT(4), PS2N_GET(5), g5 = uint64_t{VU0.VI[20].UL & 0x7FFFFFu});");
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 9u, 0x00100008u, PS2N_PUT(4); PS2N_PUT(5), (void)0, "
                         "VU0.VI[20].UL = (static_cast<uint32_t>(g4) & 0x7FFFFFu) | 0x3F800000u);");
    CHECK_CONTAINS(code, "PS2N_COP2(acc, 9u, 0x0010000Cu);");
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 9u, 0x00100010u, (void)0, (void)0, (void)0);");
}

TEST_CASE(cop2_moves_to_vu0_read_the_locals) {
    // QMTC2 takes bits 0 to 63 from the register's local and 64 to 127 from
    // the core; to VU0's register 0, which is constant, it moves nothing.
    // SQC2 stores through the address table.
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: addiu $a0, $a0, 1          # 00100000
        qmtc2 $a0, $vf2            # 00100004
        sqc2  $vf2, 32($a1)        # 00100008
        qmtc2 $a0, $vf0            # 0010000C
        jr    $ra                  # 00100010
        nop                        # 00100014
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.cop2Moves, size_t{3});
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 18u, 0x00100004u, PS2N_PUT(4), (void)0, VU0.VF[2].UD[0] = g4; VU0.VF[2].UD[1] = PS2N_GPR_HIGH(4));");
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 14u, 0x00100008u, PS2N_PUT(4), (void)0, PS2N_SQC2(2, static_cast<uint32_t>(g5) + "
                         "static_cast<uint32_t>(32), 0x00100008u, PS2N_PUT(4)));");
    CHECK_CONTAINS(code, "PS2N_COP2_MOVE(acc, 9u, 0x0010000Cu, PS2N_PUT(4), (void)0, (void)0);");
}

TEST_CASE(vu0_instructions_are_carried_out_by_compiled_code_where_the_runtime_lets_it) {
    // A VU0 instruction asks the runtime what its block does about VU0 first;
    // where nothing is to be done, compiled code carries it out itself
    // (PS2N_VU0). It touches no register of the EE's, so the locals are not
    // put back for it, nor for VCALLMS, PCSX2's function, called after the
    // cycles so far are added (PS2N_VCALLMS).
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: addiu $v0, $v0, 1            # 00100000
        vmul.xyz $vf1, $vf2, $vf3    # 00100004
        vaddx.xyzw $vf3, $vf1, $vf2x # 00100008
        vdiv  $Q, $vf1x, $vf2y       # 0010000C
        vcallms 0                    # 00100010
        jr    $ra                    # 00100014
        nop                          # 00100018
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.cop2, size_t{4});
    CHECK_EQ(stats.vu0, size_t{3});
    // The fields as constants, and the flags where the block lets them go.
    CHECK_CONTAINS(code, "PS2N_VU0(acc, 18u, 0x00100004u, VMUL(PS2N_VU0_FLAGS, 14, 1, 2, 3));");   // the addition's 9 and its own 9
    CHECK_CONTAINS(code, "PS2N_VU0(acc, 9u, 0x00100008u, VADDbc(PS2N_VU0_FLAGS, 15, 3, 1, 2, 0));");
    CHECK_CONTAINS(code, "PS2N_VU0(acc, 9u, 0x0010000Cu, VDIV(PS2N_VU0_FLAGS, 1, 0, 2, 1));");
    CHECK_CONTAINS(code, "PS2N_VCALLMS(acc, 9u, 0x00100010u, 0x4A000038u);");
    // $v0 stays in its local past the three VU0 instructions and VCALLMS.
    const size_t vcallms = code.find("PS2N_VCALLMS(");
    const size_t put = code.find("PS2N_PUT(2);");
    CHECK(vcallms != std::string::npos && put != std::string::npos && put > vcallms);
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
    CHECK_CONTAINS(code, "PS2N_LOAD_ENDS(ends, g3, int32_t, mem32_t, static_cast<uint32_t>(g2) + static_cast<uint32_t>(0), 0x00100004u, (void)0);");
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
    CHECK_CONTAINS(code, "PS2N_LOAD_F(f3, static_cast<uint32_t>(g29) + static_cast<uint32_t>(16), 0x00100004u, (void)0);");
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
    CHECK_CONTAINS(code, "val::Fpr f0 = {}, f1 = {}, f2 = {}, f3 = {}, f4 = {}, f5 = {}, f6 = {}, f7 = {}, f8 = {}, facc = {};");
    // The square root is carried into the code after the label, unless the
    // block was cut short there; the block ends with the return and the
    // multiply-add in its delay slot.
    CHECK_CONTAINS(code, strprintf("acc += %uu;", static_cast<unsigned>(r5900::cycles(r5900::Op::SQRT_S))));
    CHECK_CONTAINS(code, strprintf("PS2N_SPLIT_END(acc, %uu, 0x00100018u);", static_cast<unsigned>(r5900::cycles(r5900::Op::SQRT_S))));
    CHECK_CONTAINS(code, strprintf("PS2N_BLOCK_END(acc, %uu, target);",
                                   static_cast<unsigned>(r5900::cycles(r5900::Op::JR) + r5900::cycles(r5900::Op::MADD_S))));
}

TEST_CASE(fpu_registers_stay_in_their_locals_to_the_store) {
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: mtc1  $a0, $f1             # 00100000
        mov.s $f2, $f1             # 00100004
        swc1  $f2, 8($sp)          # 00100008
        jr    $ra                  # 0010000C
        nop                        # 00100010
)");
    const std::string code = generateFor(elf);
    CHECK_CONTAINS(code, "f1 = val::MTC1(g4);");
    CHECK_CONTAINS(code, "f2 = f1;");
    // The store takes the register's 32 bits from its local (recomp.h).
    CHECK_CONTAINS(code, "PS2N_STORE_F(static_cast<uint32_t>(g29) + static_cast<uint32_t>(8), f2, 0x00100008u, (void)0);");
    CHECK_CONTAINS(code, "val::Fpr f1 = {}, f2 = {};");
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

TEST_CASE(loops_that_may_reach_a_fixed_point_are_marked) {
    // A loop of one block that loads, computes and stores, and writes no
    // register it reads before writing it: once a time round has stored only
    // what memory held, the times round after it up to the next event test
    // are not run. One that counts writes a register it reads first, and
    // never reaches one. One that only loads may be an idle loop of PCSX2's
    // recompiler, and keeps that treatment.
    const ElfFile elf = assemble(R"(
        .text
        .globl _start
_start: lw    $v0, 0($a0)          # 00100000
        sw    $v0, 4($a0)          # 00100004
        beq   $v0, $a1, _start     # 00100008
        sw    $zero, 8($a0)        # 0010000C
1:      addiu $v1, $v1, 1          # 00100010
        sw    $v1, 0($a0)          # 00100014
        bne   $v1, $a1, 1b         # 00100018
        nop                        # 0010001C
2:      lw    $v0, 0($a0)          # 00100020
        beqz  $v0, 2b              # 00100024
        nop                        # 00100028
        jr    $ra                  # 0010002C
        nop                        # 00100030
)");
    GenerateStats stats;
    const std::string code = generateFor(elf, &stats);
    CHECK_EQ(stats.fixedLoops, size_t{1});
    CHECK_EQ(stats.idleLoops, size_t{1});
    CHECK_CONTAINS(code, "bool ps2n_same = false;");
    CHECK_CONTAINS(code, "ps2n_same = true;");
    // lw 14 + sw 14 + beq 11 + sw 14 in the delay slot; the loop's start, and its leader.
    CHECK_CONTAINS(code, "PS2N_BLOCK_END_FIXED(acc, 53u, taken ? 0x00100000u : 0x00100010u, 0x00100000u, 0);");
    // Its stores compare, the delay slot's too; its load is kept from being
    // one that changed nothing when it is not of plain memory.
    CHECK_CONTAINS(code, "PS2N_STORE_SAME(mem32_t, static_cast<uint32_t>(g4) + static_cast<uint32_t>(4), g2,");
    CHECK_CONTAINS(code, "PS2N_STORE_SAME(mem32_t, static_cast<uint32_t>(g4) + static_cast<uint32_t>(8), uint64_t{0},");
    CHECK_CONTAINS(code, ", 0x00100000u, ps2n_same = false);");
    // The other loops end their blocks as before.
    CHECK_CONTAINS(code, "PS2N_STORE(mem32_t, static_cast<uint32_t>(g4) + static_cast<uint32_t>(0), g3,");
    CHECK_CONTAINS(code, "PS2N_BLOCK_END_LOOP(acc, 34u, taken ? 0x00100020u : 0x0010002Cu, taken);");
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
    // The local still holds what was put back. A store puts nothing back,
    // not even should its address not be plain memory: nothing on that way
    // reads the EE's registers.
    CHECK_CONTAINS(code, "PS2N_STORE(mem32_t, static_cast<uint32_t>(g29) + static_cast<uint32_t>(0), g3, 0x0010000Cu, (void)0);");
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
    CHECK_CONTAINS(code, "const size_t ps2n_vu_image_count = 0;");
    CHECK_THROWS(generateRegistry({"not an identifier"}));

    // The images of compiled micro code are listed apart.
    const std::string both = generateRegistry({"a"}, {"game", "selftest"});
    CHECK_CONTAINS(both, "&ps2n_vu_image_game::image,");
    CHECK_CONTAINS(both, "&ps2n_vu_image_selftest::image,");
    CHECK_CONTAINS(both, "const size_t ps2n_vu_image_count = 2;");
    CHECK_THROWS(generateRegistry({"a"}, {"not an identifier"}));
}

// ---- the vector units' micro code (docs/game-log.md, "G11") ---------------------

TEST_CASE(captures_of_micro_code_are_read) {
    const VuCapture capture = parseVuCapture(R"(
# what a run reported
[[block]]
vu = 1
at = 0x1fd8
key = "whatever a run wrote: it is made again"
words = [
  0x8000033c, 0x000002ff, 0x01234567, 0x400002ff,
]
steps = [
  "ViLoad 5 3",
  "FMACa 0x1e3082c 0x123 0x45",   # a comment
  "T0Backup",
]

[[block]]
vu = 0
at = 8
words = []
steps = [ "ViZero 2", "TBit 0 0" ]
)", "vu-capture.toml");
    CHECK_EQ(capture.blocks.size(), size_t{2});
    const VuCapture::Block& b = capture.blocks[0];
    CHECK_EQ(b.vu, 1);
    CHECK_EQ(b.at, 0x1fd8u);
    CHECK(b.words == (std::vector<uint64_t>{0x000002ff8000033cull, 0x400002ff01234567ull}));
    CHECK_EQ(b.steps.size(), size_t{3});
    CHECK(b.steps[0].op == vu::StepOp::ViLoad);
    CHECK_EQ(b.steps[0].o[0], 5u);
    CHECK_EQ(b.steps[0].o[1], 3u);
    CHECK(b.steps[1].op == vu::StepOp::FMACa);
    CHECK_EQ(b.steps[1].o[0], 0x1e3082cu);
    CHECK(b.steps[2].op == vu::StepOp::T0Backup);
    CHECK(b.key == vu::planKey(1, b.steps.data(), b.steps.size()));
    CHECK_EQ(capture.blocks[1].vu, 0);
    CHECK_EQ(capture.blocks[1].steps.size(), size_t{2});

    // The key is of the VU and the steps with their operands: the same steps
    // for the other VU, or with another operand, are another plan.
    CHECK(!(vu::planKey(0, b.steps.data(), b.steps.size()) == b.key));
    std::vector<vu::Step> other = b.steps;
    other[0].o[1] = 4;
    CHECK(!(vu::planKey(1, other.data(), other.size()) == b.key));

    // Several runs' reports become one: every plan once, in the order of
    // their keys, and what is written reads back as the same.
    VuCapture all = capture;
    all.add(capture);
    all.add(parseVuCapture("[[block]]\nvu = 1\nsteps = [ \"ViZero 5\" ]\n", "second.toml"));
    all.normalize();
    CHECK_EQ(all.blocks.size(), size_t{3});
    CHECK_EQ(all.blocks[0].vu, 0);
    const VuCapture again = parseVuCapture(writeVuCapture(all, "a comment"), "written.toml");
    CHECK_EQ(again.blocks.size(), size_t{3});
    for (size_t i = 0; i < 3 && i < again.blocks.size(); ++i) {
        CHECK(again.blocks[i].key == all.blocks[i].key);
        CHECK(again.blocks[i].steps == all.blocks[i].steps);
        CHECK(again.blocks[i].words == all.blocks[i].words);
        CHECK_EQ(again.blocks[i].at, all.blocks[i].at);
    }

    CHECK(parseVuCapture("# nothing\n", "empty.toml").blocks.empty());
    CHECK_THROWS(parseVuCapture("vu = 1\n", "x"));                                              // outside a block
    CHECK_THROWS(parseVuCapture("[[block]]\nsteps = [ \"ViZero 5\" ]\n", "x"));                 // no VU
    CHECK_THROWS(parseVuCapture("[[block]]\nvu = 2\nsteps = []\n", "x"));                       // no such VU
    CHECK_THROWS(parseVuCapture("[[block]]\nvu = 1\nsteps = [ \"Banana 5\" ]\n", "x"));         // no such step
    CHECK_THROWS(parseVuCapture("[[block]]\nvu = 1\nsteps = [ \"ViZero\" ]\n", "x"));           // an operand missing
    CHECK_THROWS(parseVuCapture("[[block]]\nvu = 1\nsteps = [ \"ViZero 5 6\" ]\n", "x"));       // one too many
    CHECK_THROWS(parseVuCapture("[[block]]\nvu = 1\nsteps = [ \"ViZero 5\"\n", "x"));           // not closed
    CHECK_THROWS(parseVuCapture("[[block]]\nvu = 1\nwords = [ 1 ]\nsteps = []\n", "x"));        // half a pair
    CHECK_THROWS(parseVuCapture("[[block]]\nvu = 1\nsomething = 1\n", "x"));
}

TEST_CASE(a_plan_becomes_a_function_of_its_steps) {
    VuCapture capture = parseVuCapture(R"(
[[block]]
vu = 1
at = 0x1fd8
words = [ 0x8000033c, 0x000002ff ]
steps = [
  "ViLoad 5 3",
  "FMACa 0x1e3082c 0x123 0x45",
  "TBit 0 1",
  "T0Backup",
  "Leave 2 0x29 0x1b09",
]
[[block]]
vu = 0
steps = []
)", "vu-capture.toml");
    capture.normalize();

    VuGenerateOptions options;
    options.id = "test";
    VuGenerateStats stats;
    const std::vector<GeneratedFile> files = generateVuFiles(capture, options, &stats);
    CHECK_EQ(files.size(), size_t{1});
    CHECK_EQ(stats.blocks[0], size_t{1});
    CHECK_EQ(stats.blocks[1], size_t{1});
    CHECK_EQ(stats.steps, size_t{5});
    const std::string& code = files[0].text;
    const std::string name = "b1_" + vu::planKeyText(capture.blocks[1].key);
    const std::string none = "b0_" + vu::planKeyText(capture.blocks[0].key);

    // Each step is a call of the step's function with its operands as
    // constants, on the accessor the block was given.
    CHECK_CONTAINS(code, "namespace ps2n_vu_image_test");
    CHECK_CONTAINS(code, "static const Link* PS2N_VU_CC " + name + "(Ctx& c, const Link* b)\n{\n\tPS2N_VU_ACCESS a(c, b->base);\n");
    CHECK_CONTAINS(code, "\tstep::ViLoad(a, 5u, 3u);\n\tstep::FMACa(a, 0x1e3082cu, 0x123u, 0x45u);\n");
    // A T bit may end the program where it stands: what the steps wrote goes
    // back first, and the block says which of its T bits it was. The last
    // step is how the program goes on: the block's link, what the steps wrote
    // put back, and the block goes on to the next by itself.
    CHECK_CONTAINS(code, "\tif (step::TBit(a, 0u, 1u))\n\t{\n\t\ta.finish();\n\t\treturn Stop(c, b, kStopTBit + 0u);\n\t}\n"
                         "\tstep::T0Backup(a);\n\tPS2N_VU_LEAVE(2u, 0x29u, 0x1b09u);\n}");
    // A plan without that step stops the program at its end, for the provider.
    CHECK_CONTAINS(code, "static const Link* PS2N_VU_CC " + none + "(Ctx& c, const Link* b)\n{\n\tPS2N_VU_ACCESS a(c, b->base);\n"
                         "\ta.finish();\n\treturn Stop(c, b, kStopEnd);\n}");
    // The table the runtime finds them by: the plan's key, the function, the VU.
    CHECK_CONTAINS(code, strprintf("\t{0x%016llXull, 0x%016llXull, &%s, 1},\n", static_cast<unsigned long long>(capture.blocks[1].key.a),
                                   static_cast<unsigned long long>(capture.blocks[1].key.b), name.c_str()));
    CHECK_CONTAINS(code, "const ImageInfo image = {\n\t\"test\",\n\tblocks,\n\t2,");
    // Where the block was met and its pairs, for whoever reads the file.
    CHECK_CONTAINS(code, "// VU1, first met at 1fd8: 1 pairs, 5 steps\n//   000002ff:8000033c\n");

    // Spread over several files: the functions declared in a header, the
    // tables in the main file.
    options.parts = 2;
    options.headerName = "vu_test.h";
    const std::vector<GeneratedFile> parts = generateVuFiles(capture, options);
    CHECK_EQ(parts.size(), size_t{4});
    if (parts.size() == 4) {
        CHECK_EQ(parts[0].suffix, std::string(".cpp"));
        CHECK_EQ(parts[1].suffix, std::string(".h"));
        CHECK_EQ(parts[2].suffix, std::string("_part001.cpp"));
        CHECK_CONTAINS(parts[0].text, "#include \"vu_test.h\"");
        CHECK_CONTAINS(parts[0].text, "static const BlockInfo blocks[] = {");
        CHECK_CONTAINS(parts[1].text, "\nconst Link* PS2N_VU_CC " + name + "(Ctx& c, const Link* b);\n");
        CHECK(contains(parts[2].text + parts[3].text, "\nconst Link* PS2N_VU_CC " + name + "(Ctx& c, const Link* b)\n{"));
    }

    // A plan's last step is its last.
    VuCapture wrong = parseVuCapture("[[block]]\nvu = 1\nsteps = [ \"Leave 1 0 0\", \"T0Backup\" ]\n", "x");
    CHECK_THROWS(generateVuFiles(wrong, options));

    // No blocks at all is still an image: a host built before any run.
    VuGenerateOptions empty;
    empty.id = "none";
    const std::vector<GeneratedFile> nothing = generateVuFiles(VuCapture{}, empty);
    CHECK_CONTAINS(nothing[0].text, "const ImageInfo image = {\n\t\"none\",\n\tblocks,\n\t0,");

    VuGenerateOptions bad;
    bad.id = "not an identifier";
    CHECK_THROWS(generateVuFiles(capture, bad));
}

// ---- the BIOS's code (docs/game-log.md, "G8") ------------------------------------

namespace {

// An assembled program's code as one raw piece where it was assembled to
// run: no sections and no symbols, as the BIOS's files are read.
ElfFile asPieces(const char* source) {
    const ElfFile elf = assemble(source);
    const AddrRange text = elf.codeRanges().front();
    ElfFile::Piece p;
    p.name = "code";
    p.address = text.start;
    for (uint32_t a = text.start; a < text.end; a += 4) {
        uint32_t w = 0;
        elf.read32(a, w);
        for (int i = 0; i < 4; ++i) p.bytes.push_back(static_cast<uint8_t>(w >> (8 * i)));
    }
    return ElfFile::fromPieces("pieces", elf.entry, {p});
}

// Code with data among it, as the BIOS's files have it.
const char* const kCodeAndData = R"(
        .text
        .globl _start
_start: jal   sub              # 00100000
        nop                    # 00100004
        lui   $t0, 0x0010      # 00100008
        ori   $t0, $t0, 0x0030 # 0010000C  other
        jr    $t0              # 00100010
        nop                    # 00100014
        .word 0x00000000       # 00100018  data
        .word 0x2402FFFF       # 0010001C  data that reads as an instruction
sub:    jr    $ra              # 00100020
        addiu $v0, $zero, 1    # 00100024
        .word 0x12345678       # 00100028  data
        .word 0x00100020       # 0010002C  data: the address of sub
other:  j     0x00200000       # 00100030  out of the image
        nop                    # 00100034
        .word 0xFFFFFFFF       # 00100038  data
far:    addiu $v0, $zero, 2    # 0010003C  only a captured entry leads here
        jr    $ra              # 00100040
        nop                    # 00100044
)";

// A BIOS image of our own making: the directory and the files the EE runs,
// each holding "jr $ra; nop" and nothing else.
std::vector<uint8_t> makeBiosRom(bool withLoader = true) {
    struct File {
        const char* name;
        uint32_t size;
    };
    std::vector<File> files = {{"RESET", 0x40}, {"ROMDIR", 0x70}, {"RDRAM", 0x10}, {"KERNEL", 0x10}, {"EENULL", 0x10}};
    if (withLoader) files.push_back({"EELOAD", 0x10});
    std::vector<uint8_t> rom(0x100, 0);
    const auto put32 = [&](size_t at, uint32_t v) { std::memcpy(rom.data() + at, &v, 4); };
    uint32_t offset = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        const size_t entry = 0x40 + 16 * i;
        std::memcpy(rom.data() + entry, files[i].name, std::strlen(files[i].name));
        put32(entry + 12, files[i].size);
        if (std::strcmp(files[i].name, "ROMDIR") != 0) {
            put32(offset, 0x03E00008u);  // jr $ra; then nop
        }
        offset += (files[i].size + 15) & ~15u;
    }
    return rom;
}

} // namespace

TEST_CASE(pieces_are_code_where_they_are_placed) {
    const ElfFile image = ElfFile::fromPieces("two", 0x2000u, {{"b", 0x2000u, {8, 0, 0xE0, 3, 0, 0, 0, 0}}, {"a", 0x1000u, {0, 0, 0, 0}}});
    CHECK_EQ(image.codeRanges().size(), size_t{2});
    CHECK_EQ(image.codeRanges()[0].name, std::string("a"));   // in address order
    CHECK_EQ(image.codeRanges()[1].end, 0x2008u);
    uint32_t w = 0;
    CHECK(image.read32(0x2000u, w) && w == 0x03E00008u);
    CHECK(!image.isMapped(0x1004u));
    CHECK_THROWS(ElfFile::fromPieces("overlap", 0, {{"a", 0x1000u, std::vector<uint8_t>(8)}, {"b", 0x1004u, std::vector<uint8_t>(8)}}));
}

TEST_CASE(code_that_is_followed_leaves_the_data_out) {
    const ElfFile image = asPieces(kCodeAndData);
    Options o;
    o.followCode = true;
    const Program prog = analyze(image, o);

    // _start; sub, which it calls; other, where it jumps through a register it
    // has just made a constant.
    CHECK_EQ(prog.functions.size(), size_t{3});
    if (prog.functions.size() != 3) return;
    CHECK_EQ(prog.functions[0].start, 0x00100000u);
    CHECK_EQ(prog.functions[0].end, 0x00100018u);
    CHECK_EQ(prog.functions[1].start, 0x00100020u);
    CHECK_EQ(prog.functions[1].end, 0x00100028u);
    CHECK_EQ(prog.functions[1].source, Source::Jal);
    CHECK_EQ(prog.functions[2].start, 0x00100030u);
    CHECK_EQ(prog.functions[2].end, 0x00100038u);
    CHECK(prog.exits == std::vector<uint32_t>{0x00200000u});
    // The data is in no function, not even what reads as an instruction or as
    // the address of code.
    for (uint32_t a : {0x00100018u, 0x0010001Cu, 0x00100028u, 0x0010002Cu, 0x00100038u, 0x0010003Cu})
        CHECK(prog.functionContaining(a) == nullptr);
}

TEST_CASE(code_that_is_followed_takes_captured_entries) {
    const ElfFile image = asPieces(kCodeAndData);
    Options o;
    o.followCode = true;
    o.capture.entries = {0x0010003Cu, 0x00100024u};
    const Program prog = analyze(image, o);
    const Function* far = prog.functionAt(0x0010003Cu);
    CHECK(far != nullptr);
    if (far) {
        CHECK_EQ(far->end, 0x00100048u);
        CHECK_EQ(far->source, Source::Captured);
    }
    // Inside code that was reached, a captured entry is a leader.
    const Function* sub = prog.functionAt(0x00100020u);
    CHECK(sub && sub->isLeader(0x00100024u));
    CHECK(prog.functionContaining(0x00100038u) == nullptr);
}

TEST_CASE(a_bios_image_is_read_as_the_pieces_the_ee_runs) {
    const std::vector<uint8_t> rom = makeBiosRom();
    const Bios b = readBios(rom, "test.bin");
    uint32_t crc = 0;
    for (size_t i = 0; i < rom.size(); i += 4) crc ^= static_cast<uint32_t>(rom[i]) | static_cast<uint32_t>(rom[i + 1]) << 8 |
                                                     static_cast<uint32_t>(rom[i + 2]) << 16 | static_cast<uint32_t>(rom[i + 3]) << 24;
    CHECK_EQ(b.crc, crc);
    CHECK_EQ(b.images.size(), size_t{4});
    if (b.images.size() != 4) return;

    // RESET at 0, RDRAM at B0 in the ROM: through each mirror.
    CHECK_EQ(b.images[0].id, std::string("bios_rom0"));
    CHECK_EQ(b.images[0].code.codeRanges()[0].start, 0x9FC00000u);
    CHECK_EQ(b.images[0].code.codeRanges()[1].start, 0x9FC000B0u);
    CHECK_EQ(b.images[1].id, std::string("bios_rom1"));
    CHECK_EQ(b.images[1].code.codeRanges()[0].start, 0xBFC00000u);
    CHECK(b.images[1].entries == std::vector<uint32_t>{0xBFC00000u});
    CHECK_EQ(b.images[2].id, std::string("bios_kernel"));
    CHECK_EQ(b.images[2].code.codeRanges()[0].start, 0x80000000u);
    CHECK_EQ(b.images[2].code.codeRanges()[0].end, 0x80000010u);
    CHECK_EQ(b.images[2].entries.size(), size_t{5});
    CHECK_EQ(b.images[3].id, std::string("bios_loader"));
    CHECK_EQ(b.images[3].code.codeRanges()[0].start, 0x00081FC0u);
    CHECK_EQ(b.images[3].code.codeRanges()[1].start, 0x00082000u);
    CHECK(b.images[3].entries == (std::vector<uint32_t>{0x00081FC0u, 0x00082000u}));
    uint32_t w = 0;
    CHECK(b.images[2].code.read32(0x80000000u, w) && w == 0x03E00008u);

    CHECK_THROWS(readBios(makeBiosRom(false), "no loader.bin"));
    CHECK_THROWS(readBios(std::vector<uint8_t>(0x100, 0), "no directory.bin"));
}

TEST_CASE(a_bios_image_with_nothing_found_in_it_is_still_generated) {
    const Bios b = readBios(makeBiosRom(), "test.bin");
    Options o;
    o.followCode = true;   // bios_rom0 has no entry of its own
    const Program prog = analyze(b.images[0].code, o);
    CHECK(prog.functions.empty());
    GenerateOptions go;
    go.id = "bios_rom0";
    go.kind = 1;
    go.name = "test.bin";
    go.crc = 0x12345678u;
    const std::string code = generate(prog, go);
    CHECK_CONTAINS(code, "kImageBios,");
    CHECK_CONTAINS(code, "0x12345678u,");
    CHECK_CONTAINS(code, "{0, 0, nullptr, 0, 0, 0, 0},   // (none)");
    CHECK_CONTAINS(code, "derived from the BIOS image");

    // An executable without code is still a mistake.
    go.kind = 0;
    CHECK_THROWS(generate(prog, go));
}

TEST_CASE(code_run_outside_the_executable_makes_an_image_of_its_own) {
    const ElfFile elf = assemble(kCallsAndBranches);
    Capture capture;
    // Blocks the program ran in kernel memory, the second starting inside the
    // first; and one inside the executable, which is a variant's business.
    capture.code.push_back({0x80075000u, {0x24020055u, 0x03E00008u, 0x00000000u}});   // li $v0, 0x55; jr $ra; nop
    capture.code.push_back({0x80075008u, {0x00000000u, 0x24020001u}});
    capture.code.push_back({0x80076000u, {0x03E00008u, 0x00000000u}});
    capture.code.push_back({0x00100000u, {0x27BDFFF0u}});
    std::vector<Note> notes;
    std::vector<uint32_t> starts;
    const ElfFile pieces = codeOutside(elf, capture, notes, starts);
    CHECK(starts == (std::vector<uint32_t>{0x80075000u, 0x80075008u, 0x80076000u}));
    CHECK_EQ(pieces.codeRanges().size(), size_t{2});
    if (pieces.codeRanges().size() == 2) {
        CHECK_EQ(pieces.codeRanges()[0].start, 0x80075000u);
        CHECK_EQ(pieces.codeRanges()[0].end, 0x80075010u);
        CHECK_EQ(pieces.codeRanges()[1].start, 0x80076000u);
    }
    CHECK(notes.empty());

    Options o;
    o.followCode = true;
    o.capture.entries = starts;
    const Program prog = analyze(pieces, o);
    CHECK(prog.functionAt(0x80075000u) != nullptr);
    GenerateOptions go;
    go.id = "test_outside";
    go.kind = 2;
    go.crc = pcsx2Crc(elf);
    const std::string code = generate(prog, go);
    CHECK_CONTAINS(code, "kImageOutside,");
    CHECK_CONTAINS(code, strprintf("0x%08Xu,", pcsx2Crc(elf)));

    // Two different words captured at one address: the first is kept, and said.
    capture.code.push_back({0x80076004u, {0x24020002u}});
    notes.clear();
    starts.clear();
    codeOutside(elf, capture, notes, starts);
    CHECK_EQ(notes.size(), size_t{1});
}

int main(int argc, char** argv) { return ::testing::runAll(argc, argv); }
