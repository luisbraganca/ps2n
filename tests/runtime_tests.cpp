// Instruction semantics (ops_*.h), guest printf formatting and GS addressing.
#include "testing.h"

#include "audio/audio.h"
#include "clock.h"
#include "hw/dmac.h"
#include "hw/gs.h"
#include "hw/hw.h"
#include "hw/ipu.h"
#include "hw/mpeg.h"
#include "input/input.h"
#include "internal.h"
#include "iop/cpu.h"
#include "iop/disc.h"
#include "iop/hle.h"
#include "iop/hostfs.h"
#include "iop/iop.h"
#include "iop/kernel.h"
#include "iop/pad.h"
#include "iop/spu2.h"
#include "log.h"
#include "ps2/recomp.h"
#include "settings.h"
#include "stubs/format.h"

#include "ps2n/elf.h"
#include "ps2n/util.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <set>
#include <vector>

using namespace ps2;
using namespace ps2::ops;
using namespace ps2::regs;

namespace {

void reset() {
    cpu = Cpu{};
    cpu.vf[0].c[3] = 0x3F800000u;
}
void setGpr(uint32_t r, uint64_t lo, uint64_t hi = 0) {
    cpu.gpr[r].d[0] = lo;
    cpu.gpr[r].d[1] = hi;
}
uint32_t word(uint32_t r, int i) { return static_cast<uint32_t>(cpu.gpr[r].d[i / 2] >> (32 * (i % 2))); }
uint16_t half(uint32_t r, int i) { return static_cast<uint16_t>(cpu.gpr[r].d[i / 4] >> (16 * (i % 4))); }
uint32_t fbits(float f) { return std::bit_cast<uint32_t>(f); }
float vf(uint32_t r, int c) { return std::bit_cast<float>(cpu.vf[r].c[c]); }
void setVf(uint32_t r, float x, float y, float z, float w) {
    cpu.vf[r].c[0] = fbits(x);
    cpu.vf[r].c[1] = fbits(y);
    cpu.vf[r].c[2] = fbits(z);
    cpu.vf[r].c[3] = fbits(w);
}

} // namespace

TEST_CASE(int_sign_extension_and_upper_halves) {
    reset();
    setGpr(A0, 0x7FFFFFFF, 0xAAAA);
    ADDIU(A0, A0, 1);   // 32-bit result sign-extends; bits 64-127 untouched
    CHECK_EQ(cpu.gpr[A0].d[0], 0xFFFFFFFF80000000ull);
    CHECK_EQ(cpu.gpr[A0].d[1], 0xAAAAull);
    setGpr(A1, 0xFFFFFFFFFFFFFFFFull);
    setGpr(A2, 1);
    SLT(V0, A1, A2);    // 64-bit signed: -1 < 1
    CHECK_EQ(ru64(V0), 1ull);
    SLTU(V0, A1, A2);   // 64-bit unsigned
    CHECK_EQ(ru64(V0), 0ull);
    ADDIU(ZERO, A2, 5); // writes to $zero vanish
    CHECK_EQ(ru64(ZERO), 0ull);
    setGpr(T0, 0x8000000000000000ull);
    DSRA32(T1, T0, 31);
    CHECK_EQ(ru64(T1), 0xFFFFFFFFFFFFFFFFull);
    LUI(T2, 0x8000);
    CHECK_EQ(ru64(T2), 0xFFFFFFFF80000000ull);
}

TEST_CASE(int_overflow_traps_leave_rd) {
    reset();
    setGpr(A0, 0x7FFFFFFF);
    setGpr(A1, 1);
    setGpr(V0, 1234);
    ADD(V0, A0, A1);    // overflow: rd unchanged
    CHECK_EQ(ru64(V0), 1234ull);
    ADDU(V0, A0, A1);
    CHECK_EQ(rs64(V0), static_cast<int64_t>(INT32_MIN));
}

TEST_CASE(mult_div_r5900) {
    reset();
    setGpr(A0, static_cast<uint64_t>(-3));
    setGpr(A1, 7);
    MULT(V0, A0, A1);   // three-operand: rd = LO
    CHECK_EQ(rs64(V0), -21);
    CHECK_EQ(static_cast<int64_t>(cpu.hi.d[0]), -1);
    setGpr(A0, 0xFFFFFFFF);
    setGpr(A1, 2);
    MULTU(ZERO, A0, A1);
    CHECK_EQ(cpu.lo.d[0], 0xFFFFFFFFFFFFFFFEull);   // low word 0xFFFFFFFE, sign-extended
    CHECK_EQ(cpu.hi.d[0], 1ull);
    MULT1(V1, A1, A1);  // pipeline 1 uses the upper halves of HI/LO
    CHECK_EQ(cpu.lo.d[1], 4ull);
    CHECK_EQ(cpu.lo.d[0], 0xFFFFFFFFFFFFFFFEull);
    // Division by zero and overflow follow the R5900, not MIPS "undefined".
    setGpr(A0, static_cast<uint64_t>(-5));
    DIV(A0, ZERO);
    CHECK_EQ(static_cast<int64_t>(cpu.lo.d[0]), 1);
    CHECK_EQ(static_cast<int64_t>(cpu.hi.d[0]), -5);
    setGpr(A0, 5);
    DIV(A0, ZERO);
    CHECK_EQ(static_cast<int64_t>(cpu.lo.d[0]), -1);
    setGpr(A0, 0x80000000u);
    setGpr(A1, static_cast<uint64_t>(-1));
    DIV(A0, A1);
    CHECK_EQ(static_cast<int64_t>(cpu.lo.d[0]), static_cast<int64_t>(INT32_MIN));
    CHECK_EQ(cpu.hi.d[0], 0ull);
    setGpr(A0, 7);
    DIVU(A0, ZERO);
    CHECK_EQ(cpu.lo.d[0], ~0ull);
    CHECK_EQ(cpu.hi.d[0], 7ull);
    // MADD accumulates into {HI, LO}.
    cpu.lo.d[0] = 10;
    cpu.hi.d[0] = 0;
    setGpr(A0, 3);
    setGpr(A1, 4);
    MADD(V0, A0, A1);
    CHECK_EQ(ru64(V0), 22ull);
}

TEST_CASE(unaligned_loads_and_stores_all_alignments) {
    reset();
    const uint32_t base = 0x00200000;
    for (uint32_t i = 0; i < 32; ++i) ram[base + i] = static_cast<uint8_t>(0x10 + i);
    setGpr(T0, base);
    for (int a = 0; a < 4; ++a) {   // lwr/lwl pair == unaligned 32-bit load
        LWR(V0, T0, a);
        LWL(V0, T0, a + 3);
        uint32_t expect;
        std::memcpy(&expect, ram + base + a, 4);
        CHECK_EQ(ru32(V0), expect);
    }
    for (int a = 0; a < 8; ++a) {   // ldr/ldl pair == unaligned 64-bit load
        LDR(V0, T0, a);
        LDL(V0, T0, a + 7);
        uint64_t expect;
        std::memcpy(&expect, ram + base + a, 8);
        CHECK_EQ(ru64(V0), expect);
    }
    for (int a = 0; a < 4; ++a) {   // swr/swl pair == unaligned 32-bit store
        uint8_t before[12];
        std::memcpy(before, ram + base + 16, 12);
        setGpr(V1, 0xA1B2C3D4u);
        setGpr(T1, base + 16);
        SWR(V1, T1, a);
        SWL(V1, T1, a + 3);
        uint8_t expect[12];
        std::memcpy(expect, before, 12);
        const uint32_t v = 0xA1B2C3D4u;
        std::memcpy(expect + a, &v, 4);
        CHECK(std::memcmp(ram + base + 16, expect, 12) == 0);
    }
    for (int a = 0; a < 8; ++a) {   // sdr/sdl pair == unaligned 64-bit store
        uint8_t before[16];
        std::memcpy(before, ram + base, 16);
        setGpr(V1, 0x1122334455667788ull);
        SDR(V1, T0, a);
        SDL(V1, T0, a + 7);
        uint8_t expect[16];
        std::memcpy(expect, before, 16);
        const uint64_t v = 0x1122334455667788ull;
        std::memcpy(expect + a, &v, 8);
        CHECK(std::memcmp(ram + base, expect, 16) == 0);
    }
}

TEST_CASE(lq_sq_ignore_low_bits) {
    reset();
    const uint32_t base = 0x00200100;
    setGpr(A0, 0x0102030405060708ull, 0x1112131415161718ull);
    setGpr(T0, base);
    SQ(A0, T0, 7);   // address & ~15
    LQ(A1, T0, 15);
    CHECK_EQ(cpu.gpr[A1].d[0], 0x0102030405060708ull);
    CHECK_EQ(cpu.gpr[A1].d[1], 0x1112131415161718ull);
}

