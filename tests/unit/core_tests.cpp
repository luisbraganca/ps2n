// SPDX-License-Identifier: GPL-3.0-or-later
// Decoder, disassembler, assembler, ELF and IRX tests.
#include "testing.h"

#include "ps2n/assembler.h"
#include "ps2n/elf.h"
#include "ps2n/irx.h"
#include "ps2n/r5900.h"
#include "ps2n/romdir.h"
#include "ps2n/snr2.h"
#include "ps2n/util.h"
#include "ps2n/vu_program.h"

#include <cstring>
#include <random>
#include <utility>
#include <vector>

using namespace ps2n;
using namespace ps2n::r5900;

namespace {

uint32_t enc(const char* line, uint32_t addr = 0x00100000) {
    uint32_t w = 0;
    std::string err;
    if (!assembleInstruction(line, addr, w, &err)) {
        ::testing::fail(__FILE__, __LINE__, std::string("cannot assemble '") + line + "': " + err);
        return 0xFFFFFFFFu;
    }
    return w;
}

} // namespace

TEST_CASE(decoder_matches_every_table_row) {
    for (size_t i = 1; i < static_cast<size_t>(Op::Count); ++i) {
        const auto op = static_cast<Op>(i);
        if (decodeOp(info(op).match) != op)
            ::testing::fail(__FILE__, __LINE__, std::string("decode(match) != op for ") + info(op).mnemonic);
    }
}

TEST_CASE(decoder_rejects_encodings_the_ee_lacks) {
    CHECK(decodeOp(0x0000001Cu) == Op::Invalid);   // DMULT
    CHECK(decodeOp(0x0000001Eu) == Op::Invalid);   // DDIV
    CHECK(decodeOp(0xC0000000u) == Op::Invalid);   // LL
    CHECK(decodeOp(0xE0000000u) == Op::Invalid);   // SC
    CHECK(decodeOp(0xD4000000u) == Op::Invalid);   // LDC1
    CHECK(decodeOp(0x4C000000u) == Op::Invalid);   // COP3
    CHECK(decodeOp(0x70000002u) == Op::Invalid);   // MMI function 2
}

TEST_CASE(known_encodings) {
    CHECK_EQ(enc("addiu $sp, $sp, -32"), 0x27BDFFE0u);
    CHECK_EQ(enc("jr $ra"), 0x03E00008u);
    CHECK_EQ(enc("sq $zero, 0($v0)"), 0x7C400000u);
    CHECK_EQ(enc("syscall"), 0x0000000Cu);
    CHECK_EQ(enc("lui $at, 0x10"), 0x3C010010u);
    CHECK_EQ(enc("mult $v0, $a0, $a1"), 0x00851018u);
    CHECK_EQ(enc("mult $a0, $a1"), 0x00850018u);
    CHECK_EQ(enc("paddw $t3, $t1, $t2"), 0x712A5808u);
    CHECK_EQ(enc("vadd.xyzw $vf3, $vf1, $vf2"), 0x4BE208E8u);
    CHECK_EQ(enc("vaddx.xyzw $vf3, $vf1, $vf2x"), 0x4BE208C0u);
    CHECK_EQ(enc("vnop"), 0x4A0002FFu);
    CHECK_EQ(enc("nop"), 0u);
    CHECK_EQ(enc("move $a0, $s0"), 0x0200202Du);   // daddu
    CHECK_EQ(enc("jal 0x00100040", 0x00100000), 0x0C040010u);
    CHECK_EQ(enc("b 0x00100000", 0x00100008), 0x1000FFFDu);
    CHECK_EQ(enc("sync.p"), 0x0000040Fu);
    CHECK_EQ(enc("pmfhl.slw $a0"), 0x700020B0u);
    CHECK_EQ(enc("qmfc2.i $t0, $vf1"), 0x48280801u);
}

