// Instruction semantics (ops_*.h), guest printf formatting and GS addressing.
#include "testing.h"

#include "hw/dmac.h"
#include "hw/gs.h"
#include "internal.h"
#include "iop/disc.h"
#include "iop/hostfs.h"
#include "iop/iop.h"
#include "log.h"
#include "ps2/recomp.h"
#include "stubs/format.h"

#include "ps2n/elf.h"
#include "ps2n/util.h"

#include <array>
#include <bit>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

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

int main(int argc, char** argv) { return ::testing::runAll(argc, argv); }