TEST_CASE(mmi_saturation_and_shuffles) {
    reset();
    setGpr(A0, 0x7FFFFFF0'00000001ull, 0x80000000'FFFFFFFFull);
    setGpr(A1, 0x00000100'00000002ull, 0x80000000'00000001ull);
    PADDSW(V0, A0, A1);
    CHECK_EQ(word(V0, 0), 3u);
    CHECK_EQ(word(V0, 1), 0x7FFFFFFFu);   // saturated
    CHECK_EQ(word(V0, 2), 0u);            // -1 + 1
    CHECK_EQ(word(V0, 3), 0x80000000u);   // saturated negative
    setGpr(A0, 0x0005'0005'0005'0005ull, 0);
    setGpr(A1, 0x0003'0009'0003'0009ull, 0);
    PSUBUH(V0, A0, A1);                   // unsigned saturation at 0
    CHECK_EQ(half(V0, 0), 0);
    CHECK_EQ(half(V0, 1), 2);
    PCGTH(V0, A0, A1);
    CHECK_EQ(half(V0, 0), 0);
    CHECK_EQ(half(V0, 1), 0xFFFF);
    setGpr(A0, 0x00000002'00000001ull, 0x00000004'00000003ull);   // rs words 1 2 3 4
    setGpr(A1, 0x00000020'00000010ull, 0x00000040'00000030ull);   // rt words 16 32 48 64
    PEXTLW(V0, A0, A1);
    CHECK_EQ(word(V0, 0), 0x10u);
    CHECK_EQ(word(V0, 1), 1u);
    CHECK_EQ(word(V0, 2), 0x20u);
    CHECK_EQ(word(V0, 3), 2u);
    PPACW(V0, A0, A1);
    CHECK_EQ(word(V0, 0), 0x10u);
    CHECK_EQ(word(V0, 1), 0x30u);
    CHECK_EQ(word(V0, 2), 1u);
    CHECK_EQ(word(V0, 3), 3u);
    PCPYUD(V0, A0, A1);
    CHECK_EQ(cpu.gpr[V0].d[0], cpu.gpr[A0].d[1]);
    CHECK_EQ(cpu.gpr[V0].d[1], cpu.gpr[A1].d[1]);
    PROT3W(V0, A0);
    CHECK_EQ(word(V0, 0), 2u);
    CHECK_EQ(word(V0, 1), 3u);
    CHECK_EQ(word(V0, 2), 1u);
    CHECK_EQ(word(V0, 3), 4u);
    PABSW(V0, A0);
    setGpr(A2, 0x80000000u);
    PABSW(V0, A2);
    CHECK_EQ(word(V0, 0), 0x7FFFFFFFu);
}

TEST_CASE(mmi_halfword_multiply_layout) {
    reset();
    setGpr(A0, 0x0004'0003'0002'0001ull, 0x0008'0007'0006'0005ull);
    setGpr(A1, 0x000A'000A'000A'000Aull, 0x000A'000A'000A'000Aull);
    PMULTH(V0, A0, A1);
    // LO = {10, 20, 50, 60}, HI = {30, 40, 70, 80}; rd = LO0 HI0 LO2 HI2
    CHECK_EQ(static_cast<uint32_t>(cpu.lo.d[0]), 10u);
    CHECK_EQ(static_cast<uint32_t>(cpu.lo.d[0] >> 32), 20u);
    CHECK_EQ(static_cast<uint32_t>(cpu.hi.d[0]), 30u);
    CHECK_EQ(static_cast<uint32_t>(cpu.lo.d[1]), 50u);
    CHECK_EQ(word(V0, 0), 10u);
    CHECK_EQ(word(V0, 1), 30u);
    CHECK_EQ(word(V0, 2), 50u);
    CHECK_EQ(word(V0, 3), 70u);
    PHMADH(V1, A0, A1);   // pairs: 2*10+1*10, 4*10+3*10, ...
    CHECK_EQ(word(V1, 0), 30u);
    CHECK_EQ(word(V1, 1), 70u);
    CHECK_EQ(word(V1, 2), 110u);
    CHECK_EQ(word(V1, 3), 150u);
    PMFHL_LW(T0);
    CHECK_EQ(word(T0, 0), 30u);
    CHECK_EQ(word(T0, 1), 70u);
}

TEST_CASE(mmi_qfsrv_funnel_shift) {
    reset();
    setGpr(A0, 0x0F0E0D0C0B0A0908ull, 0x1716151413121110ull);   // rs (high part)
    setGpr(A1, 0x0706050403020100ull, 0x0F0E0D0C0B0A0908ull);   // rt (low part)
    setGpr(T0, 3);
    MTSAB(T0, 0);   // shift right by 3 bytes
    QFSRV(V0, A0, A1);
    CHECK_EQ(cpu.gpr[V0].d[0], 0x0A09080706050403ull);
    CHECK_EQ(cpu.gpr[V0].d[1], 0x0A09080F0E0D0C0Bull);
}

TEST_CASE(fpu_non_ieee_behaviour) {
    reset();
    cpu.fpr[1] = fbits(100.0f);
    cpu.fpr[2] = 0;
    DIV_S(3, 1, 2);   // x / 0 = +FLT_MAX, D flag
    CHECK_EQ(cpu.fpr[3], 0x7F7FFFFFu);
    CHECK(cpu.fcr31 & fpu::kD);
    cpu.fpr[1] = fbits(-100.0f);
    DIV_S(3, 1, 2);
    CHECK_EQ(cpu.fpr[3], 0xFF7FFFFFu);
    cpu.fpr[4] = 0x7F800000u;   // "infinity" reads as FLT_MAX
    cpu.fpr[5] = fbits(2.0f);
    MUL_S(6, 4, 5);             // overflow clamps
    CHECK_EQ(cpu.fpr[6], 0x7F7FFFFFu);
    CHECK(cpu.fcr31 & fpu::kO);
    cpu.fpr[7] = 0x00000001u;   // denormal reads as 0
    ADD_S(8, 7, 7);
    CHECK_EQ(cpu.fpr[8], 0u);
    cpu.fpr[9] = fbits(3.0e9f);
    CVT_W_S(10, 9);             // saturates
    CHECK_EQ(cpu.fpr[10], 0x7FFFFFFFu);
    cpu.fpr[9] = fbits(-2.75f);
    CVT_W_S(10, 9);             // truncates
    CHECK_EQ(static_cast<int32_t>(cpu.fpr[10]), -2);
    cpu.fpr[11] = fbits(-4.0f);
    SQRT_S(12, 11);             // sqrt(|x|), I flag
    CHECK_EQ(std::bit_cast<float>(cpu.fpr[12]), 2.0f);
    CHECK(cpu.fcr31 & fpu::kI);
    cpu.fpr[13] = fbits(1.0f);
    cpu.fpr[14] = fbits(2.0f);
    C_LT_S(13, 14);
    CHECK(fpuCond());
    C_EQ_S(13, 14);
    CHECK(!fpuCond());
    MULA_S(13, 14);             // ACC = 2
    MADD_S(15, 14, 14);         // ACC + 4
    CHECK_EQ(std::bit_cast<float>(cpu.fpr[15]), 6.0f);
}

TEST_CASE(vu0_arithmetic_and_flags) {
    reset();
    setVf(1, 1, 2, 3, 4);
    setVf(2, 10, -2, 30, 40);
    VADD(0xE, 3, 1, 2);         // xyz only
    CHECK_EQ(vf(3, 0), 11.0f);
    CHECK_EQ(vf(3, 1), 0.0f);
    CHECK_EQ(vf(3, 2), 33.0f);
    CHECK_EQ(cpu.vf[3].c[3], 0u);                        // w not written
    CHECK(cpu.vi[VU_MAC] & (1u << 2));                   // y zero flag
    CHECK(cpu.vi[VU_STATUS] & 1u);                       // Z status
    VSUBbc(0xF, 4, 1, 2, 1);    // minus ft.y (-2)
    CHECK_EQ(vf(4, 0), 3.0f);
    CHECK_EQ(vf(4, 3), 6.0f);
    VMULq(0xF, 5, 1);           // Q is 0
    CHECK_EQ(vf(5, 2), 0.0f);
    VDIV(2, 0, 1, 1);           // Q = 10 / 2
    CHECK_EQ(std::bit_cast<float>(cpu.vi[VU_Q]), 5.0f);
    VDIV(2, 0, 0, 0);           // divide by zero -> +FLT_MAX, D status
    CHECK_EQ(cpu.vi[VU_Q], 0x7F7FFFFFu);
    CHECK(cpu.vi[VU_STATUS] & 0x20u);
    VFTOI4(0xF, 6, 1);          // fixed point 28.4
    CHECK_EQ(cpu.vf[6].c[0], 16u);
    VITOF4(0xF, 7, 6);
    CHECK_EQ(vf(7, 0), 1.0f);
    VMR32(0xF, 8, 1);
    CHECK_EQ(vf(8, 0), 2.0f);
    CHECK_EQ(vf(8, 3), 1.0f);
    // Cross product with VOPMULA/VOPMSUB: (1,0,0) x (0,1,0) = (0,0,1)
    setVf(9, 1, 0, 0, 0);
    setVf(10, 0, 1, 0, 0);
    VOPMULA(0xE, 9, 10);
    VOPMSUB(0xE, 11, 10, 9);
    CHECK_EQ(vf(11, 2), 1.0f);
    VADD(0xF, 0, 1, 2);         // writes to vf0 vanish
    CHECK_EQ(cpu.vf[0].c[3], 0x3F800000u);
}

TEST_CASE(vu0_clip_and_integer_unit) {
    reset();
    setVf(1, 5, -5, 0.5f, 0);
    setVf(2, 0, 0, 0, 1);
    VCLIPw(0xE, 1, 2);
    CHECK_EQ(cpu.vi[VU_CLIP] & 0x3F, 0x01u | 0x08u);   // +x outside, -y outside
    cpu.vi[1] = 0xFFFF;
    cpu.vi[2] = 2;
    VIADD(3, 1, 2);
    CHECK_EQ(cpu.vi[3], 1u);                            // 16-bit wrap
    VIADDI(4, 2, -3);
    CHECK_EQ(cpu.vi[4], 0xFFFFu);
    // VSQI / VLQI walk VU0 memory in quadwords.
    cpu.vi[5] = 0;
    setVf(12, 1, 2, 3, 4);
    VSQI(0xF, 12, 5);
    VSQI(0xF, 12, 5);
    CHECK_EQ(cpu.vi[5], 2u);
    cpu.vi[6] = 1;
    VLQI(0xF, 13, 6);
    CHECK_EQ(vf(13, 3), 4.0f);
    CHECK_EQ(cpu.vi[6], 2u);
    // CFC2/CTC2 for the R register keep the 1.0 exponent.
    setGpr(A0, 0x12345678);
    CTC2(A0, VU_R);
    CHECK_EQ(cpu.vi[VU_R], 0x3F800000u | 0x345678u);
}

// A VU1 micro program: a stall holds the whole instruction pair, so an
// operation on Q paired with WAITQ, or with a DIV that waits for a busy FDIV,
// reads the Q that lands at the end of the stall; one paired with a DIV that
// starts at once, or issued a cycle after one, reads the Q from before it.
TEST_CASE(vu_stalls_hold_both_halves_of_a_pair) {
    vu1Reset();
    auto fl = [](float v) { return std::bit_cast<uint32_t>(v); };
    auto setMem = [&](uint32_t qw, float x, float y, float z, float w) {
        const uint32_t v[4] = {fl(x), fl(y), fl(z), fl(w)};
        std::memcpy(vu1Mem + qw * 16, v, 16);
    };
    setMem(0, 8, 6, 10, 2);        // vf01
    setMem(1, 2, 3, 4, 1);         // vf02: quotients 4, 2, 2.5, 2
    setMem(2, 1, 10, 100, 1000);   // vf04
    const uint32_t upperNop = 0x000002FFu, lowerNop = 0x8000033Cu, ebit = 0x40000000u;
    auto lq = [](uint32_t ft, uint32_t imm) { return (0xFu << 21) | (ft << 16) | (imm & 0x7FF); };   // lq.xyzw vft, imm(vi00)
    auto sq = [](uint32_t fs, uint32_t imm) { return 0x02000000u | (0xFu << 21) | (fs << 11) | (imm & 0x7FF); };
    auto div = [](uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
        return 0x80000000u | (ftf << 23) | (fsf << 21) | (ft << 16) | (fs << 11) | (0xEu << 6) | 0x3Cu;
    };
    const uint32_t waitq = 0x80000000u | (0xEu << 6) | 0x3Fu;
    auto mulq = [](uint32_t fd, uint32_t fs) { return (0xFu << 21) | (fs << 11) | (fd << 6) | 0x1Cu; };   // mulq.xyzw vfd, vfs, Q
    const uint32_t program[][2] = {   // lower, upper
        {lq(1, 0), upperNop},
        {lq(2, 1), upperNop},
        {lq(4, 2), upperNop},
        {div(1, 0, 2, 0), upperNop},    // Q = 4 in 7 cycles
        {waitq, mulq(3, 4)},            // waits for it: vf03 = 4 * vf04
        {div(1, 1, 2, 1), mulq(5, 4)},  // starts Q = 2; the pair reads the Q before it: vf05 = 4 * vf04
        {lowerNop, upperNop},
        {div(1, 2, 2, 2), mulq(7, 4)},  // the FDIV is busy: waits for Q = 2, then reads it: vf07 = 2 * vf04; starts Q = 2.5
        {waitq, upperNop},
        {lowerNop, mulq(8, 4)},         // vf08 = 2.5 * vf04
        {div(1, 3, 2, 3), upperNop},    // starts Q = 2
        {lowerNop, mulq(9, 4)},         // a cycle later, without waiting: still the Q before it, vf09 = 2.5 * vf04
        {sq(3, 4), upperNop},
        {sq(5, 5), upperNop},
        {sq(7, 6), upperNop},
        {sq(8, 7), upperNop},
        {sq(9, 8), upperNop | ebit},
        {lowerNop, upperNop},
    };
    std::memcpy(vu1Micro, program, sizeof program);
    vu1Start(0);
    auto at = [](uint32_t qw, int c) {
        uint32_t v;
        std::memcpy(&v, vu1Mem + qw * 16 + 4 * c, 4);
        return std::bit_cast<float>(v);
    };
    CHECK_EQ(at(4, 0), 4.0f);
    CHECK_EQ(at(4, 3), 4000.0f);
    CHECK_EQ(at(5, 1), 40.0f);
    CHECK_EQ(at(6, 0), 2.0f);
    CHECK_EQ(at(6, 2), 200.0f);
    CHECK_EQ(at(7, 0), 2.5f);
    CHECK_EQ(at(7, 3), 2500.0f);
    CHECK_EQ(at(8, 0), 2.5f);
    CHECK_EQ(at(8, 1), 25.0f);
    std::memset(vu1Micro, 0, sizeof program);
    std::memset(vu1Mem, 0, 9 * 16);
    vu1Reset();
}
TEST_CASE(guest_printf_formatting) {
    reset();
    const uint32_t fmt = 0x00300000, str = 0x00300100;
    auto put = [](uint32_t a, const char* s) { std::memcpy(ram + a, s, std::strlen(s) + 1); };
    put(fmt, "%d|%5u|%-4x|%s|%c|%llx|%.2f|%%");
    put(str, "abc");
    setGpr(A0, fmt);
    setGpr(A1, static_cast<uint64_t>(-7));
    setGpr(A2, 42);
    setGpr(A3, 0xAB);
    setGpr(T0, str);
    setGpr(T1, 'Z');
    setGpr(T2, 0x123456789ull);
    setGpr(T3, std::bit_cast<uint64_t>(3.14159));
    CHECK_EQ(formatGuestString(fmt, 1), std::string("-7|   42|ab  |abc|Z|123456789|3.14|%"));
    // Arguments past the eighth come from the stack, 8 bytes per slot.
    put(fmt, "%d %d");
    setGpr(SP, 0x00300200);
    write64(0x00300200, 99);
    GuestArgs args(7);   // slot 7 is $t3, slot 8 the first stack slot
    setGpr(T3, 5);
    CHECK_EQ(formatGuest("%d %d", args), std::string("5 99"));
    // The runtime keeps the host FPU in round-toward-zero; decimal output must
    // still round to nearest.
    const int mode = std::fegetround();
    std::fesetround(FE_TOWARDZERO);
    put(fmt, "%.3f %.6f");
    setGpr(A1, std::bit_cast<uint64_t>(3.14159));
    setGpr(A2, std::bit_cast<uint64_t>(7.4854708605503449));
    CHECK_EQ(formatGuestString(fmt, 1), std::string("3.142 7.485471"));
    std::fesetround(mode);
}

TEST_CASE(gs_swizzle_is_a_bijection_per_page) {
    struct Fmt {
        uint32_t psm, w, h, units;
    };
    // Page geometry per format: pixels wide/high and memory units per page.
    const Fmt fmts[] = {{0x00, 64, 32, 2048}, {0x02, 64, 64, 4096}, {0x0A, 64, 64, 4096},
                        {0x13, 128, 64, 8192}, {0x14, 128, 128, 16384}, {0x30, 64, 32, 2048}};
    for (const Fmt& f : fmts) {
        std::set<uint32_t> seen;
        for (uint32_t y = 0; y < f.h; ++y)
            for (uint32_t x = 0; x < f.w; ++x) {
                const uint32_t a = gs::debugAddress(f.psm, 0, f.psm >= 0x13 && f.psm < 0x30 ? 2 : 1, x, y);
                if (a >= f.units) ::testing::fail(__FILE__, __LINE__, "address outside the first page");
                seen.insert(a);
            }
        if (seen.size() != f.units) ::testing::fail(__FILE__, __LINE__, "swizzle is not a bijection for psm " + std::to_string(f.psm));
    }
    // PSMCT32 block layout spot checks (GS manual): block 1 is the 8x8 to the right of block 0.
    CHECK_EQ(gs::debugAddress(0x00, 0, 1, 8, 0), 64u);
    CHECK_EQ(gs::debugAddress(0x00, 0, 1, 0, 8), 128u);
    CHECK_EQ(gs::debugAddress(0x00, 0, 1, 2, 0), 4u);
    CHECK_EQ(gs::debugAddress(0x00, 0, 1, 0, 1), 2u);
}

TEST_CASE(gs_swizzle_blocks_follow_the_manual) {
    // Each block's number within its page, row by row of blocks, as the GS
    // User's Manual draws them; the Z layouts swap block groups (^ 0x18).
    static constexpr int kCt32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
                                        {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
    static constexpr int kCt16[8][4] = {{0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15},
                                        {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
    static constexpr int kCt16s[8][4] = {{0, 2, 16, 18}, {1, 3, 17, 19}, {8, 10, 24, 26}, {9, 11, 25, 27},
                                         {4, 6, 20, 22}, {5, 7, 21, 23}, {12, 14, 28, 30}, {13, 15, 29, 31}};
    struct Layout {
        uint32_t psm, bw, blockW, blockH, units;   // units: memory units per block
        int cols;
        const int* table;
        int flip;
    };
    const Layout layouts[] = {
        {0x00, 1, 8, 8, 64, 8, &kCt32[0][0], 0},       {0x30, 1, 8, 8, 64, 8, &kCt32[0][0], 0x18},     // CT32, Z32
        {0x02, 1, 16, 8, 128, 4, &kCt16[0][0], 0},     {0x32, 1, 16, 8, 128, 4, &kCt16[0][0], 0x18},   // CT16, Z16
        {0x0A, 1, 16, 8, 128, 4, &kCt16s[0][0], 0},    {0x3A, 1, 16, 8, 128, 4, &kCt16s[0][0], 0x18},  // CT16S, Z16S
        {0x13, 2, 16, 16, 256, 8, &kCt32[0][0], 0},    {0x14, 2, 32, 16, 512, 4, &kCt16[0][0], 0},     // T8, T4
    };
    for (const Layout& l : layouts) {
        for (int i = 0; i < 32; ++i) {
            const int bx = i % l.cols, by = i / l.cols;
            const uint32_t a = gs::debugAddress(l.psm, 0, l.bw, static_cast<uint32_t>(bx) * l.blockW, static_cast<uint32_t>(by) * l.blockH);
            CHECK_EQ(a / l.units, static_cast<uint32_t>(l.table[i] ^ l.flip));
            CHECK_EQ(a % l.units, 0u);   // the block's first unit
        }
    }
}

namespace {

// GS register writes as one PACKED A+D packet down PATH3.
void gsWrite(std::initializer_list<std::pair<uint32_t, uint64_t>> regs) {
    std::vector<uint64_t> q = {regs.size() | 1ull << 15 | 1ull << 60, 0xE};   // NLOOP, EOP, NREG 1; A+D
    for (const auto& [reg, data] : regs) {
        q.push_back(data);
        q.push_back(reg);
    }
    gs::gifTransfer(q.data(), q.size() / 2, 3);
}
uint64_t xyz(int x, int y, uint32_t z) { return static_cast<uint64_t>(x * 16) | static_cast<uint64_t>(y * 16) << 16 | static_cast<uint64_t>(z) << 32; }
// A flat sprite over [x0, x1) x [y0, y1) in the current context.
void gsRect(int x0, int y0, int x1, int y1, uint32_t rgba) {
    gsWrite({{0x00, 6}, {0x01, rgba}, {0x05, xyz(x0, y0, 0)}, {0x05, xyz(x1, y1, 0)}});
}

} // namespace

TEST_CASE(gs_layers_at_one_depth_do_not_fight) {
    // A float keeps 24 bits of Z: 0x89ABCDEF became 0x89ABCE00, and a sprite
    // drawn at the triangle's own depth failed GEQUAL against it.
    gs::init();
    constexpr uint32_t kZ = 0x89ABCDEFu;
    gsWrite({{0x4C, 1ull << 16},                              // FRAME_1: page 0, 64 wide, PSMCT32
             {0x4E, 16},                                      // ZBUF_1: page 16, PSMZ32
             {0x40, 63ull << 16 | 63ull << 48},               // SCISSOR_1: 64x64
             {0x18, 0}, {0x1A, 1},                            // XYOFFSET_1, PRMODECONT
             {0x47, 1ull << 16 | 1ull << 17},                 // TEST_1: Z test ALWAYS
             {0x00, 6}, {0x01, 0x80000000}, {0x05, xyz(0, 0, 0)}, {0x05, xyz(64, 64, 0)},
             {0x47, 1ull << 16 | 2ull << 17},                 // Z test GEQUAL
             {0x00, 3}, {0x01, 0x800000FF}, {0x05, xyz(0, 0, kZ)}, {0x05, xyz(64, 0, kZ)}, {0x05, xyz(0, 64, kZ)},
             {0x00, 6}, {0x01, 0x8000FF00}, {0x05, xyz(0, 0, kZ)}, {0x05, xyz(32, 32, kZ)}});
    for (int y = 0; y < 32; y += 3)
        for (int x = 0; x < 32; x += 3) CHECK_EQ(gs::readPixel32(0, 1, 0, x, y), 0x8000FF00u);
}

TEST_CASE(gs_display_doubles_fields_and_merges_circuits) {
    gs::init();
    gsWrite({{0x4C, 1ull << 16}, {0x40, 63ull << 16 | 63ull << 48}, {0x18, 0}, {0x1A, 1}, {0x47, 0}});
    const uint32_t rows[4] = {0x80000010, 0x80000020, 0x80000030, 0x80000040};
    for (int y = 0; y < 4; ++y) gsRect(0, y, 64, y + 1, rows[y]);       // page 0: four coloured lines
    gsWrite({{0x4C, 2 | 1ull << 16}});                                   // page 2: blue opaque, then clear
    gsRect(0, 0, 8, 4, 0x80FF0000);
    gsRect(8, 0, 16, 4, 0x00FFFFFF);
    auto pixel = [](const gs::Frame& f, int x, int y) { return f.pixels[static_cast<size_t>(y * f.width + x)]; };

    // Interlaced FRAME mode: each line of the field covers two display lines.
    gs::privWrite(0x12000020, 3);                                        // SMODE2: INT, FFMD
    gs::privWrite(0x12000000, 0x6);                                      // PMODE: circuit 2
    gs::privWrite(0x12000090, 1 << 9);                                   // DISPFB2: page 0, 64 wide
    gs::privWrite(0x120000A0, 63ull << 32 | 7ull << 44);                 // DISPLAY2: 64 clocks, 8 lines
    gs::Frame f;
    CHECK(gs::currentFrame(f));
    CHECK_EQ(f.width, 64);
    CHECK_EQ(f.height, 8);
    for (int y = 0; y < 8; ++y) CHECK_EQ(pixel(f, 5, y), rows[y / 2] | 0xFF000000u);

    // Both circuits: circuit 1 (16 clocks at x 32) over circuit 2, by its pixels' alpha.
    gs::privWrite(0x12000020, 0);
    gs::privWrite(0x120000A0, 63ull << 32 | 3ull << 44);
    gs::privWrite(0x12000070, 2 | 1 << 9);                              // DISPFB1: page 2
    gs::privWrite(0x12000080, 32 | 15ull << 32 | 3ull << 44);           // DISPLAY1
    gs::privWrite(0x12000000, 0x7);                                      // PMODE: both, MMOD 0, SLBG 0
    CHECK(gs::currentFrame(f));
    CHECK_EQ(f.width, 64);
    CHECK_EQ(pixel(f, 34, 1), 0xFFFF0000u);                              // circuit 1, alpha 0x80
    CHECK_EQ(pixel(f, 42, 1), rows[1] | 0xFF000000u);                    // alpha 0: circuit 2 shows
    CHECK_EQ(pixel(f, 5, 1), rows[1] | 0xFF000000u);                     // outside circuit 1

    // Circuit 1 alone fades towards BGCOLOR with SLBG; without it ALP does nothing.
    gs::privWrite(0x120000E0, 0xFF);                                     // BGCOLOR: red
    gs::privWrite(0x12000000, 0x1 | 0x4 | 1 << 5 | 1 << 7 | 0x80 << 8);  // EN1, MMOD, SLBG, ALP 0x80
    CHECK(gs::currentFrame(f));
    CHECK_EQ(f.width, 16);
    CHECK_EQ(pixel(f, 2, 0), 0xFF80007Fu);                               // half blue, half red
    gs::privWrite(0x12000000, 0x1 | 0x4 | 1 << 5 | 0x80 << 8);           // ps2sdk's default PMODE
    CHECK(gs::currentFrame(f));
    CHECK_EQ(pixel(f, 2, 0), 0xFFFF0000u);
}

TEST_CASE(gs_deinterlaces_fields) {
    // FIELD mode, as Agent Under Fire draws: a 4-line field shown on 8 lines,
    // redrawn every vsync with the odd field's samples a line lower.
    gs::init();
    gsWrite({{0x4C, 1ull << 16}, {0x40, 63ull << 16 | 63ull << 48}, {0x18, 0}, {0x1A, 1}, {0x47, 0}});
    gs::privWrite(0x12000020, 3);                                        // SMODE2: INT, FFMD
    gs::privWrite(0x12000000, 0x6);                                      // PMODE: circuit 2
    gs::privWrite(0x12000090, 1 << 9);                                   // DISPFB2: page 0, 64 wide
    gs::privWrite(0x120000A0, 63ull << 32 | 7ull << 44);                 // DISPLAY2: 64 clocks, 8 lines
    const uint32_t even[4] = {0x80000010, 0x80000020, 0x80000030, 0x80000040};
    const uint32_t odd[4] = {0x80001000, 0x80002000, 0x80003000, 0x80004000};
    auto draw = [](const uint32_t (&rows)[4]) {
        for (int y = 0; y < 4; ++y) gsRect(0, y, 64, y + 1, rows[y]);
    };
    auto field = [&](const uint32_t (&rows)[4]) {   // the next vsync's field, drawn
        gs::onVblankStart();
        draw(rows);
        gs::Frame f;
        CHECK(gs::currentFrame(f));
        CHECK_EQ(f.height, 8);
        std::array<uint32_t, 8> lines{};
        for (int y = 0; y < 8 && f.height == 8; ++y) lines[size_t(y)] = f.pixels[static_cast<size_t>(y * f.width + 5)] & 0xFFFFFFu;
        return lines;
    };
    auto avg = [](uint32_t a, uint32_t b) {
        uint32_t px = 0;
        for (int s = 0; s < 24; s += 8) px |= ((((a >> s) & 0xFF) + ((b >> s) & 0xFF) + 1) / 2) << s;
        return px;
    };
    auto rgb = [](uint32_t c) { return c & 0xFFFFFFu; };

    // The first field (odd) has nothing before it: its lines, doubled.
    auto l = field(odd);
    CHECK(l == (std::array<uint32_t, 8>{rgb(odd[0]), rgb(odd[0]), rgb(odd[0]), rgb(odd[1]), rgb(odd[1]), rgb(odd[2]), rgb(odd[2]), rgb(odd[3])}));
    // The second (even) does not know yet what is still: interpolated.
    l = field(even);
    CHECK_EQ(l[0], rgb(even[0]));
    CHECK_EQ(l[1], avg(even[0], even[1]));
    CHECK_EQ(l[7], rgb(even[3]));
    // The third (odd again) matches the first: still, so the two fields
    // weave into the whole picture, and it stays that way.
    const std::array<uint32_t, 8> whole = {rgb(even[0]), rgb(odd[0]), rgb(even[1]), rgb(odd[1]),
                                           rgb(even[2]), rgb(odd[2]), rgb(even[3]), rgb(odd[3])};
    CHECK(field(odd) == whole);
    CHECK(field(even) == whole);
    // Something moves in the odd field's line 1 (display line 3): around it,
    // interpolated; elsewhere still woven.
    const uint32_t moved[4] = {odd[0], 0x80FF0000, odd[2], odd[3]};
    l = field(moved);
    CHECK_EQ(l[2], avg(odd[0], moved[1]));
    CHECK_EQ(l[3], rgb(moved[1]));
    CHECK_EQ(l[4], avg(moved[1], odd[2]));
    CHECK_EQ(l[6], rgb(even[3]));                                        // still: the even field's line
    // Weave takes the other field's lines whatever moved; Bob doubles.
    gs::setDeinterlace(gs::Deinterlace::Weave);
    l = field(even);
    CHECK_EQ(l[3], rgb(moved[1]));
    gs::setDeinterlace(gs::Deinterlace::Bob);
    l = field(odd);
    CHECK_EQ(l[2], rgb(odd[0]));
    gs::setDeinterlace(gs::Deinterlace::Adaptive);
    // A frame read twice in one field: the same picture again.
    gs::Frame f;
    CHECK(gs::currentFrame(f));
    CHECK_EQ(f.pixels[static_cast<size_t>(2 * f.width + 5)] & 0xFFFFFFu, rgb(odd[0]));
    // Fields that are not consecutive (frames dumped every few vsyncs) are not woven either.
    gs::onVblankStart();
    gs::onVblankStart();
    l = field(even);
    CHECK_EQ(l[1], rgb(even[0]));
}

TEST_CASE(gs_deinterlacing_keeps_late_fields_in_place) {
    // Agent Under Fire draws each field for the field it is meant for, the
    // odd one half a line up (OFY + 8), and flips at the vsync. When it is
    // late, a picture shows twice, or reaches the screen in the field after
    // the one it was drawn for: by the FIELD bit it would jump a line.
    auto setUp = [] {   // FIELD mode, as in gs_deinterlaces_fields; the FIELD bit starts even
        gs::init();
        gsWrite({{0x4C, 1ull << 16}, {0x40, 63ull << 16 | 63ull << 48}, {0x1A, 1}, {0x47, 0}});
        gs::privWrite(0x12000020, 3);                                    // SMODE2: INT, FFMD
        gs::privWrite(0x12000000, 0x6);                                  // PMODE: circuit 2
        gs::privWrite(0x12000090, 1 << 9);                               // DISPFB2: page 0, 64 wide
        gs::privWrite(0x120000A0, 63ull << 32 | 7ull << 44);             // DISPLAY2: 64 clocks, 8 lines
    };
    const uint32_t even[4] = {0x80000010, 0x80000020, 0x80000030, 0x80000040};
    const uint32_t odd[4] = {0x80001000, 0x80002000, 0x80003000, 0x80004000};
    // A field's four lines, drawn for the odd field (half a line up) or the even one.
    auto draw = [](const uint32_t (&rows)[4], bool forOdd) {
        const uint64_t half = forOdd ? 8 : 0;
        gsWrite({{0x18, half << 32}});                                    // XYOFFSET_1
        for (uint64_t y = 0; y < 4; ++y)
            gsWrite({{0x00, 6}, {0x01, rows[y]}, {0x05, (y * 16 + half) << 16}, {0x05, 64 * 16 | ((y + 1) * 16 + half) << 16}});
    };
    // The next vsync's picture, `rows` drawn for it (or nothing drawn: the
    // game is late, and the buffer still holds the last picture).
    auto field = [&](const uint32_t (*rows)[4], bool forOdd) {
        gs::onVblankStart();
        if (rows) draw(*rows, forOdd);
        gs::Frame f;
        CHECK(gs::currentFrame(f));
        CHECK_EQ(f.height, 8);
        std::array<uint32_t, 8> lines{};
        for (int y = 0; y < 8 && f.height == 8; ++y) lines[size_t(y)] = f.pixels[static_cast<size_t>(y * f.width + 5)] & 0xFFFFFFu;
        return lines;
    };
    auto avg = [](uint32_t a, uint32_t b) {
        uint32_t px = 0;
        for (int s = 0; s < 24; s += 8) px |= ((((a >> s) & 0xFF) + ((b >> s) & 0xFF) + 1) / 2) << s;
        return px;
    };
    auto rgb = [](uint32_t c) { return c & 0xFFFFFFu; };
    const std::array<uint32_t, 8> whole = {rgb(even[0]), rgb(odd[0]), rgb(even[1]), rgb(odd[1]),
                                           rgb(even[2]), rgb(odd[2]), rgb(even[3]), rgb(odd[3])};
    const uint32_t moved[4] = {odd[0], 0x80FF0000, odd[2], odd[3]};

    // A game that draws both fields alike: each goes where the FIELD bit puts
    // it. A picture shown twice stays as it was, where it moved too.
    setUp();
    field(&odd, false);
    field(&even, false);
    CHECK(field(&odd, false) == whole);
    const auto before = field(&moved, false);                            // FIELD even
    CHECK_EQ(before[2], rgb(moved[1]));
    CHECK(field(nullptr, false) == before);

    // Drawn for each field, on time (the FIELD bit odd, even, odd, even):
    // woven into the whole picture.
    setUp();
    field(&odd, true);
    field(&even, false);
    CHECK(field(&odd, true) == whole);
    CHECK(field(&even, false) == whole);
    // Late: the odd field shows the even picture again. It stays as it was,
    // not moved down a line.
    CHECK(field(nullptr, false) == whole);
    // The picture drawn for that odd field arrives in the even one, a line of
    // it changed: on the odd lines all the same, woven where it is still.
    auto l = field(&moved, true);
    CHECK(l == (std::array<uint32_t, 8>{rgb(even[0]), rgb(odd[0]), avg(odd[0], moved[1]), rgb(moved[1]), avg(moved[1], odd[2]),
                                        rgb(odd[2]), rgb(even[3]), rgb(odd[3])}));
    // The next one is drawn for the odd field too: on the odd lines again,
    // and where it matches the last field, the last picture's lines stay.
    const uint32_t moved2[4] = {odd[0], 0x80FF0000, odd[2], 0x8000FF00};
    l = field(&moved2, true);
    CHECK(l == (std::array<uint32_t, 8>{rgb(even[0]), rgb(odd[0]), avg(odd[0], moved[1]), rgb(moved[1]), avg(moved[1], odd[2]),
                                        rgb(odd[2]), avg(odd[2], moved2[3]), rgb(moved2[3])}));
    // Bob puts each field where the FIELD bit says, as the PS2 sends it: this
    // even field's picture, drawn for the odd one, on the even lines.
    gs::setDeinterlace(gs::Deinterlace::Bob);
    l = field(&moved, true);
    CHECK_EQ(l[2], rgb(moved[1]));
    CHECK_EQ(l[3], rgb(moved[1]));
    gs::setDeinterlace(gs::Deinterlace::Adaptive);
}

namespace {

// Host-to-local transfer: w x h pixels to the top left of a buffer at block
// `dbp`, from words packing them as the format does (one per word for
// PSMCT32, four per word for PSMT8). The GIF packets, then sending them.
std::vector<uint64_t> uploadPackets(uint32_t dbp, uint32_t dbw, uint32_t psm, int w, int h, const std::vector<uint32_t>& words) {
    std::vector<uint64_t> q = {4 | 1ull << 15 | 1ull << 60, 0xE,   // A+D: BITBLTBUF TRXPOS TRXREG TRXDIR
                               static_cast<uint64_t>(dbp) << 32 | static_cast<uint64_t>(dbw) << 48 | static_cast<uint64_t>(psm) << 56, 0x50,
                               0, 0x51, static_cast<uint64_t>(w) | static_cast<uint64_t>(h) << 32, 0x52, 0, 0x53};
    auto at = [&](size_t i) { return static_cast<uint64_t>(i < words.size() ? words[i] : 0); };
    const size_t qwords = (words.size() + 3) / 4;
    for (size_t done = 0; done < qwords;) {   // IMAGE tags: NLOOP has 15 bits
        const size_t n = std::min<size_t>(qwords - done, 0x4000);
        q.push_back(n | (done + n == qwords ? 1ull << 15 : 0) | 2ull << 58);
        q.push_back(0);
        for (size_t i = 4 * done; i < 4 * (done + n); i += 4) {
            q.push_back(at(i) | at(i + 1) << 32);
            q.push_back(at(i + 2) | at(i + 3) << 32);
        }
        done += n;
    }
    return q;
}
void gsUpload(uint32_t dbp, uint32_t dbw, uint32_t psm, int w, int h, const std::vector<uint32_t>& words) {
    const std::vector<uint64_t> q = uploadPackets(dbp, dbw, psm, w, h, words);
    gs::gifTransfer(q.data(), q.size() / 2, 3);
}

std::vector<uint32_t> pseudoRandom(size_t n, uint32_t seed) {
    std::vector<uint32_t> v(n);
    for (uint32_t& w : v) w = seed = seed * 1664525u + 1013904223u;
    return v;
}

uint64_t bits64(float f) { return std::bit_cast<uint32_t>(f); }

// A scene of large primitives, drawn by `threads` threads under the EE's
// round-to-zero (the guest thread's, which the drawing inherits), on the GS's
// own thread if `ownThread`. Returns the frame buffer and the depth buffer.
std::vector<uint32_t> drawScene(int threads, bool ownThread = false) {
    gs::init();
    gs::setDrawThreads(threads);
    gs::setOwnThread(ownThread);
    const int mode = std::fegetround();
    std::fesetround(FE_TOWARDZERO);
    // A 64x64 PSMCT32 texture at page 64, a 64x64 PSMT8 one at page 72 and its
    // 256-entry CLUT at page 80.
    gsUpload(64 * 32, 1, 0x00, 64, 64, pseudoRandom(64 * 64, 1));
    gsUpload(72 * 32, 2, 0x13, 64, 64, pseudoRandom(64 * 64 / 4, 2));
    gsUpload(80 * 32, 1, 0x00, 16, 16, pseudoRandom(256, 3));
    const uint64_t frame = 4ull << 16;                              // FRAME_1: page 0, 256 wide, PSMCT32
    gsWrite({{0x4C, frame}, {0x4E, 32},                             // ZBUF_1: page 32, PSMZ32
             {0x40, 255ull << 16 | 127ull << 48}, {0x18, 0}, {0x1A, 1},
             {0x3D, 0x204060},                                       // FOGCOL
             {0x42, 0x44},                                           // ALPHA_1: (Cs - Cd) * As + Cd
             {0x47, 1ull << 16 | 1ull << 17},                        // TEST_1: depth ALWAYS
             {0x00, 6}, {0x01, 0x80302010}, {0x05, xyz(0, 0, 0)}, {0x05, xyz(256, 128, 0)}});
    // A Gouraud triangle, perspective-textured, fogged and blended, depth GEQUAL.
    const uint64_t tex32 = 2048 | 1ull << 14 | 6ull << 26 | 6ull << 30 | 1ull << 34;   // TBP0, TBW 1, 64x64, TCC, modulate
    gsWrite({{0x06, tex32}, {0x08, 0}, {0x47, 1ull << 16 | 2ull << 17},
             {0x00, 3 | 1 << 3 | 1 << 4 | 1 << 5 | 1 << 6},
             {0x02, bits64(0.0f) | bits64(0.0f) << 32}, {0x01, 0x80FF4020 | bits64(1.0f) << 32}, {0x04, xyz(0, 0, 0x100000) | 0x10ull << 56},
             {0x02, bits64(2.0f) | bits64(0.5f) << 32}, {0x01, 0x6020FF40 | bits64(2.0f) << 32}, {0x04, xyz(256, 8, 0x300000) | 0xF0ull << 56},
             {0x02, bits64(0.25f) | bits64(1.5f) << 32}, {0x01, 0x404080FF | bits64(1.5f) << 32}, {0x04, xyz(16, 128, 0x200000) | 0x80ull << 56}});
    // A sprite from the CLUT texture, by texel coordinates, behind an alpha test.
    const uint64_t tex8 = 2304 | 2ull << 14 | 0x13ull << 20 | 6ull << 26 | 6ull << 30 | 1ull << 34 | 1ull << 35 | 2560ull << 37;
    gsWrite({{0x06, tex8}, {0x08, 0}, {0x47, 1 | 6 << 1 | 0x40 << 4 | 1ull << 16 | 1ull << 17},
             {0x00, 6 | 1 << 4 | 1 << 8}, {0x03, 0}, {0x05, xyz(32, 16, 0x400000)},
             {0x03, (64 * 16) | static_cast<uint64_t>(64 * 16) << 16}, {0x05, xyz(224, 112, 0x400000)}});
    // A sprite copying the frame onto itself four rows down: it reads rows it
    // writes, so it must be drawn by one thread whatever the setting.
    const uint64_t texFrame = 0 | 4ull << 14 | 8ull << 26 | 7ull << 30 | 1ull << 34 | 1ull << 35;
    gsWrite({{0x06, texFrame}, {0x08, 0}, {0x47, 0},
             {0x00, 6 | 1 << 4 | 1 << 8}, {0x03, 0}, {0x05, xyz(0, 4, 0)},
             {0x03, (256 * 16) | static_cast<uint64_t>(124 * 16) << 16}, {0x05, xyz(256, 128, 0)}});
    std::vector<uint32_t> out;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 256; ++x) {
            out.push_back(gs::readPixel32(0, 4, 0x00, x, y));
            out.push_back(gs::readPixel32(32, 4, 0x30, x, y));
        }
    std::fesetround(mode);
    gs::setOwnThread(false);
    gs::setDrawThreads(0);
    return out;
}

} // namespace

TEST_CASE(gs_threads_draw_what_one_thread_draws) {
    const std::vector<uint32_t> one = drawScene(1);
    CHECK(std::set<uint32_t>(one.begin(), one.end()).size() > 1000);   // the scene is not a flat fill
    for (int threads : {3, 4}) {
        const std::vector<uint32_t> many = drawScene(threads);
        size_t differ = 0;
        for (size_t i = 0; i < one.size(); ++i) differ += one[i] != many[i];
        CHECK_EQ(differ, size_t(0));
    }
}

namespace {

// A+D writes, in packets of up to 32767 registers.
void gsWriteAll(const std::vector<std::pair<uint32_t, uint64_t>>& regs) {
    for (size_t at = 0; at < regs.size();) {
        const size_t n = std::min<size_t>(regs.size() - at, 0x7FFF);
        std::vector<uint64_t> q = {n | 1ull << 15 | 1ull << 60, 0xE};
        for (size_t i = at; i < at + n; ++i) {
            q.push_back(regs[i].second);
            q.push_back(regs[i].first);
        }
        gs::gifTransfer(q.data(), q.size() / 2, 3);
        at += n;
    }
}

// Many small primitives, drawn by `threads` threads (on the GS's own thread
// if `ownThread`), with what makes the draw threads wait between them: a
// texture drawn earlier in the batch, a texture drawn or copied over after
// primitives that sample it, the same memory drawn under another layout, and
// an upload over a texture in use. Returns every buffer involved.
std::vector<uint32_t> drawBatchScene(int threads, bool ownThread = false) {
    gs::init();
    gs::setDrawThreads(threads);
    gs::setOwnThread(ownThread);
    const int mode = std::fegetround();
    std::fesetround(FE_TOWARDZERO);
    // Textures as in drawScene: 64x64 PSMCT32 at page 64, PSMT8 at page 72, its CLUT at page 80.
    gsUpload(64 * 32, 1, 0x00, 64, 64, pseudoRandom(64 * 64, 11));
    gsUpload(72 * 32, 2, 0x13, 64, 64, pseudoRandom(64 * 64 / 4, 12));
    gsUpload(80 * 32, 1, 0x00, 16, 16, pseudoRandom(256, 13));
    gsUpload(100 * 32, 1, 0x00, 32, 32, pseudoRandom(32 * 32, 15));   // copied over page 72 below
    const uint64_t frame = 4ull << 16;   // FRAME_1: page 0, 256 wide, PSMCT32
    gsWrite({{0x4C, frame}, {0x4E, 32}, {0x40, 255ull << 16 | 127ull << 48}, {0x18, 0}, {0x1A, 1}, {0x42, 0x44},
             {0x47, 1ull << 16 | 1ull << 17}, {0x00, 6}, {0x01, 0x80302010}, {0x05, xyz(0, 0, 0)}, {0x05, xyz(256, 128, 0)}});
    uint32_t seed = 77;
    auto rnd = [&](uint32_t n) {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<int>((seed >> 8) % n);
    };
    const uint64_t tex32 = 2048 | 1ull << 14 | 6ull << 26 | 6ull << 30 | 1ull << 34;
    const uint64_t tex8 = 2304 | 2ull << 14 | 0x13ull << 20 | 6ull << 26 | 6ull << 30 | 1ull << 34 | 2560ull << 37;
    const uint64_t texTarget = 96 * 32 | 1ull << 14 | 6ull << 26 | 6ull << 30 | 1ull << 34;   // page 96, drawn below
    // Small Gouraud triangles, textured by ST, blended, under depth GEQUAL.
    auto triangles = [&](std::vector<std::pair<uint32_t, uint64_t>>& r, int n, uint64_t tex0) {
        r.push_back({0x06, tex0});
        r.push_back({0x47, 1ull << 16 | 2ull << 17});
        r.push_back({0x00, 3 | 1 << 3 | 1 << 4 | 1 << 6});
        for (int i = 0; i < n; ++i) {
            const int x = rnd(244), y = rnd(116);
            for (int k = 0; k < 3; ++k) {
                r.push_back({0x02, bits64(static_cast<float>(rnd(64)) / 64.0f) | bits64(static_cast<float>(rnd(64)) / 64.0f) << 32});
                r.push_back({0x01, (0x80000000u | static_cast<uint32_t>(rnd(1 << 24))) | bits64(1.0f) << 32});
                r.push_back({0x05, xyz(x + rnd(13), y + rnd(13), static_cast<uint32_t>(rnd(1 << 20)))});
            }
        }
    };
    // Small flat sprites into the current frame.
    auto sprites = [&](std::vector<std::pair<uint32_t, uint64_t>>& r, int n, int w, int h) {
        r.push_back({0x47, 0});
        r.push_back({0x00, 6});
        for (int i = 0; i < n; ++i) {
            const int x = rnd(w - 6), y = rnd(h - 6);
            r.push_back({0x01, 0x80000000u | static_cast<uint32_t>(rnd(1 << 24))});
            r.push_back({0x05, xyz(x, y, 0)});
            r.push_back({0x05, xyz(x + 1 + rnd(6), y + 1 + rnd(6), 0)});
        }
    };
    // Blended sprites over rows 0-3 only, textured by UV across the whole
    // texture: they keep the first draw thread busy while the others go on.
    auto firstBand = [&](std::vector<std::pair<uint32_t, uint64_t>>& r, int n, uint64_t tex0) {
        r.push_back({0x06, tex0});
        r.push_back({0x47, 0});
        r.push_back({0x00, 6 | 1 << 4 | 1 << 6 | 1 << 8});
        for (int i = 0; i < n; ++i) {
            r.push_back({0x01, 0x80000000u | static_cast<uint32_t>(rnd(1 << 24))});
            r.push_back({0x03, static_cast<uint64_t>(rnd(64 * 16)) | static_cast<uint64_t>(rnd(64 * 16)) << 16});
            r.push_back({0x05, xyz(0, 0, 0)});
            r.push_back({0x03, static_cast<uint64_t>(rnd(64 * 16)) | static_cast<uint64_t>(rnd(64 * 16)) << 16});
            r.push_back({0x05, xyz(256, 4, 0)});
        }
    };
    std::vector<std::pair<uint32_t, uint64_t>> r;
    triangles(r, 600, tex32);
    triangles(r, 600, tex8);
    // Page 64 sampled, then drawn over: drawing it must wait for those.
    firstBand(r, 1500, tex32);
    r.push_back({0x4C, 64 | 1ull << 16});
    r.push_back({0x40, 63ull << 16 | 63ull << 48});
    sprites(r, 200, 64, 64);
    r.push_back({0x4C, frame});
    r.push_back({0x40, 255ull << 16 | 127ull << 48});
    // Page 72 sampled, then copied over within GS memory: the copy must wait for those.
    firstBand(r, 1500, tex8);
    r.push_back({0x50, static_cast<uint64_t>(100 * 32) | 1ull << 16 | static_cast<uint64_t>(72 * 32) << 32 | 1ull << 48});
    r.push_back({0x51, 0});
    r.push_back({0x52, 32 | 32ull << 32});
    r.push_back({0x53, 2});
    triangles(r, 300, tex8);
    // Page 96 drawn, then sampled: the batch must be drawn first.
    r.push_back({0x4C, 96 | 1ull << 16});
    r.push_back({0x40, 63ull << 16 | 63ull << 48});
    sprites(r, 200, 64, 64);
    r.push_back({0x4C, frame});
    r.push_back({0x40, 255ull << 16 | 127ull << 48});
    triangles(r, 300, texTarget);
    triangles(r, 300, tex32);
    // Page 120 drawn as PSMCT32, then as PSMCT16: other rows own its words.
    r.push_back({0x4C, 120 | 2ull << 16});
    r.push_back({0x40, 127ull << 16 | 63ull << 48});
    sprites(r, 200, 128, 64);
    r.push_back({0x4C, 120 | 2ull << 16 | 2ull << 24});
    sprites(r, 200, 128, 64);
    r.push_back({0x4C, frame});
    r.push_back({0x40, 255ull << 16 | 127ull << 48});
    // Points and a line strip.
    r.push_back({0x47, 0});
    r.push_back({0x00, 0});
    for (int i = 0; i < 300; ++i) {
        r.push_back({0x01, 0x80000000u | static_cast<uint32_t>(rnd(1 << 24))});
        r.push_back({0x05, xyz(rnd(256), rnd(128), 0)});
    }
    r.push_back({0x00, 2});
    for (int i = 0; i < 300; ++i) {
        r.push_back({0x01, 0x80000000u | static_cast<uint32_t>(rnd(1 << 24))});
        r.push_back({0x05, xyz(rnd(256), rnd(128), 0)});
    }
    triangles(r, 300, tex8);
    gsWriteAll(r);
    // An upload over the PSMT8 texture while primitives sampling it are queued, then more of them.
    gsUpload(72 * 32, 2, 0x13, 64, 64, pseudoRandom(64 * 64 / 4, 14));
    r.clear();
    triangles(r, 300, tex8);
    gsWriteAll(r);
    std::vector<uint32_t> out;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 256; ++x) {
            out.push_back(gs::readPixel32(0, 4, 0x00, x, y));
            out.push_back(gs::readPixel32(32, 4, 0x30, x, y));
        }
    for (const uint32_t page : {64u, 96u})
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) out.push_back(gs::readPixel32(page, 1, 0x00, x, y));
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x) out.push_back(gs::readPixel32(120, 2, 0x00, x, y));
    std::fesetround(mode);
    gs::setOwnThread(false);
    gs::setDrawThreads(0);
    return out;
}

} // namespace

TEST_CASE(gs_threads_share_small_primitives_in_order) {
    // Every primitive is shared by rows: the picture is one thread's, also
    // where a primitive depends on what the batch drew or read before it.
    const std::vector<uint32_t> one = drawBatchScene(1);
    CHECK(std::set<uint32_t>(one.begin(), one.end()).size() > 5000);
    for (const auto& [threads, ownThread] : {std::pair{3, false}, std::pair{4, false}, std::pair{2, true}, std::pair{4, true}}) {
        const std::vector<uint32_t> many = drawBatchScene(threads, ownThread);
        size_t differ = 0;
        for (size_t i = 0; i < one.size(); ++i) differ += one[i] != many[i];
        CHECK_EQ(differ, size_t(0));
    }
}

TEST_CASE(gs_thread_draws_what_the_guest_thread_draws) {
    // The GS thread takes the guest's rounding, and reading back waits for it.
    const std::vector<uint32_t> guest = drawScene(1);
    for (int threads : {2, 4}) {
        const std::vector<uint32_t> own = drawScene(threads, true);
        size_t differ = 0;
        for (size_t i = 0; i < guest.size(); ++i) differ += guest[i] != own[i];
        CHECK_EQ(differ, size_t(0));
    }
}

namespace {

// Keeps the GS thread busy for a while: large sprites at page 64 and above,
// away from what the tests show.
void gsBusyWork() {
    gsWrite({{0x4C, 64 | 8ull << 16}, {0x40, 511ull << 16 | 255ull << 48}, {0x18, 0}, {0x1A, 1}, {0x47, 0}});
    for (uint32_t i = 0; i < 16; ++i) gsRect(0, 0, 512, 256, 0x80000000u | i);
    gsWrite({{0x4C, 1ull << 16}, {0x40, 63ull << 16 | 63ull << 48}});   // back to page 0, 64 wide
}

} // namespace

TEST_CASE(gs_thread_events_reach_the_guest) {
    // FINISH and SIGNAL reached on the GS thread: CSR and SIGLBLID show them,
    // the interrupt comes when the guest thread looks (hw::intcPending at a
    // safepoint), and a guest waiting idle wakes for them.
    gs::init();
    gs::setDrawThreads(2);
    gs::setOwnThread(true);
    const uint64_t imr = gs::putImr(0x7F00 & ~(3ull << 8));   // SIGNAL and FINISH get through
    hw::ackIntc(hw::INTC_GS);
    hw::setIntcMask(hw::INTC_GS, true);
    clockWaitUntil(nowCycles() + kEeClockHz / 1000);   // uses up any earlier wake-up

    gsBusyWork();
    gsWrite({{0x61, 0}});   // FINISH
    const auto start = std::chrono::steady_clock::now();
    clockWaitUntil(nowCycles() + 2 * kEeClockHz);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    CHECK(gs::csr() & 2);
    CHECK(hw::intcPending() & (1u << hw::INTC_GS));

    hw::ackIntc(hw::INTC_GS);
    gs::privWrite(0x12001000, 0x1F);   // CSR: clear the events
    gs::privWrite(0x12001080, 0);      // SIGLBLID
    gsWrite({{0x60, 0x1234 | 0xFFFFFFFFull << 32}, {0x62, 0x5678 | 0xFFFFFFFFull << 32}});   // SIGNAL, LABEL
    gs::readPixel32(0, 1, 0, 0, 0);    // waits for the GS thread
    CHECK_EQ(gs::privRead(0x12001080), 0x1234 | 0x5678ull << 32);
    CHECK_EQ(gs::csr() & 3, uint64_t(1));
    CHECK(hw::intcPending() & (1u << hw::INTC_GS));

    hw::setIntcMask(hw::INTC_GS, false);
    hw::ackIntc(hw::INTC_GS);
    gs::putImr(imr);
    gs::privWrite(0x12001000, 0x1F);
    gs::setOwnThread(false);
    gs::setDrawThreads(0);
}

TEST_CASE(gs_thread_ring_wraps_round) {
    // More GIF data than the GS thread's ring holds (32 MB), in 1 MB image
    // uploads: read back one by one, then queued as fast as they go in.
    gs::init();
    gs::setDrawThreads(2);
    gs::setOwnThread(true);
    auto pixel = [](size_t i) { return gs::readPixel32(0, 8, 0x00, static_cast<int>(i % 512), static_cast<int>(i / 512)); };
    size_t differ = 0;
    for (uint32_t n = 0; n < 36; ++n) {
        const std::vector<uint32_t> image = pseudoRandom(512 * 512, n + 7);
        gsUpload(0, 8, 0x00, 512, 512, image);
        for (size_t i = n % 11; i < image.size(); i += 11) differ += pixel(i) != image[i];
    }
    const std::array<std::vector<uint32_t>, 2> images = {pseudoRandom(512 * 512, 50), pseudoRandom(512 * 512, 51)};
    const std::array<std::vector<uint64_t>, 2> packets = {uploadPackets(0, 8, 0x00, 512, 512, images[0]),
                                                          uploadPackets(0, 8, 0x00, 512, 512, images[1])};
    for (int n = 0; n < 37; ++n) gs::gifTransfer(packets[n & 1].data(), packets[n & 1].size() / 2, 3);
    for (size_t i = 0; i < images[0].size(); i += 7) differ += pixel(i) != images[0][i];
    CHECK_EQ(differ, size_t(0));
    gs::setOwnThread(false);
    gs::setDrawThreads(0);
}

TEST_CASE(gs_thread_keeps_resets_in_order) {
    // GIF_CTRL.RST and CSR.RESET drop a half-sent packet when the GS thread
    // gets to them, not when the guest writes them.
    gs::init();
    gs::setDrawThreads(2);
    gs::setOwnThread(true);
    const std::array<std::pair<uint32_t, uint64_t>, 2> resets = {{{0x10003000u, 1}, {0x12001000u, 0x200}}};
    for (const auto& [reg, value] : resets) {
        gsBusyWork();
        gsWrite({{0x47, 0}});
        const uint64_t half[4] = {2 | 1ull << 15 | 1ull << 60, 0xE, 0, 0x47};   // NLOOP 2, one A+D sent
        gs::gifTransfer(half, 2, 3);
        if (reg == 0x10003000u) gs::gifRegWrite(reg, static_cast<uint32_t>(value));
        else gs::privWrite(reg, value);
        gsRect(0, 0, 8, 8, 0x80123456u);
        CHECK_EQ(gs::readPixel32(0, 1, 0, 4, 4), 0x80123456u);
        gsRect(0, 0, 8, 8, 0);
    }
    gs::setOwnThread(false);
    gs::setDrawThreads(0);
}

namespace {

// The first pixel of a PPM file written by --dump-frames, as 0xBBGGRR.
uint32_t ppmFirstPixel(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int w = 0, h = 0, max = 0;
    in >> magic >> w >> h >> max;
    in.get();
    unsigned char rgb[3] = {};
    in.read(reinterpret_cast<char*>(rgb), 3);
    return in ? static_cast<uint32_t>(rgb[0] | rgb[1] << 8 | rgb[2] << 16) : 0xFFFFFFFFu;
}

std::vector<std::filesystem::path> filesIn(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> v;
    for (const auto& e : std::filesystem::directory_iterator(dir)) v.push_back(e.path());
    std::sort(v.begin(), v.end());
    return v;
}

} // namespace

TEST_CASE(gs_thread_shows_each_vsync_as_it_was) {
    // The GS thread draws behind the guest, but a vsync's picture is memory
    // and the display registers as they were at that vsync; the guest waits
    // when the GS thread is two vsyncs behind.
    const auto dir = std::filesystem::temp_directory_path() / "ps2n_gs_thread_frames";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    g_opt.dumpFrames = dir;
    g_opt.dumpEvery = 1;
    gs::init();
    gs::setDrawThreads(2);
    gs::setOwnThread(true);
    gs::privWrite(0x12000000, 0x6);                          // PMODE: circuit 2
    gs::privWrite(0x12000090, 1 << 9);                       // DISPFB2: page 0, 64 wide
    gs::privWrite(0x120000A0, 63ull << 32 | 7ull << 44);     // DISPLAY2: 64 clocks, 8 lines
    gsBusyWork();
    gsRect(0, 0, 64, 8, 0x800000FF);                         // red on page 0
    gs::onVblankStart();
    gsRect(0, 0, 64, 8, 0x8000FF00);                         // green on page 0, then blue on page 2, shown
    gsWrite({{0x4C, 2 | 1ull << 16}});
    gsRect(0, 0, 64, 8, 0x80FF0000);
    gs::privWrite(0x12000090, 2 | 1 << 9);
    gs::onVblankStart();
    gs::onVblankStart();
    CHECK(!filesIn(dir).empty());   // the first vsync was shown before the third was queued
    gsBusyWork();
    gsWrite({{0x4C, 2 | 1ull << 16}});
    gsRect(0, 0, 64, 8, 0x80FFFFFF);                         // white on page 2
    gs::Frame f;
    CHECK(gs::currentFrame(f));                              // once the GS thread has drawn it
    CHECK(!f.pixels.empty() && f.pixels[0] == 0xFFFFFFFFu);
    const auto files = filesIn(dir);
    CHECK_EQ(files.size(), size_t(3));
    if (files.size() == 3) {
        CHECK_EQ(ppmFirstPixel(files[0]), 0x0000FFu);
        CHECK_EQ(ppmFirstPixel(files[1]), 0xFF0000u);
        CHECK_EQ(ppmFirstPixel(files[2]), 0xFF0000u);
    }
    gs::setOwnThread(false);
    gs::setDrawThreads(0);
    g_opt.dumpFrames.clear();
    std::filesystem::remove_all(dir);
}

TEST_CASE(gs_vsync_shows_what_the_draw_threads_drew) {
    // On the guest thread too, the draw threads may still be drawing when a
    // vsync's picture is made: it waits for them.
    const auto dir = std::filesystem::temp_directory_path() / "ps2n_draw_thread_frames";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    g_opt.dumpFrames = dir;
    g_opt.dumpEvery = 1;
    gs::init();
    gs::setDrawThreads(3);
    gs::privWrite(0x12000000, 0x6);                          // PMODE: circuit 2
    gs::privWrite(0x12000090, 1 << 9);                       // DISPFB2: page 0, 64 wide
    gs::privWrite(0x120000A0, 63ull << 32 | 7ull << 44);     // DISPLAY2: 64 clocks, 8 lines
    gsWrite({{0x4C, 1ull << 16}, {0x40, 63ull << 16 | 63ull << 48}, {0x18, 0}, {0x1A, 1}, {0x47, 0}});
    for (uint32_t i = 0; i < 64; ++i) gsRect(0, 0, 64, 64, 0x80000000u | i);   // the draw threads stay busy
    gsRect(0, 0, 64, 8, 0x8000FF00);
    gs::onVblankStart();
    const auto files = filesIn(dir);
    CHECK_EQ(files.size(), size_t(1));
    if (files.size() == 1) CHECK_EQ(ppmFirstPixel(files[0]), 0x00FF00u);
    gs::setDrawThreads(0);
    g_opt.dumpFrames.clear();
    std::filesystem::remove_all(dir);
}

TEST_CASE(gs_triangles_cover_what_the_float_test_covers) {
    // Rows are narrowed to spans from exact edge functions; the pixels inside
    // must be exactly those the float weights and the edge rule select over
    // the whole bounding box. Random triangles in 12.4 coordinates, both
    // windings, from slivers to ones reaching 4096 pixels (where the float
    // products round), seen through a 512x512 scissor.
    gs::init();
    gsWrite({{0x4C, 8ull << 16}, {0x40, 511ull << 16 | 511ull << 48}, {0x18, 0}, {0x1A, 1}, {0x47, 0}});
    uint32_t seed = 12345;
    auto next = [&](uint32_t range) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) % range;
    };
    struct V {
        float x, y;
    };
    int drawn = 0;
    for (int t = 0; t < 60; ++t) {
        const uint32_t span = (t < 20 ? 64 : t < 40 ? 512 : 4096) * 16;   // in sixteenths of a pixel
        uint32_t X[3], Y[3];
        for (int i = 0; i < 3; ++i) {
            X[i] = next(span);
            Y[i] = next(span);
        }
        if (t % 7 == 0) Y[2] = Y[1] + next(3);   // a near-horizontal edge
        gsWrite({{0x00, 6}, {0x01, 0}, {0x05, xyz(0, 0, 0)}, {0x05, xyz(512, 512, 0)}});
        gsWrite({{0x00, 3}, {0x01, 0x80FFFFFF},
                 {0x05, X[0] | static_cast<uint64_t>(Y[0]) << 16}, {0x05, X[1] | static_cast<uint64_t>(Y[1]) << 16},
                 {0x05, X[2] | static_cast<uint64_t>(Y[2]) << 16}});
        const V v0{X[0] / 16.0f, Y[0] / 16.0f}, v1{X[1] / 16.0f, Y[1] / 16.0f}, v2{X[2] / 16.0f, Y[2] / 16.0f};
        const float area = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
        const int minX = std::max(static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))), 0);
        const int maxX = std::min(static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))), 511);
        const int minY = std::max(static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))), 0);
        const int maxY = std::min(static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))), 511);
        size_t mismatches = 0;
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                bool covered = false;
                if (area != 0) {
                    const float px = static_cast<float>(x), py = static_cast<float>(y);
                    const float w0 = ((v1.x - px) * (v2.y - py) - (v2.x - px) * (v1.y - py)) / area;
                    const float w1 = ((v2.x - px) * (v0.y - py) - (v0.x - px) * (v2.y - py)) / area;
                    const float w2 = 1.0f - w0 - w1;
                    covered = w0 >= 0 && w1 >= 0 && w2 >= 0 &&
                              !((w0 == 0 && w1 + w2 == 1 && py == std::max(v1.y, v2.y)) ||
                                (px == maxX && (w0 == 0 || w1 == 0 || w2 == 0) && maxX != minX));
                }
                drawn += covered;
                mismatches += covered != (gs::readPixel32(0, 8, 0x00, x, y) != 0);
            }
        }
        CHECK_EQ(mismatches, size_t(0));
    }
    CHECK(drawn > 100000);
}