TEST_CASE(disassembler_aliases) {
    CHECK_EQ(disassemble(0x00000000u, 0), std::string("nop"));
    CHECK_EQ(disassemble(0x0200202Du, 0), std::string("move $a0, $s0"));
    CHECK_EQ(disassemble(0x24040005u, 0), std::string("li $a0, 5"));
    CHECK_EQ(disassemble(0x1000FFFDu, 0x00100008), std::string("b 0x00100000"));
    CHECK_EQ(disassemble(0x0320F809u, 0), std::string("jalr $t9"));
    CHECK_EQ(disassemble(0x4BE208E8u, 0), std::string("vadd.xyzw $vf3, $vf1, $vf2"));
    CHECK_EQ(disassemble(0x4C000000u, 0), std::string(".word 0x4c000000"));   // COP3 does not exist on the EE
}

TEST_CASE(random_round_trip) {
    std::mt19937 rng(20261026);
    int checked = 0;
    for (size_t i = 1; i < static_cast<size_t>(Op::Count); ++i) {
        const auto op = static_cast<Op>(i);
        const auto& oi = info(op);
        for (int k = 0; k < 100; ++k) {
            uint32_t raw = oi.match | (rng() & 0x03FFFFC0u);
            if (oi.flags & F_VUBC) raw = (raw & ~3u) | (rng() & 3);
            if (decodeOp(raw) != op) continue;
            if ((oi.flags & F_VUDEST) && ((raw >> 21) & 15) == 0) continue;   // degenerate empty mask
            const Insn in(raw, 0x00100000 + 4 * (rng() & 0xFFF));
            const std::string text = disassemble(in);
            uint32_t back = 0;
            std::string err;
            if (!assembleInstruction(text, in.addr, back, &err)) {
                ::testing::fail(__FILE__, __LINE__, "cannot re-assemble '" + text + "': " + err);
                continue;
            }
            if (disassemble(Insn(back, in.addr)) != text) ::testing::fail(__FILE__, __LINE__, "round trip changed '" + text + "'");
            ++checked;
        }
    }
    CHECK(checked > 10000);
}

TEST_CASE(field_accessors) {
    const Insn in(enc("viaddi $vi3, $vi4, -2"), 0);
    CHECK_EQ(in.op, Op::VIADDI);
    CHECK_EQ(in.ft(), 3u);
    CHECK_EQ(in.fs(), 4u);
    CHECK_EQ(in.imm5(), -2);
    const Insn b(enc("beq $a0, $a1, 0x00100100", 0x00100000), 0x00100000);
    CHECK_EQ(b.branchTarget(), 0x00100100u);
    CHECK(b.hasDelaySlot());
    CHECK(!b.endsBlock());
    CHECK(Insn(enc("jr $ra"), 0).isReturn());
    CHECK(Insn(enc("b 0x00100000", 0x00100000), 0x00100000).endsBlock());
}

TEST_CASE(assembler_program_and_elf) {
    Assembler as;
    as.assemble(R"(
        .text
        .globl _start
        .ent _start
_start: la    $a0, msg
        jal   func
        nop
1:      b     1b
        nop
        .end _start
        .ent func
func:   lui   $v0, %hi(value)
        lw    $v0, %lo(value)($v0)
        jr    $ra
        nop
        .end func
        .rodata
msg:    .asciz "hi"
        .data
value:  .word 0x12345678
        .bss
buf:    .space 64
)", "test.s");
    const auto bytes = as.buildElf();
    const ElfFile elf = ElfFile::parse(bytes, "test.elf");
    CHECK_EQ(elf.machine, elf::EM_MIPS);
    CHECK_EQ(elf.type, elf::ET_EXEC);
    CHECK_EQ(elf.entry, 0x00100000u);
    CHECK(elf.isCode(0x00100000));
    const auto func = elf.symbolValue("func");
    CHECK(func.has_value());
    bool sized = false;
    for (const auto& s : elf.symbols)
        if (s.name == "func") sized = s.isFunction() && s.size == 16;
    CHECK(sized);
    uint32_t w = 0;
    CHECK(elf.read32(*func, w));
    CHECK_EQ(Insn(w, *func).op, Op::LUI);
    const auto value = elf.symbolValue("value");
    CHECK(elf.read32(*value, w));
    CHECK_EQ(w, 0x12345678u);
    const auto buf = elf.symbolValue("buf");
    CHECK(elf.read32(*buf, w));   // .bss reads as zero
    CHECK_EQ(w, 0u);
}