namespace {

// An MPEG bitstream written code by code ("0101 1", spaces ignored).
struct Bits {
    std::vector<uint8_t> bytes;
    int used = 8;   // bits used in the last byte
    Bits& operator<<(const char* code) {
        for (const char* p = code; *p; ++p) {
            if (*p == ' ') continue;
            if (used == 8) {
                bytes.push_back(0);
                used = 0;
            }
            if (*p == '1') bytes.back() = static_cast<uint8_t>(bytes.back() | (0x80 >> used));
            ++used;
        }
        return *this;
    }
    // Stores the stream zero-padded to whole qwords; returns how many.
    uint32_t store(uint32_t addr) const {
        const uint32_t qwc = static_cast<uint32_t>((bytes.size() + 15) / 16);
        fillBytes(addr, 0, qwc * 16);
        writeBytes(addr, bytes.data(), static_cast<uint32_t>(bytes.size()));
        return qwc;
    }
};

constexpr uint32_t kIpuCmd = 0x10002000, kIpuCtrl = 0x10002010, kIpuBp = 0x10002020, kIpuTop = 0x10002030;
constexpr uint32_t kIpuInFifo = 0x10007010;
constexpr uint32_t kD3 = 0x1000B000, kD4 = 0x1000B400, kDmaStat = 0x1000E010;

void resetIpu() {
    dmac::init();
    ipu::init();
    write32(kIpuCtrl, 0x40000000);   // RST
}

// A normal-mode transfer: toIPU reads memory, fromIPU writes it.
void startDma(uint32_t channel, uint32_t madr, uint32_t qwc) {
    write32(channel + 0x10, madr);
    write32(channel + 0x20, qwc);
    write32(channel, channel == kD4 ? 0x101u : 0x100u);
}

double idctCos(int a, int b) {
    static const std::array<double, 64> table = [] {
        std::array<double, 64> t{};
        for (int i = 0; i < 64; ++i) t[static_cast<size_t>(i)] = std::cos((2 * (i / 8) + 1) * (i % 8) * std::numbers::pi / 16);
        return t;
    }();
    return table[static_cast<size_t>(a * 8 + b)];
}

// The double-precision inverse DCT IEEE 1180 measures against, rounded and saturated.
void referenceIdct(const int16_t in[64], int16_t out[64]) {
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            double s = 0;
            for (int v = 0; v < 8; ++v)
                for (int u = 0; u < 8; ++u)
                    s += (u ? 1.0 : std::sqrt(0.5)) * (v ? 1.0 : std::sqrt(0.5)) * in[v * 8 + u] * idctCos(x, u) * idctCos(y, v);
            out[y * 8 + x] = static_cast<int16_t>(std::clamp(std::floor(s / 4 + 0.5), -256.0, 255.0));
        }
}

// What BDEC should output (RAW16) for six blocks of coefficients. With field
// DCT the first two luminance blocks hold the even lines, the others the odd.
std::vector<int16_t> expectedRaw16(const int16_t coefficients[6][64], bool fieldDct = false) {
    int16_t px[6][64];
    for (int b = 0; b < 6; ++b) referenceIdct(coefficients[b], px[b]);
    std::vector<int16_t> mb(384);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const int block = (fieldDct ? y % 2 : y / 8) * 2 + x / 8;
            const int row = fieldDct ? y / 2 : y % 8;
            mb[static_cast<size_t>(y * 16 + x)] = px[block][row * 8 + x % 8];
        }
    for (int i = 0; i < 64; ++i) {
        mb[static_cast<size_t>(256 + i)] = px[4][i];
        mb[static_cast<size_t>(320 + i)] = px[5][i];
    }
    return mb;
}

std::vector<int16_t> readRaw16(uint32_t addr) {
    std::vector<int16_t> mb(384);
    readBytes(addr, mb.data(), 768);
    return mb;
}

// "" when equal, else the first difference (index: got/expected).
std::string firstDifference(const std::vector<int16_t>& got, const std::vector<int16_t>& want) {
    for (size_t i = 0; i < got.size() && i < want.size(); ++i)
        if (got[i] != want[i]) return std::to_string(i) + ": " + std::to_string(got[i]) + "/" + std::to_string(want[i]);
    return got.size() == want.size() ? "" : "sizes differ";
}

} // namespace

TEST_CASE(ipu_vlc_commands_and_fifo_registers) {
    resetIpu();
    CHECK_EQ(read32(kIpuCtrl), 0u);
    Bits b;
    b << "1" << "0000 0001 000" << "00010" << "0011" << "11"   // increment 1, escape, P macroblock_type, motion -2, dmvector -1
      << "1101 1110 1010 1101 1011 1110 1110 1111";           // 0xDEADBEEF
    b.store(0x01100000);
    uint64_t q[2];
    read128(0x01100000, q);
    write128(kIpuInFifo, q);
    CHECK_EQ(read32(kIpuBp), 0x10000u);   // FP 1, IFC 0, BP 0

    write32(kIpuCmd, 0x30000000);         // VDEC macroblock_address_increment
    CHECK_EQ(read32(kIpuCmd), 0x00010001u);   // code length 1, value 1
    CHECK_EQ(read32(kIpuCmd + 4), 0u);        // not busy
    write32(kIpuCmd, 0x30000000);
    CHECK_EQ(read32(kIpuCmd), 0x000B0023u);   // macroblock_escape
    write32(kIpuCtrl, 2u << 24);              // P picture
    write32(kIpuCmd, 0x34000000);             // VDEC macroblock_type
    CHECK_EQ(read32(kIpuCmd), 0x0005001Au);   // quant, forward motion, pattern
    write32(kIpuCmd, 0x38000000);             // VDEC motion_code
    CHECK_EQ(read32(kIpuCmd), 0x0004FFFEu);
    write32(kIpuCmd, 0x3C000000);             // VDEC dmvector
    CHECK_EQ(read32(kIpuCmd), 0x0002FFFFu);
    CHECK_EQ(read32(kIpuBp), 0x10000u | 23u);
    CHECK_EQ(read32(kIpuTop), 0xDEADBEEFu);
    CHECK_EQ(read32(kIpuTop + 4), 0u);        // 32 bits ready
    CHECK((read32(0x1000F000) >> 8) & 1);     // each command raises the IPU interrupt

    write32(kIpuCmd, 0x40000008);             // FDEC after skipping 8 bits: shows, does not consume
    CHECK_EQ(read32(kIpuCmd), 0xADBEEF00u);
    CHECK_EQ(read32(kIpuBp) & 0x7F, 31u);
    write32(kIpuCmd, 0x40000018);             // past the rest of 0xDEADBEEF
    CHECK_EQ(read32(kIpuCmd), 0u);
    write32(kIpuCmd, 0x30000000);             // eleven zeros are no address increment: ECD
    CHECK_EQ(read32(kIpuCtrl) & 0x80004000u, 0x4000u);

    // A command that runs out of data waits; BP shows how far it got.
    write32(kIpuCmd, 0x4000003F);             // skip 63 bits to 118, then FDEC needs 32
    CHECK_EQ(read32(kIpuCtrl) >> 31, 1u);
    CHECK_EQ(read32(kIpuTop + 4), 0x80000000u);
    CHECK_EQ(read32(kIpuBp), 0x10000u | 118u);
    const uint64_t more[2] = {0xFFFFFFFFull, 0};
    write128(kIpuInFifo, more);
    CHECK_EQ(read32(kIpuCtrl) >> 31, 0u);
    CHECK_EQ(read32(kIpuCmd), 0x003FFFFFu);   // 10 zero bits, then the next qword's ones
    CHECK_EQ(read32(kIpuBp), 0x20000u | 118u);

    write32(kIpuCmd, 0x00000045);             // BCLR: empty FIFO, BP 69
    CHECK_EQ(read32(kIpuBp), 69u);
}