TEST_CASE(assembler_incbin) {
    // A file next to the source: .incbin embeds its bytes as they are.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ps2n_incbin_test";
    std::filesystem::create_directories(dir);
    writeFileIfChanged(dir / "blob.bin", std::string("\x78\x56\x34\x12\x01\x02", 6));
    Assembler as;
    as.assemble("        .text\n_start: nop\n        .data\n        .align 4\nblob:   .incbin \"blob.bin\"\nafter:  .byte 7\n",
                (dir / "incbin.s").string());
    const ElfFile elf = ElfFile::parse(as.buildElf(), "incbin.elf");
    uint32_t w = 0;
    CHECK(elf.read32(*elf.symbolValue("blob"), w));
    CHECK_EQ(w, 0x12345678u);
    CHECK_EQ(*elf.symbolValue("after") - *elf.symbolValue("blob"), 6u);
    Assembler missing;
    CHECK_THROWS(missing.assemble("        .data\n        .incbin \"no_such_file.bin\"\n", (dir / "missing.s").string()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE(assembler_reports_errors_with_location) {
    Assembler as;
    try {
        as.assemble("nop\n  frobnicate $a0\n", "bad.s");
        ::testing::fail(__FILE__, __LINE__, "expected an error");
    } catch (const Error& e) {
        CHECK(std::string(e.what()).find("bad.s:2") != std::string::npos);
    }
    Assembler as2;
    CHECK_THROWS(as2.assemble("addiu $a0, $a0, 40000\n", "range.s"));
    Assembler as3;
    CHECK_THROWS(as3.assemble("vadd $vf1, $vf2, $vf3\n", "dest.s"));   // missing .xyzw
}

TEST_CASE(elf_rejects_non_mips_files) {
    std::vector<uint8_t> junk(64, 0);
    CHECK_THROWS(ElfFile::parse(junk, "junk"));
    Assembler as;
    as.assemble("nop\n");
    auto bytes = as.buildElf();
    bytes[18] = 3;   // e_machine = x86
    CHECK_THROWS(ElfFile::parse(bytes, "x86"));
}

namespace {

// A minimal IRX: one PT_LOAD at 0 with an import table, plus .iopmod.
std::vector<uint8_t> makeIrx() {
    std::vector<uint32_t> text = {
        0x41E00000u, 0, 0x0101,                         // import table: magic, next, version
        0x6D737973u, 0x00006D65u,                       // "sysmem\0\0"
        0x03E00008u, 0x24000004u,                       // jr $ra; addiu $0,$0,4  (AllocSysMemory)
        0x03E00008u, 0x24000005u,                       // FreeSysMemory
        0, 0,
    };
    std::vector<uint8_t> iopmod(28 + 7, 0);
    auto put32 = [](std::vector<uint8_t>& b, size_t off, uint32_t v) { std::memcpy(b.data() + off, &v, 4); };
    put32(iopmod, 0, 0xFFFFFFFFu);
    put32(iopmod, 4, 0x20);          // entry
    put32(iopmod, 8, 0x8000);        // gp
    put32(iopmod, 12, static_cast<uint32_t>(text.size() * 4));
    iopmod[24] = 0x03;               // version 1.3
    iopmod[25] = 0x01;
    std::memcpy(iopmod.data() + 26, "testmod", 7);

    const std::string shstr = std::string("\0.text\0.iopmod\0.shstrtab\0", 25);
    const size_t textOff = 0x100, iopOff = textOff + text.size() * 4, strOff = iopOff + iopmod.size() + 4;
    const size_t shoff = (strOff + shstr.size() + 15) & ~size_t(15);
    std::vector<uint8_t> b(shoff + 40 * 4, 0);
    const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 1, 1, 1};
    std::memcpy(b.data(), ident, 16);
    auto put16 = [&](size_t off, uint16_t v) { std::memcpy(b.data() + off, &v, 2); };
    put16(16, elf::ET_SCE_IOPRELEXEC);
    put16(18, elf::EM_MIPS);
    put32(b, 20, 1);
    put32(b, 28, 52);
    put32(b, 32, static_cast<uint32_t>(shoff));
    put16(40, 52);
    put16(42, 32);
    put16(44, 1);
    put16(46, 40);
    put16(48, 4);
    put16(50, 3);
    // PT_LOAD at vaddr 0
    put32(b, 52, elf::PT_LOAD);
    put32(b, 56, static_cast<uint32_t>(textOff));
    put32(b, 64, 0);
    put32(b, 68, static_cast<uint32_t>(text.size() * 4));
    put32(b, 72, static_cast<uint32_t>(text.size() * 4));
    put32(b, 76, elf::PF_R | elf::PF_X);
    std::memcpy(b.data() + textOff, text.data(), text.size() * 4);
    std::memcpy(b.data() + iopOff, iopmod.data(), iopmod.size());
    std::memcpy(b.data() + strOff, shstr.data(), shstr.size());
    auto sh = [&](int i, uint32_t name, uint32_t type, uint32_t flags, uint32_t off, uint32_t size) {
        const size_t h = shoff + 40 * static_cast<size_t>(i);
        put32(b, h, name);
        put32(b, h + 4, type);
        put32(b, h + 8, flags);
        put32(b, h + 12, 0);
        put32(b, h + 16, off);
        put32(b, h + 20, size);
    };
    sh(1, 1, elf::SHT_PROGBITS, elf::SHF_ALLOC | elf::SHF_EXECINSTR, static_cast<uint32_t>(textOff), static_cast<uint32_t>(text.size() * 4));
    sh(2, 7, elf::SHT_SCE_IOPMOD, 0, static_cast<uint32_t>(iopOff), static_cast<uint32_t>(iopmod.size()));
    sh(3, 15, elf::SHT_STRTAB, 0, static_cast<uint32_t>(strOff), static_cast<uint32_t>(shstr.size()));
    return b;
}

} // namespace

TEST_CASE(irx_header_and_imports) {
    const ElfFile elf = ElfFile::parse(makeIrx(), "test.irx");
    CHECK(elf.isIrx());
    const IrxInfo info = parseIrx(elf);
    CHECK_EQ(info.name, std::string("testmod"));
    CHECK_EQ(info.version, 0x0103);
    CHECK_EQ(info.entry, 0x20u);
    CHECK_EQ(info.imports.size(), size_t(2));
    if (info.imports.size() == 2) {
        CHECK_EQ(info.imports[0].library, std::string("sysmem"));
        CHECK_EQ(info.imports[0].name, std::string("AllocSysMemory"));
        CHECK_EQ(info.imports[1].name, std::string("FreeSysMemory"));
    }
}

TEST_CASE(util_parse_int) {
    CHECK_EQ(parseInt("0x10").value(), 16);
    CHECK_EQ(parseInt("-12").value(), -12);
    CHECK_EQ(parseInt(" 42 ").value(), 42);
    CHECK(!parseInt("0xZZ").has_value());
    CHECK(!parseInt("").has_value());
    CHECK_EQ(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9), 0xCBF43926u);
}

TEST_CASE(snr2_parses_symbols_and_relocation_streams) {
    std::vector<uint8_t> b(0x290, 0);
    auto put = [&](uint32_t at, uint32_t value) { std::memcpy(b.data() + at, &value, 4); };
    auto text = [&](uint32_t at, const char* value) { std::memcpy(b.data() + at, value, std::strlen(value) + 1); };
    put(0x00, 0x32524E53u);
    put(0x04, 0x210);   // three external links
    put(0x08, 3);
    put(0x0C, 0x238);
    put(0x10, 3);
    put(0x14, 0x201);   // the name starts at an unaligned metadata boundary
    put(0x18, 0x100);
    put(0x1C, 0x120);
    put(0x2C, static_cast<uint32_t>(b.size()));
    put(0x34, 0x270);
    text(0x201, "M");
    text(0x204, "local");
    text(0x20A, "import");
    text(0x1D0, "callback");
    put(0x238 + 0, 0x204); put(0x238 + 4, 0x100); put(0x238 + 8, 0x00020000u);
    put(0x238 + 12, 0x20A); put(0x238 + 16, 0); put(0x238 + 20, 0);
    put(0x238 + 24, 0x1D0); put(0x238 + 28, 0x120); put(0x238 + 32, 0x00020000u);
    put(0x100, 0x3C020002u);   // lui  $v0, 2
    put(0x104, 0x24428034u);   // addiu $v0, $v0, -32716
    put(0x108, 0x0C000048u);   // jal  0x120
    put(0x10C, 0);
    put(0x120, 0x03E00008u);   // jr $ra
    put(0x124, 0x24020007u);
    put(0x130, 0x3C040000u);   // imported high/low
    put(0x134, 0x24840000u);
    put(0x138, 0x0C000000u);   // imported jal
    put(0x180, 0x120);         // an internal word link
    put(0x270, 1); put(0x271, 0x100);   // high, followed by a jump, not its low half
    b[0x275] = 0x0A;                    // +8: jump at 0x108
    b[0x276] = 0; put(0x277, 0x104);    // absolute low at 0x104
    b[0x27B] = 0x7F; put(0x27C, 0);     // +0x7c: word at 0x180, then stop
    put(0x210, 0x130); put(0x214, (1u << 8) | elf::R_MIPS_HI16); put(0x218, 0x20);
    put(0x21C, 0x134); put(0x220, (1u << 8) | elf::R_MIPS_LO16); put(0x224, 0x20);
    put(0x228, 0x138); put(0x22C, (2u << 8) | elf::R_MIPS_26); put(0x238 - 4, 0);

    const Snr2Module module = Snr2Module::parse(b, "TEST.REL");
    CHECK_EQ(module.name, std::string("TEST.REL"));
    CHECK_EQ(module.codeStart, 0x100u);
    CHECK_EQ(module.codeEnd, 0x200u);
    CHECK_EQ(module.entryPoints.size(), size_t(2));
    CHECK_EQ(module.symbols.size(), size_t(3));
    CHECK_EQ(module.symbols[2].name, std::string("callback"));
    CHECK_EQ(module.links.size(), size_t(7));
    const ElfFile image = module.image();
    uint32_t w = 0;
    CHECK(image.read32(0x100, w)); CHECK_EQ(w, 0x3C020002u);   // carry from the signed low half
    CHECK(image.read32(0x104, w)); CHECK_EQ(w, 0x24428034u);
    CHECK(image.read32(0x108, w)); CHECK_EQ(w, 0x0C000048u);
    CHECK(image.read32(0x180, w)); CHECK_EQ(w, 0x120u);
    CHECK(image.read32(0x138, w)); CHECK_EQ(w, 0x0C000000u);   // imports stay for the game's loader
    CHECK_EQ(module.links[0].offset, 0x100u);
    CHECK_EQ(module.links[0].value, 0x18034u);

    std::vector<uint8_t> bad = b;
    put(0x08, 1000);
    CHECK_THROWS(Snr2Module::parse(b, "bad-table.rel"));
    put(0x08, 3); put(0x22C, 0xFF00u | elf::R_MIPS_26);
    CHECK_THROWS(Snr2Module::parse(b, "bad-symbol.rel"));
    put(0x22C, (2u << 8) | elf::R_MIPS_26); put(0x277, 0);
    CHECK_THROWS(Snr2Module::parse(b, "bad-high.rel"));

    put(0x34, 0);   // no internal stream; the three external links remain
    const Snr2Module withoutStream = Snr2Module::parse(b, "no-stream.rel");
    CHECK_EQ(withoutStream.links.size(), size_t(3));
}

// A VU program is told by its code wherever it sits: the pairs it reaches
// (branches, a call and the pair it returns to, up to its E bit's delay slot)
// and where they sit from its start; its first pairs find it.
// What a hand-over leaves in flight, as the runtime records it and the
// recompile reads it back: relative to the cycle, read back at another.
TEST_CASE(vu_pipeline_text_reads_back) {
    ps2n::vu::Timing t;
    t.cycle = 500;
    t.head = 6;   // the ring's oldest entry anywhere
    t.count = 3;
    t.fmac[6] = {502, true, false};
    t.fmac[7] = {503, false, true};
    t.fmac[0] = {504, true, true};
    t.lands[7][1] = 504;
    t.lands[31][3] = 503;
    t.lands[2][0] = 499;   // landed already
    t.fdiv = {true, 507};
    t.efu = {true, 530};
    t.ialu = {502, 0x30};
    t.viBackupCycles = 1;
    const std::string text = ps2n::vu::pipelineText(t);
    CHECK_EQ(text, std::string("f2c f3s f4cs l7.1:4 l31.3:3 q7 p30 i30:2 b1"));
    ps2n::vu::Timing back;
    CHECK(ps2n::vu::parsePipeline(text, 1000, back));
    CHECK_EQ(back.count, 3u);
    CHECK_EQ(back.fmac[2].done, 1004u);
    CHECK(back.fmac[2].clip && back.fmac[2].fsset);
    CHECK_EQ(back.lands[7][1], 1004u);
    CHECK_EQ(back.landsMax[31], 1003u);
    CHECK_EQ(back.ialu.regs, 0x30u);
    CHECK_EQ(ps2n::vu::pipelineText(back), text);
    ps2n::vu::Timing empty;
    CHECK(ps2n::vu::parsePipeline("", 7, empty));
    CHECK_EQ(empty.count, 0u);
    for (const char* bad : {"x1", "f", "f1 f2 f3 f4 f5", "l32.0:1", "l1.4:1", "l1.1", "q", "i:3", "f1c2"})
        CHECK(!ps2n::vu::parsePipeline(bad, 0, empty));
}

TEST_CASE(vu_programs_are_told_by_their_code_wherever_they_sit) {
    constexpr uint32_t upperNop = 0x000002FFu, lowerNop = 0x8000033Cu, ebit = 0x40000000u;
    auto pair = [](uint32_t lower, uint32_t upper) { return static_cast<uint64_t>(lower) | static_cast<uint64_t>(upper) << 32; };
    const std::vector<uint64_t> program = {
        pair((0x08u << 25) | (1u << 16) | 3u, upperNop),    // 0: iaddiu vi01, vi00, 3
        pair((0x21u << 25) | (15u << 16) | 3u, upperNop),   // 1: bal vi15, to 5 (the delay slot plus 3 pairs)
        pair(lowerNop, upperNop),                           // 2: its delay slot
        pair(lowerNop, upperNop | ebit),                    // 3: back from the call: the end
        pair(lowerNop, upperNop),                           // 4: the E bit's delay slot
        pair((0x08u << 25) | (2u << 16) | 5u, upperNop),    // 5: iaddiu vi02, vi00, 5
        pair((0x24u << 25) | (15u << 11), upperNop),        // 6: jr vi15
        pair(lowerNop, upperNop),                           // 7: its delay slot
    };
    std::mt19937_64 random(7);
    auto memoryWith = [&](uint32_t at, const std::vector<uint64_t>& code) {
        std::vector<uint64_t> micro(2048);
        for (uint64_t& w : micro) w = random() & ~(uint64_t(ebit) << 32);   // other code around it
        for (size_t i = 0; i < code.size(); ++i) micro[(at / 8 + i) % 2048] = code[i];
        return micro;
    };
    auto keys = [](const std::vector<uint64_t>& micro, uint32_t entry) {
        const auto words = [&micro](uint32_t pc) { return micro[(pc & 0x3FFF) / 8]; };
        return std::make_pair(vu::entryKey(words, entry, 0x3FFF, 1), vu::programKey(words, entry, 0x3FFF, 1));
    };
    const auto here = keys(memoryWith(0x0100, program), 0x0100);
    CHECK(keys(memoryWith(0x2340, program), 0x2340) == here);
    CHECK(keys(memoryWith(0x3FE0, program), 0x3FE0) == here);   // round the end of micro memory
    std::vector<uint64_t> changed = program;
    changed[5] = pair((0x08u << 25) | (2u << 16) | 6u, upperNop);   // the routine: another program
    CHECK(keys(memoryWith(0x0100, changed), 0x0100).second != here.second);
    CHECK_EQ(keys(memoryWith(0x0100, changed), 0x0100).first, here.first);   // its first pairs are the same
    changed = program;
    changed[3] = pair(lowerNop, upperNop);   // goes on past where it ended
    CHECK(keys(memoryWith(0x0100, changed), 0x0100).second != here.second);
    changed = program;
    std::swap(changed[5], changed[7]);   // the same pairs, in another order
    CHECK(keys(memoryWith(0x0100, changed), 0x0100).second != here.second);
    changed = program;
    changed[0] = pair((0x08u << 25) | (1u << 16) | 4u, upperNop);
    CHECK(keys(memoryWith(0x0100, changed), 0x0100).first != here.first);
    // The same pairs in the same order, where the first branch lands on
    // another of them: another program.
    const uint64_t b2 = pair((0x20u << 25) | 2u, upperNop);                            // b, to the delay slot plus 2 pairs
    const uint64_t back3 = pair((0x29u << 25) | (1u << 11) | 0x7FDu, upperNop);        // ibne vi00, vi01, back 3 pairs from the slot
    const uint64_t x = pair((0x08u << 25) | (3u << 16) | 1u, upperNop);                // iaddiu vi03, vi00, 1
    const uint64_t y = pair((0x08u << 25) | (4u << 16) | 1u, upperNop);                // iaddiu vi04, vi00, 1
    const uint64_t nop = pair(lowerNop, upperNop), end = pair(lowerNop, upperNop | ebit);
    const std::vector<uint64_t> loopFirst = {b2, nop, x, y, back3, nop, end, nop};       // to y, then x y while vi01
    const std::vector<uint64_t> gapFirst = {b2, nop, nop, x, y, back3, nop, end, nop};   // to x, then x y while vi01; pair 2 unreached
    CHECK(keys(memoryWith(0x0100, loopFirst), 0x0100).second != keys(memoryWith(0x0100, gapFirst), 0x0100).second);
    // Pairs it does not reach are not part of it.
    changed = program;
    changed.push_back(pair(lowerNop, upperNop | ebit));   // 8: after the routine's return
    CHECK(keys(memoryWith(0x0100, changed), 0x0100) == here);
}

namespace {

// A BIOS image of our own making, as small as one can be: RESET (with the
// word "RESET" among its own bytes, which is not the directory), the
// directory, a space between files, and one more file.
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(0xC0, 0xEE);
    const auto entry = [&](size_t at, const char* name, uint32_t size) {
        std::memset(rom.data() + at, 0, 16);
        std::memcpy(rom.data() + at, name, std::strlen(name));
        std::memcpy(rom.data() + at + 12, &size, 4);
    };
    std::memcpy(rom.data() + 0x10, "RESET\0\0\0\0\0", 10);  // a decoy inside RESET
    entry(0x30, "RESET", 0x30);
    entry(0x40, "ROMDIR", 0x50);
    entry(0x50, "-", 0x08);
    entry(0x60, "KERNEL", 0x14);
    std::memset(rom.data() + 0x70, 0, 16);  // the end of the directory
    return rom;
}

} // namespace