TEST_CASE(ipu_bdec_decodes_macroblocks_through_dma) {
    resetIpu();
    write32(kIpuCtrl, 1u << 24);   // I picture, MPEG-2, zigzag, 8-bit DC, linear quantiser scale
    Bits b;
    // Intra macroblock: each block a DC difference, the second block one AC coefficient too.
    b << "101 111" << "10"         // Y0: size 3, +7 -> 135
      << "01 00" << "111" << "10"  // Y1: size 2, -3 -> 132; run 0 level -1
      << "100" << "10" << "100" << "10"   // Y2, Y3: no difference
      << "01 0" << "10"            // Cb: chroma size 1, -1 -> 127
      << "00" << "10";             // Cr: 128
    // Non-intra macroblock: coded_block_pattern 32 (first block only), "1s" first coefficient.
    b << "1010" << "11" << "10";
    // Intra again, for alternate scan and field DCT: Y0 128 with run 0 level +1, Y1 135, Y2 and Y3 132.
    b << "100 110 10" << "101 111 10" << "01 00 10" << "100 10" << "00 10" << "00 10";
    const uint32_t qwc = b.store(0x01100000);
    startDma(kD4, 0x01100000, qwc);
    CHECK((read32(kDmaStat) >> 4) & 1);       // all of it fits in the FIFO: the transfer is done

    write32(kIpuCmd, 0x2C080000);             // BDEC intra, DC reset, quantiser_scale_code 8
    CHECK_EQ(read32(kIpuCtrl) & 0x800000F0u, 0x80000080u);   // busy with a full output FIFO
    startDma(kD3, 0x01200000, 48);
    CHECK_EQ(read32(kIpuCtrl) >> 31, 0u);
    CHECK((read32(kDmaStat) >> 3) & 1);
    int16_t intra[6][64] = {};
    const int16_t dc[6] = {135, 132, 132, 132, 127, 128};
    for (int i = 0; i < 6; ++i) {
        intra[i][0] = static_cast<int16_t>(dc[i] * 8);
        intra[i][63] = 1;   // mismatch control: every block's sum is even
    }
    intra[1][1] = -16;      // 2 * -1 * 16 (default intra matrix) * 16 (scale) / 32
    CHECK_EQ(firstDifference(readRaw16(0x01200000), expectedRaw16(intra)), "");

    write32(kIpuCmd, 0x20080000);             // BDEC non-intra
    startDma(kD3, 0x01300000, 48);
    CHECK_EQ((read32(kIpuCtrl) >> 8) & 0x3F, 32u);   // CBP
    CHECK_EQ((read32(kIpuCtrl) >> 15) & 1, 0u);       // a macroblock follows, not a start code
    int16_t inter[6][64] = {};
    inter[0][0] = -24;      // (2 * -1 - 1) * 16 * 16 / 32
    inter[0][63] = 1;
    CHECK_EQ(firstDifference(readRaw16(0x01300000), expectedRaw16(inter)), "");

    write32(kIpuCtrl, 1u << 24 | 1u << 20);   // alternate scan
    write32(kIpuCmd, 0x2E080000);             // BDEC intra, DC reset, field DCT
    startDma(kD3, 0x01400000, 48);
    CHECK((read32(kIpuCtrl) >> 15) & 1);      // SCD: zero bits follow
    int16_t field[6][64] = {};
    const int16_t fieldDc[6] = {128, 135, 132, 132, 128, 128};
    for (int i = 0; i < 6; ++i) {
        field[i][0] = static_cast<int16_t>(fieldDc[i] * 8);
        field[i][63] = 1;
    }
    field[0][8] = 16;       // the second coefficient in alternate scan is the second row's first
    CHECK_EQ(firstDifference(readRaw16(0x01400000), expectedRaw16(field, true)), "");
}

TEST_CASE(ipu_csc_converts_with_alpha_thresholds) {
    resetIpu();
    uint8_t raw8[384];
    std::fill_n(raw8, 128, uint8_t{16});         // black above
    std::fill_n(raw8 + 128, 128, uint8_t{235});  // white below
    std::fill_n(raw8 + 256, 128, uint8_t{128});
    writeBytes(0x01100000, raw8, sizeof raw8);
    write32(kIpuCmd, 0x90000001);   // SETTH: TH0 1, TH1 0
    startDma(kD4, 0x01100000, 24);
    write32(kIpuCmd, 0x70000001);   // CSC, one macroblock, RGB32
    startDma(kD3, 0x01200000, 64);
    CHECK_EQ(read32(0x01200000), 0u);                    // below TH0: transparent black
    CHECK_EQ(read32(0x01200000 + 255 * 4), 0x80FFFFFFu);

    write32(kIpuCmd, 0x90000000);
    startDma(kD4, 0x01100000, 24);
    write32(kIpuCmd, 0x78000001);   // RGB16
    startDma(kD3, 0x01300000, 32);
    CHECK_EQ(read16(0x01300000), 0u);
    CHECK_EQ(read16(0x01300000 + 255 * 2), 0x7FFFu);
    CHECK_EQ(read32(kIpuCtrl) >> 31, 0u);

    uint8_t red[384];   // ITU-R BT.601 red
    std::fill_n(red, 256, uint8_t{81});
    std::fill_n(red + 256, 64, uint8_t{90});
    std::fill_n(red + 320, 64, uint8_t{240});
    uint32_t px[256];
    mpeg::yuvToRgb32(red, px, 0, 0);
    CHECK_EQ(px[0], 0x800000FEu);
}

TEST_CASE(ipu_resumes_split_and_chained_streams) {
    // One intra macroblock whose first block holds 62 escape-coded coefficients
    // (12 qwords, more than the FIFOs hold), decoded from one transfer, two
    // transfers, a DMA chain, and with the DMAC held. Every way gives the same output.
    Bits b;
    b << "100";
    for (int i = 1; i <= 62; ++i) b << (i % 2 ? "000001 000000 000000000011" : "000001 000000 111111111101");   // run 0, level +3 / -3
    b << "10" << "100 10" << "100 10" << "100 10" << "00 10" << "00 10";
    const uint32_t qwc = b.store(0x01100000);
    CHECK(qwc > 10);

    resetIpu();
    write32(kIpuCtrl, 1u << 24);
    startDma(kD4, 0x01100000, qwc);
    write32(kIpuCmd, 0x2C080000);
    startDma(kD3, 0x01200000, 48);
    const std::vector<int16_t> whole = readRaw16(0x01200000);
    int16_t coefficients[6][64] = {};
    int sum = 1024;
    for (int i = 1; i <= 62; ++i) {   // 2 * +-3 * W * 16 / 32 along the zigzag
        const int at = mpeg::kZigzag[i];
        coefficients[0][at] = static_cast<int16_t>((i % 2 ? 3 : -3) * mpeg::kDefaultIntraMatrix[at]);
        sum += coefficients[0][at];
    }
    for (int i = 0; i < 6; ++i) coefficients[i][0] = 1024;
    coefficients[0][63] = static_cast<int16_t>((sum & 1) ? 0 : 1);
    for (int i = 1; i < 6; ++i) coefficients[i][63] = 1;
    CHECK_EQ(firstDifference(whole, expectedRaw16(coefficients)), "");

    resetIpu();
    write32(kIpuCtrl, 1u << 24);
    startDma(kD3, 0x01300000, 48);
    startDma(kD4, 0x01100000, 2);
    write32(kIpuCmd, 0x2C080000);
    CHECK_EQ(read32(kIpuCtrl) >> 31, 1u);              // waiting for data
    CHECK((read32(kDmaStat) >> 4) & 1);                // after taking all the DMA had
    CHECK_EQ(read32(kIpuBp) & 0xF00u, 0u);             // IFC 0: the decoder holds what is left
    startDma(kD4, 0x01100000 + 32, qwc - 2);
    CHECK_EQ(read32(kIpuCtrl) >> 31, 0u);
    CHECK_EQ(firstDifference(readRaw16(0x01300000), whole), "");

    // A chain: ref to the first two qwords, then refe to the rest.
    resetIpu();
    write32(kIpuCtrl, 1u << 24);
    const uint64_t tags[4] = {0x30000002ull | (0x01100000ull << 32), 0, (qwc - 2) | (0x01100020ull << 32), 0};
    writeBytes(0x01400000, tags, sizeof tags);
    write32(kD4 + 0x20, 0);
    write32(kD4 + 0x30, 0x01400000);
    write32(kD4, 0x105);   // chain mode
    write32(kIpuCmd, 0x2C080000);
    startDma(kD3, 0x01500000, 48);
    CHECK((read32(kDmaStat) >> 4) & 1);
    CHECK_EQ(firstDifference(readRaw16(0x01500000), whole), "");

    // Held by D_ENABLEW, a channel started then waits for the release, and one
    // already running stops moving data.
    resetIpu();
    write32(kIpuCtrl, 1u << 24);
    write32(0x1000F590, 0x11201);
    startDma(kD4, 0x01100000, qwc);
    CHECK_EQ(read32(kIpuBp), 0u);
    write32(0x1000F590, 0x1201);
    CHECK_EQ(read32(kIpuBp), 0x20800u);               // the FIFOs are full: FP 2, IFC 8
    startDma(kD3, 0x01600000, 48);
    write32(0x1000F590, 0x11201);
    write32(kIpuCmd, 0x2C080000);
    CHECK_EQ(read32(kIpuCtrl) >> 31, 1u);              // needs the last two qwords...
    CHECK_EQ(read32(kD4 + 0x20), 2u);                  // ...which the held channel keeps
    write32(0x1000F590, 0x1201);
    CHECK_EQ(read32(kIpuCtrl) >> 31, 0u);
    CHECK_EQ(firstDifference(readRaw16(0x01600000), whole), "");
}

TEST_CASE(dmac_to_scratchpad_chain) {
    dmac::init();
    write32(kDmaStat, read32(kDmaStat) & 0x3FF);   // clear status bits
    // cnt with two qwords after the tag, then end with one.
    const uint64_t list[10] = {0x10000002ull, 0, 1, 2, 3, 4, 0x70000001ull, 0, 5, 6};
    writeBytes(0x01100000, list, sizeof list);
    write32(0x1000D480, 0x100);        // SADR
    write32(0x1000D430, 0x01100000);   // TADR
    write32(0x1000D420, 0);
    write32(0x1000D400, 0x105);        // chain mode
    CHECK_EQ(read64(0x70000100), 1ull);
    CHECK_EQ(read64(0x70000118), 4ull);
    CHECK_EQ(read64(0x70000128), 6ull);   // the end tag's qword
    CHECK_EQ(read32(0x1000D480), 0x130u);
    CHECK((read32(kDmaStat) >> 9) & 1);
}

TEST_CASE(mpeg_idct_meets_ieee_1180) {
    // IEEE 1180: random pixel blocks, forward-transformed in double precision and
    // rounded, must come back within 1 of the double inverse, with small mean errors.
    uint32_t seed = 1;
    const auto random = [&](int lo, int hi) {
        seed = seed * 1103515245u + 12345u;
        return lo + static_cast<int>((seed >> 16) % static_cast<uint32_t>(hi - lo + 1));
    };
    const int ranges[3][2] = {{-256, 255}, {-5, 5}, {-300, 300}};
    for (const auto& range : ranges) {
        double squared = 0;
        int peak = 0;
        for (int n = 0; n < 2000; ++n) {
            double pixels[64];
            for (double& p : pixels) p = random(range[0], range[1]);
            int16_t f[64];
            for (int v = 0; v < 8; ++v)
                for (int u = 0; u < 8; ++u) {
                    double s = 0;
                    for (int y = 0; y < 8; ++y)
                        for (int x = 0; x < 8; ++x) s += pixels[y * 8 + x] * idctCos(x, u) * idctCos(y, v);
                    s *= (u ? 1.0 : std::sqrt(0.5)) * (v ? 1.0 : std::sqrt(0.5)) / 4;
                    f[v * 8 + u] = static_cast<int16_t>(std::clamp(std::floor(s + 0.5), -2048.0, 2047.0));
                }
            int16_t got[64], want[64];
            mpeg::idct(f, got);
            referenceIdct(f, want);
            for (int i = 0; i < 64; ++i) {
                const int e = got[i] - want[i];
                peak = std::max(peak, std::abs(e));
                squared += e * e;
            }
        }
        CHECK(peak <= 1);
        CHECK(squared / (2000 * 64) <= 0.02);
    }
}


namespace {

// The EE side of the SIF protocol, as sceSifSendCmd / sceSifCallRpc do it.
struct EeSif {
    static constexpr uint32_t kPktBuf = 0x00400000;   // EE receive buffer
    static constexpr uint32_t kOut = 0x00400100;      // outgoing packet
    static constexpr uint32_t kDesc = 0x00400200;     // DMA descriptors
    uint32_t iopCmd = 0;

    void send(uint32_t cid, const std::vector<uint32_t>& body, uint32_t extraSrc = 0, uint32_t extraDest = 0, uint32_t extraSize = 0,
              uint32_t opt = 0) {
        const uint32_t size = 16 + static_cast<uint32_t>(body.size()) * 4;
        write32(kOut + 0, size);   // psize (dsize unused)
        write32(kOut + 4, 0);
        write32(kOut + 8, cid);
        write32(kOut + 12, opt);
        for (size_t i = 0; i < body.size(); ++i) write32(kOut + 16 + 4 * static_cast<uint32_t>(i), body[i]);
        uint32_t d = kDesc;
        int n = 0;
        if (extraSize) {
            write32(d, extraSrc);
            write32(d + 4, extraDest);
            write32(d + 8, extraSize);
            write32(d + 12, 0);
            d += 16;
            ++n;
        }
        write32(d, kOut);
        write32(d + 4, iopCmd);
        write32(d + 8, size);
        write32(d + 12, 0x44);
        ++n;
        CHECK(iop::sifSetDma(kDesc, n) != 0);
    }
    // Takes the reply the IOP left in the receive buffer, then re-arms SIF0.
    std::vector<uint32_t> receive() {
        std::vector<uint32_t> p(16);
        for (uint32_t i = 0; i < 16; ++i) p[i] = read32(kPktBuf + 4 * i);
        write32(kPktBuf, 0);
        dmac::ack(5);
        iop::sifSetDChain();
        return p;
    }
};

} // namespace

TEST_CASE(iop_sif_rpc_fileio_and_loadfile) {
    reset();
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ps2n_iop_test";
    fs::create_directories(dir);
    std::ofstream(dir / "data.txt") << "hello from the host";
    g_opt.host0Dir = dir;
    dmac::init();
    dmac::setMask(5, true);
    iop::init();

    EeSif ee;
    CHECK(iop::sifGetReg(4) & 0x20000);   // SIF_STAT_CMDINIT
    ee.iopCmd = iop::sifGetReg(2);
    ee.send(0x80000002u, {EeSif::kPktBuf});         // INIT_CMD (opt 0): our receive buffer
    ee.send(0x80000002u, {0}, 0, 0, 0, 1);          // INIT_CMD (opt 1): RPC init
    auto sreg = ee.receive();
    CHECK_EQ(sreg[2], 0x80000001u);                 // SET_SREG
    CHECK_EQ(sreg[5], 1u);                          // SIF_SREG_RPCINIT = 1

    // Bind FILEIO (0x80000001).
    const uint32_t cd = 0x00400400;
    ee.send(0x80000009u, {0x1 | 4, 0x00400800, 7, cd, 0x80000001u});
    CHECK(dmac::pending() & (1u << 5));       // SIF0 interrupt raised
    auto r = ee.receive();
    CHECK_EQ(r[2], 0x80000008u);              // RPC_END
    CHECK_EQ(r[5], 0x00400800u);              // our packet address echoed
    CHECK_EQ(r[7], cd);
    CHECK_EQ(r[8], 0x80000009u);              // ending a bind
    const uint32_t sd = r[9], buf = r[10];
    CHECK(sd != 0 && buf != 0);

    // open("host0:data.txt", O_RDONLY)
    const uint32_t send = 0x00401000, recv = 0x00402000;
    fillBytes(send, 0, 260);
    write32(send, 1);
    const char* path = "host0:data.txt";
    writeBytes(send + 4, path, static_cast<uint32_t>(std::strlen(path)));
    ee.send(0x8000000Au, {4, 0x00400800, 8, cd, 0, 260, recv, 4, 1, sd}, send, buf, 260);
    r = ee.receive();
    CHECK_EQ(r[2], 0x80000008u);
    const auto fd = static_cast<int32_t>(read32(recv));
    CHECK(fd >= 3);

    // read(fd, dest, 5)
    const uint32_t dest = 0x00403000, readData = 0x00403100;
    write32(send, static_cast<uint32_t>(fd));
    write32(send + 4, dest);
    write32(send + 8, 5);
    write32(send + 12, readData);
    ee.send(0x8000000Au, {4, 0x00400800, 9, cd, 2, 16, recv, 4, 1, sd}, send, buf, 16);
    ee.receive();
    CHECK_EQ(read32(recv), 5u);
    CHECK(std::memcmp(ram + dest, "hello", 5) == 0);

    // close(fd): Windows cannot remove the directory at the end while the file is open.
    write32(send, static_cast<uint32_t>(fd));
    ee.send(0x8000000Au, {4, 0x00400800, 10, cd, 1, 4, recv, 4, 1, sd}, send, buf, 4);
    ee.receive();
    CHECK_EQ(read32(recv), 0u);

    // LOADFILE: SifLoadModule("rom0:XPADMAN") enables the pad service.
    ee.send(0x80000009u, {4, 0x00400800, 11, cd, 0x80000006u});
    r = ee.receive();
    const uint32_t lfSd = r[9], lfBuf = r[10];
    CHECK(lfSd != 0);
    fillBytes(send, 0, 512);
    const char* mod = "rom0:XPADMAN";
    writeBytes(send + 8, mod, static_cast<uint32_t>(std::strlen(mod)));
    ee.send(0x8000000Au, {4, 0x00400800, 12, cd, 0, 512, recv, 8, 1, lfSd}, send, lfBuf, 512);
    ee.receive();
    CHECK(static_cast<int32_t>(read32(recv)) > 0);
    CHECK(iop::hasServer(0x80000100u));

    fs::remove_all(dir);
}

// The EE's SIF0 handler clears a packet's size byte before it has copied the
// packet out, and re-arms the channel after. The IOP runs at the handler's
// safepoints; a packet it sends then must wait for the re-arm, or it
// overwrites the one being read (Agent Under Fire lost a "sound bank upload
// done" message that way and waited for it forever).
TEST_CASE(iop_sif_holds_packets_until_the_ee_rearms) {
    reset();
    dmac::init();
    dmac::setMask(5, true);
    iop::init();
    EeSif ee;
    ee.iopCmd = iop::sifGetReg(2);
    ee.send(0x80000002u, {EeSif::kPktBuf});   // INIT_CMD: our receive buffer
    constexpr uint32_t kIopPkt = 0x001F0000;
    auto iopSends = [&](uint32_t tag) {
        const uint32_t words[5] = {0, 0, 0, tag, tag + 1};
        iop::mem::writeBlock(kIopPkt, words, sizeof words);
        iop::sendCommandToEe(1, kIopPkt, 20, 0, 0, 0);
    };

    iopSends(0xA000);
    CHECK_EQ(read32(EeSif::kPktBuf) & 0xFF, 20u);
    CHECK_EQ(read32(EeSif::kPktBuf + 16), 0xA001u);
    dmac::ack(5);
    write32(EeSif::kPktBuf, 0);                 // the handler takes the size, then copies
    iopSends(0xB000);                           // the IOP runs in between
    CHECK_EQ(read32(EeSif::kPktBuf) & 0xFF, 0u);
    CHECK_EQ(read32(EeSif::kPktBuf + 12), 0xA000u);
    CHECK_EQ(read32(EeSif::kPktBuf + 16), 0xA001u);
    CHECK(!(dmac::pending() & (1u << 5)));
    iop::sifSetDChain();                        // copied: the channel takes the next packet
    CHECK(dmac::pending() & (1u << 5));
    CHECK_EQ(read32(EeSif::kPktBuf + 12), 0xB000u);
    CHECK_EQ(read32(EeSif::kPktBuf + 16), 0xB001u);
    ee.receive();
    CHECK(!(dmac::pending() & (1u << 5)));

    for (uint32_t i = 0; i < 8; ++i) write32(EeSif::kPktBuf + 4 * i, 0);
    iop::mem::fill(kIopPkt, 0, 20);
}

TEST_CASE(iop_reset_with_a_replacement_image) {
    reset();
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ps2n_ioprp_test";
    fs::create_directories(dir);
    // All the runtime reads from a replacement image are its modules' ids.
    std::string image(0x400, '\0');
    image.replace(0x100, 16, "PsIIfileio  2140");
    image.replace(0x200, 16, "PsIIloadfile2140");
    std::ofstream(dir / "IOPRP214.IMG", std::ios::binary) << image;
    std::ofstream(dir / "IOPRP23.IMG", std::ios::binary) << std::string(0x40, '\0');   // no ids: its name tells
    std::ofstream(dir / "data.txt") << "hello from the host";
    g_opt.host0Dir = dir;
    dmac::init();
    dmac::setMask(5, true);
    iop::init();
    CHECK_EQ(iop::moduleVersion("fileio"), 1000u);   // the BIOS's modules

    EeSif ee;
    const uint32_t cd = 0x00400400, send = 0x00401000, recv = 0x00402000;
    uint32_t rpcId = 1;
    // SifIopReset: the reset command, SMFLAG polled until the IOP is back, SIF set up again.
    auto resetIop = [&](const std::string& arg) {
        std::vector<uint32_t> body{static_cast<uint32_t>(arg.size() + 1), 0};
        std::string bytes = arg;
        bytes.resize((arg.size() + 4) & ~size_t(3), '\0');
        for (size_t i = 0; i < bytes.size(); i += 4) {
            uint32_t w;
            std::memcpy(&w, bytes.data() + i, 4);
            body.push_back(w);
        }
        ee.iopCmd = iop::sifGetReg(2);
        ee.send(0x80000003u, body);
        CHECK(iop::sifGetReg(4) & 0x40000);              // SIF_STAT_BOOTEND
        ee.send(0x80000002u, {EeSif::kPktBuf});
        ee.send(0x80000002u, {0}, 0, 0, 0, 1);
        ee.receive();                                     // SET_SREG
    };
    auto bind = [&](uint32_t sid, uint32_t& sd, uint32_t& buf) {
        ee.send(0x80000009u, {4, 0x00400800, rpcId++, cd, sid});
        const auto r = ee.receive();
        sd = r[9];
        buf = r[10];
    };
    auto call = [&](uint32_t sd, uint32_t buf, uint32_t fno, uint32_t ssize, uint32_t rsize) {
        fillBytes(recv, 0, 16);
        ee.send(0x8000000Au, {4, 0x00400800, rpcId++, cd, fno, ssize, recv, rsize, 1, sd}, send, buf, ssize);
        ee.receive();
    };

    resetIop("rom0:UDNL host0:IOPRP214.IMG");
    CHECK_EQ(iop::moduleVersion("fileio"), 2140u);
    CHECK_EQ(iop::moduleVersion("loadfile"), 2140u);
    uint32_t sd = 0, buf = 0;
    bind(0x80000001u, sd, buf);
    call(sd, buf, 255, 0, 4);                             // the version query SDK 2.x's libraries check
    CHECK(std::memcmp(ram + recv, "2140", 4) == 0);
    call(sd, buf, 255, 0, 8);
    CHECK(std::memcmp(ram + recv, "2140....", 8) == 0);

    // open in SDK 2.1's layout, {flags, mode, name}; read and close are unchanged.
    const char* path = "host0:data.txt";
    fillBytes(send, 0, 64);
    write32(send, 1);                                     // O_RDONLY
    writeBytes(send + 8, path, static_cast<uint32_t>(std::strlen(path)));
    call(sd, buf, 0, 8 + static_cast<uint32_t>(std::strlen(path)) + 1, 4);
    const auto fd = static_cast<int32_t>(read32(recv));
    CHECK(fd >= 3);
    const uint32_t dest = 0x00403000, fixup = 0x00403100;
    fillBytes(fixup, 0xEE, 16);
    write32(send, static_cast<uint32_t>(fd));
    write32(send + 4, dest);
    write32(send + 8, 5);
    write32(send + 12, fixup);
    call(sd, buf, 2, 16, 4);
    CHECK_EQ(read32(recv), 5u);
    CHECK(std::memcmp(ram + dest, "hello", 5) == 0);
    CHECK_EQ(read32(fixup), 0u);                          // the fix-up packet: no head bytes to copy
    write32(send, static_cast<uint32_t>(fd));
    call(sd, buf, 1, 4, 4);
    CHECK_EQ(read32(recv), 0u);

    uint32_t lfSd = 0, lfBuf = 0;
    bind(0x80000006u, lfSd, lfBuf);
    call(lfSd, lfBuf, 255, 0, 4);
    CHECK(std::memcmp(ram + recv, "2140", 4) == 0);

    resetIop("rom0:UDNL host0:IOPRP23.IMG");
    CHECK_EQ(iop::moduleVersion("fileio"), 2300u);
    resetIop("rom0:UDNL rom0:EELOADCNF");                 // the BIOS's modules again
    CHECK_EQ(iop::moduleVersion("fileio"), 1000u);
    fs::remove_all(dir);
}

TEST_CASE(iop_power_off_multitap_and_pad_services) {
    reset();
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ps2n_services_test";
    fs::create_directories(dir);
    // All the runtime reads from a module it replaces is its IRX header: an
    // ELF whose one program header points at the .iopmod data.
    auto writeIrx = [&](const char* file, const char* name, uint16_t version) {
        std::string irx(0x80, '\0');
        auto put = [&](size_t off, uint32_t v, size_t n) { std::memcpy(irx.data() + off, &v, n); };
        irx.replace(0, 7, "\x7f" "ELF\x01\x01\x01", 7);
        put(16, ps2n::elf::ET_SCE_IOPRELEXEC, 2);
        put(18, ps2n::elf::EM_MIPS, 2);
        put(28, 52, 4);                                   // e_phoff
        put(42, 32, 2);                                   // e_phentsize
        put(44, 1, 2);                                    // e_phnum
        put(52, ps2n::elf::PT_SCE_IOPMOD, 4);
        put(56, 84, 4);
        put(84, 0xFFFFFFFFu, 4);                          // no module info in the image
        put(84 + 24, version, 2);
        irx.replace(84 + 26, std::strlen(name), name);
        std::ofstream(dir / file, std::ios::binary) << irx;
    };
    writeIrx("MTAPMAN.IRX", "multitap_manager", 0x0302);
    writeIrx("PADMAN.IRX", "padman", 0x0402);
    g_opt.host0Dir = dir;
    dmac::init();
    dmac::setMask(5, true);
    iop::init();

    EeSif ee;
    ee.iopCmd = iop::sifGetReg(2);
    ee.send(0x80000002u, {EeSif::kPktBuf});
    ee.send(0x80000002u, {0}, 0, 0, 0, 1);
    ee.receive();                                         // SET_SREG
    const uint32_t cd = 0x00400400, send = 0x00401000, recv = 0x00402000;
    uint32_t rpcId = 1;
    auto bind = [&](uint32_t sid, uint32_t& sd, uint32_t& buf) {
        ee.send(0x80000009u, {4, 0x00400800, rpcId++, cd, sid});
        const auto r = ee.receive();
        sd = r[9];
        buf = r[10];
    };
    auto call = [&](uint32_t sd, uint32_t buf, uint32_t fno, uint32_t ssize, uint32_t rsize) {
        fillBytes(recv, 0xEE, 128);
        ee.send(0x8000000Au, {4, 0x00400800, rpcId++, cd, fno, ssize, recv, rsize, 1, sd}, send, buf, ssize);
        ee.receive();
    };

    // sceCdInit's power-off request: the server is there from start-up, and
    // the call it gets is not answered.
    uint32_t sd = 0, buf = 0;
    bind(0x80000596u, sd, buf);
    CHECK(sd != 0);
    ee.send(0x8000000Au, {4, 0x00400800, rpcId++, cd, 1, 0, 0, 0, 1, sd});
    CHECK(!(dmac::pending() & (1u << 5)));
    CHECK_EQ(read32(EeSif::kPktBuf), 0u);

    // Loading the game's MTAPMAN.IRX and PADMAN.IRX enables their services.
    uint32_t lfSd = 0, lfBuf = 0;
    bind(0x80000006u, lfSd, lfBuf);
    auto loadModule = [&](const char* path) {
        fillBytes(send, 0, 512);
        writeBytes(send + 8, path, static_cast<uint32_t>(std::strlen(path)));
        call(lfSd, lfBuf, 0, 512, 8);
        return static_cast<int32_t>(read32(recv));
    };
    CHECK(loadModule("host0:MTAPMAN.IRX") > 0);
    CHECK(loadModule("host0:PADMAN.IRX") > 0);

    // libmtap 3.0 sends 128 bytes and reads the result from the reply;
    // ps2sdk's libmtap sends {port} and reads 8 bytes.
    std::array<uint32_t, 6> sds{}, bufs{};
    for (uint32_t i = 1; i <= 5; ++i) bind(0x80000900u + i, sds[i], bufs[i]);
    fillBytes(send, 0, 128);
    call(sds[1], bufs[1], 1, 128, 128);                   // PortOpen(0)
    CHECK_EQ(read32(recv + 4), 1u);
    write32(send, 4);
    call(sds[1], bufs[1], 1, 4, 8);                       // PortOpen(4): there is no port 4
    CHECK_EQ(read32(recv), 4u);
    CHECK_EQ(read32(recv + 4), 0u);
    write32(send, 1);
    call(sds[3], bufs[3], 1, 128, 128);                   // GetConnection(1): no multitap
    CHECK_EQ(read32(recv + 4), 0u);
    call(sds[2], bufs[2], 1, 128, 128);                   // PortClose(1)
    CHECK_EQ(read32(recv + 4), 1u);
    write32(send, 20);
    write32(send + 4, 40);
    call(sds[4], bufs[4], 1, 128, 128);                   // ChangeThreadPriority(20, 40)
    CHECK_EQ(read32(recv + 8), 1u);
    write32(send + 4, 8);
    call(sds[4], bufs[4], 1, 128, 128);                   // 8 is not a priority modules may take
    CHECK_EQ(read32(recv + 8), 0u);
    call(sds[5], bufs[5], 1, 128, 128);                   // GetModVersion: the IRX's
    CHECK_EQ(read32(recv), 0x0302u);

    // libpad checks PADMAN's version too (command 0x12, the answer in word 3).
    uint32_t padSd = 0, padBuf = 0;
    bind(0x80000100u, padSd, padBuf);
    fillBytes(send, 0, 128);
    write32(send, 0x12);
    call(padSd, padBuf, 1, 128, 128);
    CHECK_EQ(read32(recv + 12), 0x0402u);

    // Port 1 opens with its data area; each vblank writes the newer of the
    // area's two 128-byte frames (the frame number is word 22).
    const uint32_t area = 0x00403000;
    fillBytes(area, 0, 256);
    auto padCommand = [&](uint32_t cmd, std::initializer_list<uint8_t> args = {}, uint32_t argOffset = 12) {
        fillBytes(send, 0, 128);
        write32(send, cmd);
        uint32_t at = send + argOffset;
        for (uint8_t b : args) writeBytes(at++, &b, 1);
        if (cmd == 0x01) write32(send + 16, area);
        call(padSd, padBuf, 1, 128, 128);
    };
    padCommand(0x01);   // open port 0, slot 0
    CHECK_EQ(read32(recv + 12), 1u);
    input::PadConfig& cfg = input::pad(0);
    const input::PadConfig saved = cfg;
    cfg = input::defaultPad(0);
    cfg.bindings[size_t(input::Ds2::Analog)] = {*input::parseBinding("Keyboard/F10")};
    cfg.motors[size_t(input::Motor::Large)] = {*input::parseBinding("SDL-0/LargeMotor")};
    cfg.motors[size_t(input::Motor::Small)] = {*input::parseBinding("SDL-0/SmallMotor")};
    auto vblank = [&](std::vector<std::string> keys) {
        input::HostState host;
        host.keys = std::move(keys);
        input::update(host, true, true);
        iop::pad::onVblank();
    };
    auto mode = [&] {
        const uint32_t newer = read32(area + 88) > read32(area + 128 + 88) ? area : area + 128;
        uint8_t id = 0;
        readBytes(newer + 1, &id, 1);
        return id;
    };
    for (int i = 0; i < 3; ++i) vblank({});
    CHECK_EQ(mode(), 0x41);              // digital, as a DualShock 2 starts
    vblank({"F10"});
    CHECK_EQ(mode(), 0x73);              // the Analog button: analog mode
    vblank({"F10"});
    CHECK_EQ(mode(), 0x73);              // holding it does not toggle again
    vblank({});
    vblank({"F10"});
    CHECK_EQ(mode(), 0x41);
    padCommand(0x06, {1, 0, 0, 0, 3});   // the game sets analog mode and locks it
    for (int i = 0; i < 3; ++i) vblank({});
    CHECK_EQ(mode(), 0x73);
    vblank({"F10"});
    CHECK_EQ(mode(), 0x73);              // locked: the button does nothing

    // Vibration: actuator 0 drives the small motor (on/off), 1 the large one.
    padCommand(0x08, {0, 1, 0xFF, 0xFF, 0xFF, 0xFF});
    padCommand(0x07, {1, 200, 0, 0, 0, 0});
    vblank({});
    auto rumble = input::rumble();
    CHECK_EQ(rumble.size(), size_t(1));
    if (rumble.size() == 1) {
        CHECK_EQ(rumble[0].device, 0);
        CHECK(std::fabs(rumble[0].large - 200.0f / 255.0f) < 1e-6f);
        CHECK_EQ(rumble[0].small, 1.0f);
    }
    padCommand(0x0E);                    // the port closes: the motors stop
    vblank({});
    rumble = input::rumble();
    CHECK(rumble.size() == 1 && rumble[0].large == 0.0f && rumble[0].small == 0.0f);
    cfg = saved;
    input::update({}, true, true);
    fs::remove_all(dir);
}

TEST_CASE(disc_iso9660_from_a_directory_and_an_image) {
    namespace disc = iop::disc;
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ps2n_disc_test";
    const fs::path iso = fs::temp_directory_path() / "ps2n_disc_test.iso";
    fs::remove_all(dir);
    fs::create_directories(dir / "data" / "sub");
    const std::string cnf = "BOOT2 = cdrom0:\\SLUS_000.00;1\r\nVER = 1.00\r\n";
    std::ofstream(dir / "SYSTEM.CNF", std::ios::binary) << cnf;
    std::string big(5000, '\0');
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>('a' + i % 26);
    std::ofstream(dir / "data" / "big.bin", std::ios::binary) << big;
    std::ofstream(dir / "data" / "sub" / "empty.txt").close();
    for (int i = 0; i < 80; ++i) std::ofstream(dir / ("file" + std::to_string(i) + ".dat")) << i;   // a two-sector root

    auto check = [&] {
        disc::Entry e;
        CHECK(disc::find("\\SYSTEM.CNF;1", e));
        CHECK_EQ(e.size, static_cast<uint32_t>(cnf.size()));
        CHECK(disc::find("data/BIG.BIN", e));
        CHECK(!e.dir);
        CHECK_EQ(e.size, 5000u);
        std::vector<uint8_t> sectors(3 * 2048, 0xFF);
        CHECK(disc::read(e.lsn, 3, sectors.data()));
        CHECK(std::memcmp(sectors.data(), big.data(), big.size()) == 0);
        CHECK_EQ(sectors[5000], 0);
        CHECK(disc::find("\\DATA\\SUB\\EMPTY.TXT;1", e));
        CHECK_EQ(e.size, 0u);
        CHECK(disc::find("\\data\\sub", e));
        CHECK(e.dir);
        CHECK(!disc::find("\\DATA\\MISSING.BIN", e));
        std::vector<disc::Entry> root;
        CHECK(disc::list("\\", root));
        CHECK_EQ(root.size(), 82u);
        CHECK(disc::find("\\FILE79.DAT;1", e));
        CHECK(disc::read(e.lsn, 1, sectors.data()));
        CHECK(std::memcmp(sectors.data(), "79", 2) == 0);
    };
    CHECK(disc::insert(dir));
    CHECK(disc::type() == disc::Type::Dvd);
    check();

    const std::vector<uint8_t> image = disc::buildImage(dir);
    std::ofstream(iso, std::ios::binary).write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    CHECK(disc::insert(iso));
    CHECK_EQ(disc::sectorCount(), static_cast<uint32_t>(image.size() / 2048));
    check();

    // cdrom0: files on the image, through the file layer FILEIO and ioman share.
    g_opt.cdrom = iso;
    const int fd = iop::hostfs::open("cdrom0:\\DATA\\BIG.BIN;1", iop::hostfs::O_RDONLY);
    CHECK(fd >= 0);
    char buf[16] = {};
    CHECK_EQ(iop::hostfs::lseek(fd, 2050, 0), 2050);
    CHECK_EQ(iop::hostfs::read(fd, buf, 4), 4);
    CHECK(std::memcmp(buf, big.data() + 2050, 4) == 0);
    CHECK_EQ(iop::hostfs::lseek(fd, -2, 2), 4998);
    CHECK_EQ(iop::hostfs::read(fd, buf, 16), 2);
    CHECK_EQ(iop::hostfs::write(fd, buf, 1), iop::hostfs::E_ACCES);
    iop::hostfs::close(fd);
    std::vector<uint8_t> whole;
    CHECK(iop::hostfs::readWholeFile("cdrom0:\\SYSTEM.CNF;1", whole));
    CHECK(std::string(whole.begin(), whole.end()) == cnf);
    iop::hostfs::Stat st;
    CHECK_EQ(iop::hostfs::getstat("cdrom0:\\DATA", st), 0);
    CHECK(st.dir);
    const int dfd = iop::hostfs::dopen("cdrom0:\\DATA");
    std::string name;
    CHECK_EQ(iop::hostfs::dread(dfd, name, st), 1);
    CHECK(name == "BIG.BIN");
    CHECK_EQ(st.size, 5000u);
    iop::hostfs::close(dfd);

    // The BIOS ROMVER file, with the region from --region.
    g_opt.region = 'E';
    CHECK(iop::hostfs::readWholeFile("rom0:ROMVER", whole));
    CHECK_EQ(whole.size(), 16u);
    CHECK(std::memcmp(whole.data(), "0160EC20010704\n", 15) == 0);
    g_opt.region = 'A';

    g_opt.cdrom.clear();
    disc::eject();
    fs::remove_all(dir);
    fs::remove(iso);
}

namespace {
void noop() {}
uint32_t checksumOf(const uint32_t* words, size_t count) {
    ps2n::CodeChecksum sum;
    for (size_t i = 0; i < count; ++i) sum.add(words[i]);
    return sum.value;
}
void writeWords(uint32_t addr, const uint32_t* words, size_t count) {
    for (size_t i = 0; i < count; ++i) write32(addr + 4 * static_cast<uint32_t>(i), words[i]);
}
} // namespace

TEST_CASE(dispatch_checks_programs_against_ram) {
    // Two programs the game may load by itself at the same address, two functions each.
    static const uint32_t kX[] = {0x24020001u, 0x03e00008u, 0x24030002u, 0x03e00008u};
    static const uint32_t kY[] = {0x24020005u, 0x03e00008u, 0x24030006u, 0x03e00008u};
    static const FunctionInfo bootFns[] = {{0x00100000u, 0x00100008u, &noop, "boot_main", false, 0}};
    static const FunctionInfo xFns[] = {{0x00200000u, 0x00200008u, &noop, "x_a", false, checksumOf(kX, 2)},
                                        {0x00200008u, 0x00200010u, &noop, "x_b", false, checksumOf(kX + 2, 2)}};
    static const FunctionInfo yFns[] = {{0x00200000u, 0x00200008u, &noop, "y_a", false, checksumOf(kY, 2)},
                                        {0x00200008u, 0x00200010u, &noop, "y_b", false, checksumOf(kY + 2, 2)}};
    static const GameInfo x = {"x", "X.ELF", 0, 0x00200000u, xFns, 2, nullptr, 0, "test", nullptr, 0, nullptr, 0};
    static const GameInfo y = {"y", "Y.ELF", 0, 0x00200000u, yFns, 2, nullptr, 0, "test", nullptr, 0, nullptr, 0};
    static const GameInfo* const others[] = {&x, &y};
    static const GameInfo boot = {"boot", "BOOT.ELF", 0, 0x00100000u, bootFns, 1, nullptr, 0, "test", nullptr, 0, others, 2};
    initDispatch(boot);
    CHECK(hasCompiledEntry(0x00100000u));                // loaded by the runtime: no check needed

    // Half of each program: neither is in RAM as a whole, so neither runs.
    writeWords(0x00200000u, kX, 2);
    writeWords(0x00200008u, kY + 2, 2);
    invalidateCompiledCode();
    CHECK(!hasCompiledEntry(0x00200000u));
    CHECK(!hasCompiledEntry(0x00200008u));

    writeWords(0x00200000u, kX, 4);                      // the game loads X and jumps into it
    invalidateCompiledCode();
    CHECK(hasCompiledEntry(0x00200000u));
    CHECK(findFunction(0x00200008u) == &xFns[1]);        // X is the running program now

    writeWords(0x00200000u, kY, 4);                      // then Y over it
    invalidateCompiledCode();
    CHECK(hasCompiledEntry(0x00200000u));
    CHECK(findFunction(0x00200000u) == &yFns[0]);

    write32(0x0020000Cu, 0);                             // Y rewrites its second function
    invalidateCompiledCode();
    CHECK(hasCompiledEntry(0x00200000u));                // the first is still Y's
    CHECK(!hasCompiledEntry(0x00200008u));               // the second runs on the interpreter
    CHECK(hasCompiledEntry(0x00100000u));                // no checksum: always trusted
}

TEST_CASE(strict_mode_stops_at_gaps_not_fallbacks) {
    reset();
    g_opt.strict = true;
    cpu.pc = 0x00100000;
    PS2_FALLBACK("test", "test fallback", "a handled path");
    CHECK(!g_exitRequested);
    CHECK_EQ(cpu.pc, 0x00100000u);

    PS2_UNIMPL("test", "test gap", "an unimplemented path");
    CHECK(g_exitRequested);
    CHECK_EQ(g_exitCode, 4);
    CHECK_EQ(cpu.pc, kExitPc);
    CHECK(!safepoint(0x00100000));   // the next back-edge unwinds...
    CHECK_EQ(cpu.pc, kExitPc);
    cpu.pc = 0x00100008;             // ...even after guest code returned through $ra
    syscall(0x00100004);
    CHECK_EQ(cpu.pc, kExitPc);

    g_opt.strict = false;
    g_exitRequested = false;
    g_exitCode = 0;
}