TEST_CASE(rom_directory_lists_files_where_they_lie) {
    const std::vector<uint8_t> rom = makeRom();
    const std::vector<bios::RomFile> files = bios::readRomDirectory(rom.data(), rom.size());
    CHECK_EQ(files.size(), size_t{4});
    if (files.size() != 4) return;
    CHECK_EQ(files[0].name, std::string("RESET"));
    CHECK_EQ(files[0].offset, 0u);
    CHECK_EQ(files[1].name, std::string("ROMDIR"));
    CHECK_EQ(files[1].offset, 0x30u);
    CHECK_EQ(files[2].name, std::string("-"));
    CHECK_EQ(files[2].offset, 0x80u);
    CHECK_EQ(files[3].offset, 0x90u);  // after "-", rounded up to 16 bytes
    CHECK_EQ(files[3].size, 0x14u);

    const auto kernel = bios::findRomFile(files, "KERNEL");
    CHECK(kernel && kernel->offset == 0x90u);
    CHECK(!bios::findRomFile(files, "EELOAD"));
}

TEST_CASE(rom_directory_absent_or_cut_short_is_none) {
    const std::vector<uint8_t> nothing(0x100, 0);
    CHECK(bios::readRomDirectory(nothing.data(), nothing.size()).empty());

    // KERNEL would end past the end of the image.
    const std::vector<uint8_t> rom = makeRom();
    CHECK(bios::readRomDirectory(rom.data(), 0xA0).empty());
    CHECK(bios::readRomDirectory(nullptr, 0).empty());
}

int main(int argc, char** argv) { return ::testing::runAll(argc, argv); }