TEST_CASE(settings_ini_keeps_repeated_keys_and_order) {
    settings::Ini ini;
    ini.parse("; comment\n[Pad1]\nType = DualShock2\nCross = Keyboard/K\n# another\nCross = SDL-0/FaceSouth\n"
              "AxisScale = 1.5\n[Empty]\n[UI]\nflag = yes\n");
    CHECK(ini.hasSection("pad1"));   // names ignore case
    const auto cross = ini.values("Pad1", "cross");
    CHECK_EQ(cross.size(), size_t(2));
    CHECK(cross.size() == 2 && cross[0] == "Keyboard/K" && cross[1] == "SDL-0/FaceSouth");
    CHECK_EQ(ini.getFloat("Pad1", "AxisScale", 0.0f), 1.5f);
    CHECK_EQ(ini.getFloat("Pad1", "Missing", 2.0f), 2.0f);
    CHECK(ini.getBool("UI", "flag", false));
    // Replacing a repeated key keeps its place in the section.
    ini.setValues("Pad1", "Cross", {"Keyboard/X"});
    CHECK_EQ(ini.text(), std::string("[Pad1]\nType = DualShock2\nCross = Keyboard/X\nAxisScale = 1.5\n\n[Empty]\n\n[UI]\nflag = yes\n"));
    ini.setValues("Pad1", "Cross", {});
    CHECK(ini.values("Pad1", "Cross").empty());
    CHECK(ini.hasSection("Empty"));   // an emptied section stays (no [Hotkeys] means the defaults)
}

TEST_CASE(input_bindings_parse_pcsx2_strings) {
    using input::HostKey;
    auto round = [](const char* text) {
        const auto b = input::parseBinding(text);
        return b ? input::formatBinding(*b) : std::string("(invalid)");
    };
    CHECK_EQ(round("Keyboard/K"), std::string("Keyboard/K"));
    CHECK_EQ(round("SDL-0/FaceSouth"), std::string("SDL-0/FaceSouth"));
    CHECK_EQ(round("SDL-1/A"), std::string("SDL-1/FaceSouth"));   // PCSX2's SDL 2 era name
    CHECK_EQ(round("SDL-1/Y"), std::string("SDL-1/FaceNorth"));
    CHECK_EQ(round("SDL-0/+LeftX"), std::string("SDL-0/+LeftX"));
    CHECK_EQ(round("SDL-0/-LeftY"), std::string("SDL-0/-LeftY"));
    CHECK_EQ(round("SDL-0/FullLeftTrigger"), std::string("SDL-0/FullLeftTrigger"));
    CHECK_EQ(round("SDL-0/+RightTrigger~"), std::string("SDL-0/+RightTrigger~"));
    CHECK_EQ(round("SDL-2/LargeMotor"), std::string("SDL-2/LargeMotor"));
    CHECK_EQ(round("Keyboard/Alt & Keyboard/Return"), std::string("Keyboard/Alt & Keyboard/Return"));
    CHECK_EQ(round("Keyboard/Shift&SDL-0/Start"), std::string("Keyboard/Shift & SDL-0/Start"));
    // Inputs this runtime does not read are kept as written.
    CHECK_EQ(round("XInput-0/A"), std::string("XInput-0/A"));
    CHECK_EQ(round("SDL-0/JoyButton3"), std::string("SDL-0/JoyButton3"));
    CHECK_EQ(round("Pointer-0/LeftButton"), std::string("Pointer-0/LeftButton"));
    CHECK_EQ(round("Keyboard"), std::string("(invalid)"));
    CHECK_EQ(round(""), std::string("(invalid)"));
    CHECK_EQ(round("Keyboard/A & Keyboard/B & Keyboard/C & Keyboard/D & Keyboard/E"), std::string("(invalid)"));
    const auto axis = input::parseBinding("SDL-3/-RightY");
    CHECK(axis && axis->keys.size() == 1 && axis->keys[0].kind == HostKey::Kind::Axis && axis->keys[0].device == 3 &&
          axis->keys[0].index == int(input::PadAxis::RightY) && axis->keys[0].range == HostKey::Range::Negative);
    CHECK_EQ(input::displayBinding(*input::parseBinding("Keyboard/Alt & Keyboard/Return")), std::string("Alt + Return"));
    CHECK_EQ(input::displayBinding(*input::parseBinding("SDL-0/FaceSouth")), std::string("SDL-0 A"));
}

TEST_CASE(input_ds2_reads_like_pcsx2) {
    using input::Ds2;
    auto keys = [](std::vector<std::string> k) {
        input::HostState s;
        s.keys = std::move(k);
        return s;
    };
    input::PadConfig cfg = input::defaultPad(0);   // PCSX2's keyboard layout
    input::PadState st = input::evaluate(cfg, keys({}));
    CHECK(st.connected);
    CHECK_EQ(st.buttons, 0);
    CHECK_EQ(st.lx, 0x80);
    st = input::evaluate(cfg, keys({"K", "Return", "Up"}));
    CHECK_EQ(st.buttons, 0x4000 | 0x0008 | 0x0010);   // Cross, Start, Up
    CHECK_EQ(st.pressure[6], 255);                     // Cross, fully pressed
    CHECK_EQ(st.pressure[2], 255);                     // Up
    st = input::evaluate(cfg, keys({"W", "D"}));       // left stick up and right: a key is full tilt
    CHECK_EQ(st.ly, 0);
    CHECK_EQ(st.lx, 255);
    CHECK_EQ(input::evaluate(input::defaultPad(1), keys({"K"})).connected, false);   // port 2: no controller

    // Apply Pressure scales the buttons it holds, but not L2/R2.
    cfg.bindings[size_t(Ds2::Pressure)] = {*input::parseBinding("Keyboard/Shift")};
    st = input::evaluate(cfg, keys({"K", "1", "Shift"}));
    CHECK_EQ(st.pressure[6], 128);    // Cross at 50%
    CHECK_EQ(st.pressure[10], 255);   // L2 untouched
    cfg.scale[size_t(Ds2::Cross)] = 0.25f;   // the input's own sensitivity
    CHECK_EQ(input::evaluate(cfg, keys({"K"})).pressure[6], 64);

    // A controller, mapped automatically: triggers give proportional L2/R2,
    // sticks scale by 133% and a deadzone centres them.
    input::PadConfig pad;
    input::mapController(pad, 0);
    CHECK(pad.bindings[size_t(Ds2::Pressure)].empty());
    CHECK_EQ(input::formatBinding(pad.bindings[size_t(Ds2::Cross)][0]), std::string("SDL-0/FaceSouth"));
    CHECK_EQ(input::formatBinding(pad.motors[size_t(input::Motor::Large)][0]), std::string("SDL-0/LargeMotor"));
    input::HostState host;
    host.controllers.push_back({});
    auto& c = host.controllers[0];
    c.axes[size_t(input::PadAxis::LeftTrigger)] = 0.5f;
    c.axes[size_t(input::PadAxis::LeftX)] = 0.5f;
    c.buttons[size_t(input::PadButton::FaceEast)] = true;
    st = input::evaluate(pad, host);
    CHECK_EQ(st.buttons, 0x0100 | 0x2000);   // L2 and Circle
    CHECK_EQ(st.pressure[10], 128);
    CHECK_EQ(st.lx, 212);                    // 0.5 * 1.33 of the way right
    pad.tuning.deadzone = 0.7f;
    CHECK_EQ(input::evaluate(pad, host).lx, 0x80);   // 0.665 is inside a 0.7 radius
    pad.tuning.deadzone = 0.0f;
    pad.tuning.invertL = 1;
    CHECK_EQ(input::evaluate(pad, host).lx, 43);
    pad.tuning.invertL = 0;
    pad.tuning.buttonDeadzone = 0.6f;
    CHECK_EQ(input::evaluate(pad, host).buttons & 0x0100, 0);   // the trigger at 0.5 no longer presses L2
    host.controllers[0].device = 1;
    CHECK_EQ(input::evaluate(pad, host).buttons, 0);           // another controller's input
}

TEST_CASE(input_settings_carry_pcsx2_sections) {
    using input::Ds2;
    settings::Ini ini;
    ini.parse("[Pad1]\nType = DualShock2\nCross = Keyboard/K\nCross = SDL-0/FaceSouth\nLUp = SDL-0/-LeftY\n"
              "Deadzone = 0.1\nAxisScale = 1.5\nCrossScale = 0.8\nLargeMotor = SDL-0/LargeMotor\n"
              "[Pad2]\nType = None\n[Hotkeys]\nToggleFullscreen = Keyboard/Alt & Keyboard/Return\n[UI]\nKeep = 1\n");
    const input::PadConfig p1 = input::loadPad(ini, 0), p2 = input::loadPad(ini, 1);
    CHECK(p1.connected);
    CHECK_EQ(p1.bindings[size_t(Ds2::Cross)].size(), size_t(2));
    CHECK_EQ(p1.bindings[size_t(Ds2::Start)].size(), size_t(0));   // a section replaces the defaults
    CHECK_EQ(p1.tuning.deadzone, 0.1f);
    CHECK_EQ(p1.tuning.axisScale, 1.5f);
    CHECK_EQ(p1.scale[size_t(Ds2::Cross)], 0.8f);
    CHECK_EQ(p1.motors[size_t(input::Motor::Large)].size(), size_t(1));
    CHECK(!p2.connected);
    const input::HotkeyConfig hk = input::loadHotkeys(ini);
    CHECK_EQ(hk.bindings[size_t(input::Hotkey::ToggleFullscreen)].size(), size_t(1));
    CHECK(hk.bindings[size_t(input::Hotkey::OpenPauseMenu)].empty());   // the section says nothing: unbound

    // Written back and read again, nothing changes, and other sections stay.
    input::savePad(ini, 0, p1);
    input::savePad(ini, 1, p2);
    input::saveHotkeys(ini, hk);
    settings::Ini again;
    again.parse(ini.text());
    const input::PadConfig q1 = input::loadPad(again, 0);
    CHECK(q1.bindings == p1.bindings && q1.motors == p1.motors && q1.scale == p1.scale);
    CHECK_EQ(q1.tuning.axisScale, 1.5f);
    CHECK(!input::loadPad(again, 1).connected);
    CHECK(input::loadHotkeys(again).bindings == hk.bindings);
    CHECK_EQ(again.getString("UI", "Keep", ""), std::string("1"));
    CHECK_EQ(again.value("Pad1", "LDownScale").has_value(), false);   // defaults are not written

    // No sections: PCSX2's defaults.
    const settings::Ini empty;
    CHECK_EQ(input::formatBinding(input::loadPad(empty, 0).bindings[size_t(Ds2::Triangle)][0]), std::string("Keyboard/I"));
    CHECK(!input::loadPad(empty, 1).connected);
    CHECK_EQ(input::formatBinding(input::loadHotkeys(empty).bindings[size_t(input::Hotkey::OpenPauseMenu)][0]),
             std::string("Keyboard/Escape"));
}

TEST_CASE(input_hotkey_chords_take_their_keys) {
    using input::Hotkey;
    const input::PadConfig savedPad = input::pad(0);
    const input::HotkeyConfig savedKeys = input::hotkeys();
    input::pad(0) = input::defaultPad(0);
    input::hotkeys() = input::defaultHotkeys();
    auto step = [](std::vector<std::string> keys, bool toGame = true, bool hotkeysOn = true) {
        input::HostState s;
        s.keys = std::move(keys);
        return input::update(s, toGame, hotkeysOn);
    };
    auto start = [] { return (input::padState(0).buttons & 0x0008) != 0; };
    CHECK(step({"Return"}).empty());
    CHECK(start());
    auto ev = step({"Return", "Alt"});   // Alt+Return: fullscreen, and Return no longer reaches Start
    CHECK(ev.size() == 1 && ev[0].hotkey == Hotkey::ToggleFullscreen && ev[0].pressed);
    CHECK(!start());
    ev = step({"Alt"});
    CHECK(ev.size() == 1 && ev[0].hotkey == Hotkey::ToggleFullscreen && !ev[0].pressed);
    CHECK(step({"Alt", "Return"}).size() == 1);
    CHECK(step({"Return"}).size() == 1);   // released: Alt let go first, Return is still taken
    CHECK(!start());
    CHECK(step({}).empty());
    step({"Return"});
    CHECK(start());                        // free again once released

    ev = step({"Escape"});
    CHECK(ev.size() == 1 && ev[0].hotkey == Hotkey::OpenPauseMenu && ev[0].pressed);
    CHECK(step({"Escape"}, true, false).empty());   // hotkeys off (binding capture): forgotten silently
    CHECK(step({}, true, true).empty());
    step({"K"}, false, true);                        // the overlay has the input: the game sees nothing
    CHECK_EQ(input::padState(0).buttons, 0);
    input::HostState held;
    held.keys = {"K"};
    input::holdBack(held);                           // K closed the menu: it reaches nothing until released
    step({"K"});
    CHECK_EQ(input::padState(0).buttons, 0);
    step({});
    step({"K"});
    CHECK_EQ(input::padState(0).buttons, 0x4000);
    step({});
    input::pad(0) = savedPad;
    input::hotkeys() = savedKeys;
}

TEST_CASE(input_first_run_adopts_one_controller) {
    namespace fs = std::filesystem;
    using input::Ds2;
    const fs::path dir = fs::temp_directory_path() / "ps2n_input_test";
    fs::remove_all(dir);
    input::init(dir / "ps2n.ini");                           // no settings file: a first run
    CHECK(input::adoptFirstController(1));
    CHECK(!input::adoptFirstController(2));                  // only the first controller
    const auto& cross = input::pad(0).bindings[size_t(Ds2::Cross)];
    CHECK(cross.size() == 2 && input::formatBinding(cross[0]) == "Keyboard/K" &&
          input::formatBinding(cross[1]) == "SDL-1/FaceSouth");   // beside the keyboard
    CHECK_EQ(input::formatBinding(input::pad(0).bindings[size_t(Ds2::Analog)][0]), std::string("SDL-1/Guide"));
    CHECK(fs::exists(dir / "ps2n.ini"));                     // and saved
    input::init(dir / "ps2n.ini");                           // with the file there: read back, nothing adopted
    CHECK(!input::adoptFirstController(0));
    CHECK_EQ(input::pad(0).bindings[size_t(Ds2::Cross)].size(), size_t(2));
    CHECK(!input::pad(1).connected);
    fs::remove_all(dir);
    input::pad(0) = input::defaultPad(0);
    input::pad(1) = input::defaultPad(1);
    input::hotkeys() = input::defaultHotkeys();
}

// ---- SPU2 and libsd ------------------------------------------------------------------------------
namespace {

namespace spu2 = iop::spu2;

// Emulated time stands still, so that only runSamples() moves the SPU2 (a
// register access mixes what is due by now); the real-time clock comes back after.
struct FrozenClock {
    FrozenClock() { clockInit(true, false); }
    ~FrozenClock() {
        spu2::setSink(nullptr);
        clockInit(false, false);
    }
};

constexpr uint32_t kSpu2 = 0x1F900000u;
void sw(uint32_t reg, uint16_t v) { spu2::write16(kSpu2 + reg, v); }
uint16_t sr(uint32_t reg) { return spu2::read16(kSpu2 + reg); }
int16_t soundWord(uint32_t at) {
    int16_t v;
    std::memcpy(&v, spu2::ram() + (at & 0xFFFFF) * 2, 2);
    return v;
}

// An ADPCM block at word address `at` whose 28 nibbles are all `nibble`.
void adpcmBlock(uint32_t at, int shift, int filter, int flags, int nibble) {
    uint16_t words[8] = {static_cast<uint16_t>(shift | (filter << 4) | (flags << 8))};
    for (int i = 0; i < 28; ++i) words[1 + i / 4] = static_cast<uint16_t>(words[1 + i / 4] | ((nibble & 0xF) << ((i & 3) * 4)));
    std::memcpy(spu2::ram() + at * 2, words, sizeof words);
}

uint32_t voiceParam(int core, int voice) { return static_cast<uint32_t>(core * 0x400 + voice * 16); }
uint32_t voiceAddr(int core, int voice) { return static_cast<uint32_t>(core * 0x400 + 0x1C0 + voice * 12); }
void setVoice(int core, int voice, uint32_t start, uint16_t pitch, uint16_t adsr1, uint16_t adsr2) {
    sw(voiceAddr(core, voice), static_cast<uint16_t>(start >> 16));
    sw(voiceAddr(core, voice) + 2, static_cast<uint16_t>(start));
    sw(voiceParam(core, voice) + 4, pitch);
    sw(voiceParam(core, voice) + 6, adsr1);
    sw(voiceParam(core, voice) + 8, adsr2);
}
void voiceKey(int core, int voice, bool on) {
    sw(static_cast<uint32_t>(core * 0x400 + (on ? 0x1A0 : 0x1A4) + (voice < 16 ? 0 : 2)), static_cast<uint16_t>(1u << (voice % 16)));
}
uint16_t envx(int core, int voice) { return sr(voiceParam(core, voice) + 0xA); }
uint32_t addrPair(uint32_t reg) { return (static_cast<uint32_t>(sr(reg)) << 16) | sr(reg + 2); }
uint32_t endx(int core) { return sr(static_cast<uint32_t>(core * 0x400 + 0x340)) | (static_cast<uint32_t>(sr(static_cast<uint32_t>(core * 0x400 + 0x342))) << 16); }

// The fastest linear attack and sustain: full level within six samples, held.
constexpr uint16_t kHoldAdsr1 = 0x000F, kHoldAdsr2 = 0x0000;

std::vector<int16_t> g_heard;
void hear(const int16_t* samples, size_t frames) { g_heard.insert(g_heard.end(), samples, samples + frames * 2); }

} // namespace

TEST_CASE(spu2_adpcm_follows_the_shift_and_filter_rules) {
    auto decode = [](int shift, int filter, std::initializer_list<int> nibbles, int32_t p1, int32_t p2, int16_t* out) {
        uint16_t block[8] = {static_cast<uint16_t>(shift | (filter << 4))};
        int i = 0;
        for (int n : nibbles) {
            block[1 + i / 4] = static_cast<uint16_t>(block[1 + i / 4] | ((n & 0xF) << ((i & 3) * 4)));
            ++i;
        }
        spu2::decodeAdpcm(block, p1, p2, out);
        return std::make_pair(p1, p2);
    };
    int16_t s[28];
    decode(0, 0, {1, 7, 8, 15}, 0, 0, s);   // a nibble is the top of a 16-bit sample, shifted right
    CHECK_EQ(s[0], 4096);
    CHECK_EQ(s[1], 28672);
    CHECK_EQ(s[2], -32768);
    CHECK_EQ(s[3], -4096);
    CHECK_EQ(s[4], 0);
    decode(12, 0, {7, 9}, 0, 0, s);
    CHECK_EQ(s[0], 7);
    CHECK_EQ(s[1], -7);
    decode(13, 0, {7}, 0, 0, s);            // shifts 13 to 15 act as 9
    CHECK_EQ(s[0], 56);
    decode(15, 0, {7}, 0, 0, s);
    CHECK_EQ(s[0], 56);
    const auto carried = decode(12, 1, {}, 1000, 0, s);   // filter 1: 60/64 of the last sample, rounded
    CHECK_EQ(s[0], 938);
    CHECK_EQ(s[1], 879);
    CHECK(carried.first == s[27] && carried.second == s[26]);   // the history carries on
    decode(12, 2, {}, -1000, 500, s);      // (115 * -1000 - 52 * 500 + 32) >> 6 rounds down
    CHECK_EQ(s[0], -2203);
    decode(0, 4, {7}, 30000, 0, s);         // filter 4 overflows: saturated
    CHECK_EQ(s[0], 32767);
    decode(12, 5, {}, 1000, 1000, s);       // filters 5 to 7 predict nothing
    CHECK_EQ(s[0], 0);
}

TEST_CASE(spu2_interpolation_weighs_the_last_four_samples) {
    FrozenClock frozen;
    spu2::init();
    adpcmBlock(0x3000, 0, 0, 0, 0);   // silence
    adpcmBlock(0x3008, 0, 0, 7, 4);   // then 16384, looping on itself
    // Voice 1 plays at the output rate; the core writes its output (after
    // the envelope) at 0x400 every sample. A step through the four taps at
    // phase 0 (table entries 0, 0x100, 0x1FF and 0xFF join in turn):
    setVoice(0, 1, 0x3000, 0x1000, kHoldAdsr1, kHoldAdsr2);
    voiceKey(0, 1, true);
    spu2::runSamples(40);
    const int16_t step[] = {-1, 2433, 13914, 16317};
    bool found = false;
    for (uint32_t i = 0; i < 0x200 && !found; ++i) {
        found = true;
        for (uint32_t k = 0; k < 4; ++k) found = found && soundWord(0x400 + ((i + k) & 0x1FF)) == step[k];
    }
    CHECK(found);
    // Voice 3 creeps through one interpolation phase per sample over the
    // held level; at every phase the four taps sum to 255/256 of full scale.
    setVoice(0, 3, 0x3008, 0x0010, kHoldAdsr1, kHoldAdsr2);
    voiceKey(0, 3, true);
    spu2::runSamples(1024 + 512);
    int lo = 0x7FFF, hi = -0x8000;
    for (uint32_t i = 0; i < 0x200; ++i) {
        lo = std::min<int>(lo, soundWord(0x600 + i));
        hi = std::max<int>(hi, soundWord(0x600 + i));
    }
    CHECK(lo >= 16316 && hi <= 16320);
}

TEST_CASE(spu2_envelopes_and_sweeps_step_at_their_rates) {
    FrozenClock frozen;
    spu2::init();
    adpcmBlock(0x3000, 0, 0, 7, 1);
    // Attack linear, shift 10 (+14 a sample), sustain level 7; sustain
    // decreasing at the rate that never steps; release linear, shift 10.
    setVoice(0, 0, 0x3000, 0x1000, 0x2807, 0x5FCA);
    voiceKey(0, 0, true);
    spu2::runSamples(1000);
    CHECK_EQ(envx(0, 0), 14000);
    spu2::runSamples(2000);   // full level at 2341, one decay step to 16383 (<= 0x4000), held there
    CHECK_EQ(envx(0, 0), 16383);
    voiceKey(0, 0, false);
    spu2::runSamples(500);
    CHECK_EQ(envx(0, 0), 16383 - 500 * 16);
    spu2::runSamples(600);
    CHECK_EQ(envx(0, 0), 0);
    // Volumes: bit 15 clear holds half the level; set, it sweeps from where it was.
    sw(voiceParam(0, 0), 0x3FFF);
    CHECK_EQ(sr(voiceParam(0, 0) + 0xC), 0x7FFE);   // VOLXL
    sw(voiceParam(0, 0), 0);
    sw(voiceParam(0, 0), 0x8028);   // linear increase, shift 10: +14 a sample
    spu2::runSamples(100);
    CHECK_EQ(sr(voiceParam(0, 0) + 0xC), 1400);
}

TEST_CASE(spu2_noise_follows_the_core_clock) {
    FrozenClock frozen;
    spu2::init();
    adpcmBlock(0x3000, 0, 0, 7, 0);
    setVoice(0, 1, 0x3000, 0x1000, kHoldAdsr1, kHoldAdsr2);
    sw(0x184, 1u << 1);   // NON: voice 1 plays the core's noise
    auto distinct = [] {
        std::set<int16_t> values;
        bool negative = false, positive = false;
        for (uint32_t i = 0; i < 0x200; ++i) {
            values.insert(soundWord(0x400 + i));
            negative = negative || soundWord(0x400 + i) < 0;
            positive = positive || soundWord(0x400 + i) > 0;
        }
        return std::make_pair(values.size(), negative && positive);
    };
    sw(0x19A, 0xBF00);   // ATTR: noise shift 15, step 3, the fastest clock
    voiceKey(0, 1, true);
    spu2::runSamples(1024);
    const auto fast = distinct();
    CHECK(fast.first > 200 && fast.second);
    sw(0x19A, 0x8000);   // shift 0, step 0: a step every 32768 samples
    spu2::runSamples(1024);
    CHECK(distinct().first <= 2);
}

TEST_CASE(spu2_loop_flags_set_endx_lsax_and_nax) {
    FrozenClock frozen;
    spu2::init();
    adpcmBlock(0x3000, 0, 0, 0, 1);
    adpcmBlock(0x3008, 0, 0, 4, 1);   // loop start
    adpcmBlock(0x3010, 0, 0, 3, 1);   // loop end, repeat
    setVoice(0, 2, 0x3000, 0x1000, kHoldAdsr1, kHoldAdsr2);
    voiceKey(0, 2, true);
    CHECK_EQ(addrPair(voiceAddr(0, 2) + 8), 0x3001u);   // NAX: the first data word
    spu2::runSamples(28);
    CHECK_EQ(addrPair(voiceAddr(0, 2) + 4), 0x3008u);   // LSAX from the loop-start block
    spu2::runSamples(55);
    CHECK_EQ(addrPair(voiceAddr(0, 2) + 8), 0x3017u);   // in the last block, its last word
    CHECK_EQ(endx(0) & 4u, 0u);
    spu2::runSamples(1);
    CHECK_EQ(endx(0) & 4u, 4u);                           // the end sets ENDX and jumps to LSAX
    CHECK_EQ(addrPair(voiceAddr(0, 2) + 8), 0x3009u);
    sw(0x340, 0);                                         // a write clears ENDX
    CHECK_EQ(endx(0) & 4u, 0u);
    // LSAX written after key on wins over the loop-start flag.
    voiceKey(0, 2, true);
    sw(voiceAddr(0, 2) + 4, 0);
    sw(voiceAddr(0, 2) + 6, 0x3000);
    spu2::runSamples(84);
    CHECK_EQ(addrPair(voiceAddr(0, 2) + 4), 0x3000u);
    CHECK_EQ(addrPair(voiceAddr(0, 2) + 8), 0x3001u);
    // An end without the repeat flag silences the voice.
    adpcmBlock(0x3018, 0, 0, 1, 1);
    setVoice(0, 20, 0x3018, 0x1000, 0x2C0F, kHoldAdsr2);   // attack +7 a sample: still rising at the end
    voiceKey(0, 20, true);
    spu2::runSamples(27);
    CHECK(envx(0, 20) > 0);
    spu2::runSamples(1);
    CHECK_EQ(envx(0, 20), 0);
    CHECK_EQ(endx(0) & (1u << 20), 1u << 20);
}

TEST_CASE(spu2_interrupt_address_watches_reads_and_transfers) {
    FrozenClock frozen;
    iop::init();   // the IOP kernel takes the SPU2's interrupt, line 9
    adpcmBlock(0x3000, 0, 0, 4, 1);
    adpcmBlock(0x3008, 0, 0, 0, 1);
    adpcmBlock(0x3010, 0, 0, 3, 1);
    setVoice(0, 0, 0x3000, 0x1000, kHoldAdsr1, kHoldAdsr2);
    sw(0x19C, 0);
    sw(0x19E, 0x3010);   // core 0's interrupt address: the third block's header
    sw(0x19A, 0x8040);   // core 0 on, interrupt enabled
    voiceKey(0, 0, true);
    spu2::runSamples(50);
    CHECK_EQ(sr(0x7C2) & 0xC, 0);
    spu2::runSamples(10);
    CHECK_EQ(sr(0x7C2) & 0xC, 4);
    CHECK((iop::kernel::intcRead(0x1F801070) & (1u << 9)) != 0);
    sw(0x19A, 0x8000);   // turning it off acknowledges it
    CHECK_EQ(sr(0x7C2) & 0xC, 0);
    // Core 1 watches what core 0's voices read too.
    sw(0x59C, 0);
    sw(0x59E, 0x3010);
    sw(0x59A, 0x8040);
    spu2::runSamples(84);
    CHECK_EQ(sr(0x7C2) & 0xC, 8);
    sw(0x59A, 0x8000);
    // And transfers into sound memory.
    sw(0x19E, 0x5004);
    sw(0x19A, 0x8040);
    const uint8_t data[32] = {};
    spu2::writeMemory(0xA000, data, sizeof data);   // words 0x5000-0x500F
    CHECK_EQ(sr(0x7C2) & 0xC, 4);
    sw(0x19A, 0x8000);
}

TEST_CASE(spu2_autodma_streams_pcm_through_both_cores) {
    FrozenClock frozen;
    spu2::init();
    // Two 1 KB blocks, as AutoDMA reads them: 256 left samples, then 256 right.
    std::vector<int16_t> pcm(1024);
    for (int b = 0; b < 2; ++b)
        for (int i = 0; i < 256; ++i) {
            pcm[static_cast<size_t>(b * 512 + i)] = static_cast<int16_t>(1000 * (b + 1));
            pcm[static_cast<size_t>(b * 512 + 256 + i)] = static_cast<int16_t>(-1000 * (b + 1));
        }
    iop::mem::writeBlock(0x1E0000, pcm.data(), 2048);
    sw(0x76C, 0x7FFF);   // core 0's input volume
    sw(0x76E, 0x7FFF);
    sw(0x198, 0x0C0);    // core 0: its input to the dry mix
    sw(0x760, 0x3FFF);   // core 0's master volume
    sw(0x762, 0x3FFF);
    sw(0x790, 0x7FFF);   // core 1 takes core 0's output (external input) dry
    sw(0x792, 0x7FFF);
    sw(0x598, 0x00C);
    sw(0x788, 0x3FFF);
    sw(0x78A, 0x3FFF);
    g_heard.clear();
    spu2::setSink(&hear);
    spu2::startAutoDma(0, 0x1E0000, 2048);
    CHECK(spu2::autoDmaActive(0));
    CHECK_EQ(spu2::autoDmaAddress(0), 0x1E0000u);
    spu2::runSamples(1);    // the input position enters a half: the other takes the first block
    CHECK_EQ(spu2::autoDmaAddress(0), 0x1E0400u);
    spu2::runSamples(1023);
    CHECK(!spu2::autoDmaActive(0));
    CHECK_EQ(spu2::autoDmaAddress(0), 0x1E0800u);   // where it ended, as the channel's address shows
    // Silence until that half plays, then each block in turn, through four
    // volumes (input, core 0 master, core 1's external input, core 1 master).
    CHECK_EQ(g_heard.size(), size_t(2048));
    bool ok = true;
    for (size_t i = 0; i < 1024 && ok; ++i) {
        const int16_t l = g_heard[i * 2], r = g_heard[i * 2 + 1];
        if (i < 256) ok = l == 0 && r == 0;
        else if (i < 512) ok = l == 996 && r == -1000;
        else if (i < 768) ok = l == 1996 && r == -2000;
        else ok = l == 996 && r == -1000;   // the input keeps what it had
    }
    CHECK(ok);
}

TEST_CASE(spu2_reverb_stays_in_its_work_area) {
    FrozenClock frozen;
    spu2::init();
    // libsd's Room preset: dAPF1, dAPF2, eight volumes, twenty offsets (in
    // 8-byte units), vLIN, vRIN; its work area is 0x26C0 bytes below 2 MB.
    const uint16_t room[32] = {0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80, 0x5800, 0x5300, 0x04D6,
                               0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF, 0x0336, 0x01B7, 0x0335, 0x01B6, 0x0334, 0x01B5,
                               0x0334, 0x01B5, 0x0334, 0x01B5, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000};
    auto offset = [&](uint32_t reg, uint16_t v) {
        sw(reg, static_cast<uint16_t>(v >> 14));
        sw(reg + 2, static_cast<uint16_t>(v << 2));
    };
    offset(0x2E4, room[0]);
    offset(0x2E8, room[1]);
    for (uint32_t i = 0; i < 8; ++i) sw(0x774 + i * 2, room[2 + i]);
    for (uint32_t i = 0; i < 20; ++i) offset(0x2EC + i * 4, room[10 + i]);
    sw(0x784, room[30]);
    sw(0x786, room[31]);
    const uint32_t start = 0x100000 - 0x26C0 / 2;
    sw(0x2E0, static_cast<uint16_t>(start >> 16));
    sw(0x2E2, static_cast<uint16_t>(start));
    sw(0x33C, 0xF);
    for (uint32_t a = start - 0x100; a < start; ++a) std::memcpy(spu2::ram() + a * 2, "\x34\x12", 2);   // a guard below it
    // A square wave (1.7 kHz) into core 0's dry and wet mixes; the reverb's output to core 1.
    const uint16_t square[8] = {0x0700, 0x7777, 0x7777, 0x7777, 0x9977, 0x9999, 0x9999, 0x9999};
    std::memcpy(spu2::ram() + 0x3000 * 2, square, sizeof square);
    setVoice(0, 0, 0x3000, 0x1000, kHoldAdsr1, kHoldAdsr2);
    sw(voiceParam(0, 0), 0x3FFF);
    sw(voiceParam(0, 0) + 2, 0x3FFF);
    sw(0x188, 1);   // VMIXL, VMIXEL, VMIXR, VMIXER: voice 0
    sw(0x18C, 1);
    sw(0x190, 1);
    sw(0x194, 1);
    sw(0x198, 0xF00);
    sw(0x764, 0x7FFF);   // EVOL
    sw(0x766, 0x7FFF);
    sw(0x760, 0x3FFF);
    sw(0x762, 0x3FFF);
    sw(0x790, 0x7FFF);
    sw(0x792, 0x7FFF);
    sw(0x598, 0x00C);
    sw(0x788, 0x3FFF);
    sw(0x78A, 0x3FFF);
    sw(0x19A, 0x8080);   // effects on
    voiceKey(0, 0, true);
    spu2::runSamples(4800);
    voiceKey(0, 0, false);   // release at the fastest rate: silent within two samples
    spu2::runSamples(8);
    g_heard.clear();
    spu2::setSink(&hear);
    spu2::runSamples(4800);
    int tail = 0;
    for (int16_t v : g_heard) tail = std::max(tail, std::abs(static_cast<int>(v)));
    CHECK(tail > 100);   // the dry voice has stopped; the reverb goes on
    bool guard = true, written = false;
    for (uint32_t a = start - 0x100; a < start; ++a) guard = guard && soundWord(a) == 0x1234;
    for (uint32_t a = start; a < 0x100000; ++a) written = written || soundWord(a) != 0;
    CHECK(guard);
    CHECK(written);
}

TEST_CASE(libsd_entries_name_the_spu2_registers) {
    FrozenClock frozen;
    iop::init();
    auto sd = [](uint16_t index, uint32_t a0 = 0, uint32_t a1 = 0) {
        return iop::callGuest(iop::hle::exportAddress("libsd", index), a0, a1);
    };
    enum : uint16_t { Init = 4, SetParam, GetParam, SetSwitch, GetSwitch, SetAddr, GetAddr, SetCoreAttr, GetCoreAttr };
    CHECK_EQ(sd(Init, 0), 0u);
    CHECK_EQ(sr(0x198), 0xFF0);   // core 0: voices and its input to both mixes
    CHECK_EQ(sr(0x598), 0xFFC);   // core 1: also core 0's output, dry
    CHECK_EQ(sr(0x790), 0x7FFF);
    CHECK_EQ(sd(GetSwitch, 0x1700), 0u);   // ENDX, cleared
    // Every voice ends up keyed off, looping a silent block at 0x5000.
    spu2::runSamples(100);
    CHECK(envx(0, 0) == 0 && envx(1, 23) == 0);
    CHECK(addrPair(voiceAddr(0, 0) + 8) >= 0x2800u && addrPair(voiceAddr(0, 0) + 8) < 0x2808u);
    CHECK_EQ(sd(GetSwitch, 0x1700), 0xFFFFFFu);   // and ENDX is set again by every voice's loop
    // Entries: what in bits 8-15, voice in 1-5, core in 0; bit 7 a core parameter.
    sd(SetParam, 0x020B, 0x1234);   // voice 5 of core 1, PITCH
    CHECK_EQ(sr(0x400 + 5 * 16 + 4), 0x1234);
    CHECK_EQ(sd(GetParam, 0x020B), 0x1234u);
    sd(SetParam, 0x0981, 0x2222);   // core 1, MVOLL: core parameters are 40 bytes apart
    CHECK_EQ(sr(0x788), 0x2222);
    sd(SetAddr, 0x2046, 0x5010);    // voice 3 of core 0, SSA, with Sony's voice-address bit
    CHECK_EQ(sr(0x1C0 + 3 * 12 + 2), 0x2808);
    CHECK_EQ(sd(GetAddr, 0x2046), 0x5010u);
    sd(SetAddr, 0x1D01, 0x1C0000);  // core 1, EEA: only its high part
    CHECK_EQ(sr(0x73C), 0xE);
    CHECK_EQ(sd(GetAddr, 0x1D01), 0x1DFFFFu);
    sd(SetCoreAttr, 0x0005, 1);     // core 1, interrupt enable
    CHECK((sr(0x59A) & 0x40) != 0);
    CHECK_EQ(sd(GetCoreAttr, 0x0005), 1u);
    sd(SetCoreAttr, 0x0009, 0x2A);  // core 1, noise clock
    CHECK_EQ((sr(0x59A) >> 8) & 0x3F, 0x2A);
    sd(SetSwitch, 0x1500, 1u << 3); // KON, core 0, voice 3
    spu2::runSamples(4);
    CHECK(envx(0, 3) > 0);
    CHECK_EQ(sd(GetParam, 0x0506), static_cast<uint32_t>(envx(0, 3)));
}

TEST_CASE(audio_dump_writes_the_output_as_wav) {
    namespace fs = std::filesystem;
    FrozenClock frozen;
    spu2::init();
    const fs::path file = fs::temp_directory_path() / "ps2n_audio_test.wav";
    g_opt.dumpAudio = file;
    audio::init();
    spu2::runSamples(480);
    audio::shutdown();
    g_opt.dumpAudio.clear();
    std::ifstream in(file, std::ios::binary);
    const std::vector<char> wav((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    fs::remove(file);
    CHECK_EQ(wav.size(), size_t(44 + 480 * 4));
    if (wav.size() < 44) return;
    auto u32 = [&](size_t at) {
        uint32_t v;
        std::memcpy(&v, wav.data() + at, 4);
        return v;
    };
    auto u16 = [&](size_t at) {
        uint16_t v;
        std::memcpy(&v, wav.data() + at, 2);
        return v;
    };
    CHECK(std::string(wav.data(), 4) == "RIFF" && std::string(wav.data() + 8, 8) == "WAVEfmt " &&
          std::string(wav.data() + 36, 4) == "data");
    CHECK_EQ(u32(4), 36u + 480 * 4);
    CHECK_EQ(u16(20), 1);        // PCM
    CHECK_EQ(u16(22), 2);        // stereo
    CHECK_EQ(u32(24), 48000u);
    CHECK_EQ(u32(28), 192000u);
    CHECK_EQ(u16(32), 4);
    CHECK_EQ(u16(34), 16);
    CHECK_EQ(u32(40), 480u * 4);
}

// ---- EE timers ----------------------------------------------------------------------------------
// A timer's event runs at the next safepoint, some ticks after the one it was
// due on; the interrupt must come all the same. Agent Under Fire runs its
// sound streaming from a 100 Hz timer and got 15 of its 3,000 ticks when the
// counter had to equal the compare value as the event ran.
TEST_CASE(ee_timer_interrupts_come_when_their_event_runs_late) {
    FrozenClock frozen;
    const bool wasEnabled = (read32(0x1000F010) >> hw::INTC_TIM1) & 1;
    hw::setIntcMask(hw::INTC_TIM1, false);   // only I_STAT is looked at: no handler runs
    constexpr uint32_t kCount = 0x10000800, kMode = 0x10000810, kComp = 0x10000820;
    constexpr uint64_t kTick = 512;   // BUSCLK/256, in EE cycles
    auto pass = [](uint64_t ticks) {
        clockAdvance(ticks * kTick);
        runDueEvents();
    };
    auto raised = [] {
        const bool r = (read32(0x1000F000) >> hw::INTC_TIM1) & 1;
        write32(0x1000F000, 1u << hw::INTC_TIM1);
        return r;
    };
    raised();

    write32(kCount, 0);
    write32(kComp, 100);
    write32(kMode, 0x182);   // BUSCLK/256, counting, compare interrupt
    pass(99);
    CHECK(!raised());
    pass(4);                 // the counter reached 100 three ticks ago
    CHECK(raised());
    CHECK(read32(kMode) & 0x400);   // EQUF
    CHECK_EQ(read32(kCount), 103u);  // without ZRET it counts on

    // Re-armed as the game's handler does: the counter restarts, the flag is cleared.
    write32(kCount, 0);
    write32(kMode, read32(kMode) | 0x400);
    CHECK(!(read32(kMode) & 0x400));
    pass(130);
    CHECK(raised());
    pass(65536);             // the next match is a whole wrap away
    CHECK(raised());

    write32(kMode, 0x400);   // stopped, flag cleared
    write32(kCount, 0xFFF0);
    write32(kMode, 0x282);   // overflow interrupt only
    pass(20);
    CHECK(raised());
    CHECK(read32(kMode) & 0x800);   // OVFF
    CHECK_EQ(read32(kCount), 4u);

    write32(kMode, 0xC00);
    write32(kCount, 0);
    write32(kComp, 50);
    write32(kMode, 0x1C2);   // compare interrupt and ZRET: the counter restarts after 50
    pass(60);
    CHECK(raised());
    CHECK_EQ(read32(kCount), 9u);
    pass(45);
    CHECK(raised());
    CHECK_EQ(read32(kCount), 3u);

    write32(kMode, 0xC00);
    write32(kCount, 0);
    write32(kComp, 0);
    raised();
    hw::setIntcMask(hw::INTC_TIM1, wasEnabled);
}

int main(int argc, char** argv) { return ::testing::runAll(argc, argv); }
