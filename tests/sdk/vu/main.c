// VU micro programs: VU0 programs uploaded through VIF0 and started with
// VCALLMS / VCALLMSR, a VU1 program uploaded through VIF1 and started with
// MSCAL on a TOP/TOPS double buffer, VIF1 UNPACK formats and modes, and
// XGKICK drawing to the GS (the test also checks the displayed frame).
#include <dma.h>
#include <kernel.h>
#include <stdio.h>
#include <tamtypes.h>

extern u32 Vu0CodeStart[], Vu0Flags[], Vu0Link[], Vu0CodeEnd[];
extern u32 Vu1CodeStart[], Vu1CodeEnd[];

#define VU0_DATA ((volatile u32 *)0x11004000)
#define VU1_DATA ((volatile u32 *)0x1100C000)
#define VIF1_REG(off) (*(volatile u32 *)(0x10003C00 + (off)))
#define GS_REG(off) (*(volatile u64 *)(0x12000000 + (off)))

// ---- VIF packets ----------------------------------------------------------------------
enum { NOP = 0x00, STCYCL = 0x01, OFFSET = 0x02, BASE = 0x03, ITOP = 0x04, STMOD = 0x05,
       FLUSH = 0x11, MSCAL = 0x14, STMASK = 0x20, STROW = 0x30, STCOL = 0x31, MPG = 0x4A };
// UNPACK command: 0x60 | masked (0x10) | vn << 2 | vl. Immediate: address | USN | FLG.
enum { S_32 = 0x60, V2_16 = 0x65, V3_32 = 0x68, V4_32 = 0x6C, V4_8 = 0x6E, V4_5 = 0x6F, MASKED = 0x10 };
enum { USN = 0x4000, FLG = 0x8000 };

static u32 packet[1024] __attribute__((aligned(64)));
static int words;

static void vif(u32 cmd, u32 num, u32 imm) { packet[words++] = (cmd << 24) | ((num & 0xFF) << 16) | imm; }
static void data(const void *p, int bytes)
{
    const u8 *b = p;
    int i;
    for (i = 0; i < bytes; i += 4) {
        u32 w = 0;
        int k;
        for (k = 0; k < 4 && i + k < bytes; k++)
            w |= (u32)b[i + k] << (8 * k);
        packet[words++] = w;
    }
}
static void unpack(u32 cmd, u32 imm, u32 num, const void *p, int bytes)
{
    vif(cmd, num, imm);
    data(p, bytes);
}
static void mpg(const u32 *start, const u32 *end, u32 addr)
{
    // MPG data must start on a 64-bit boundary.
    if (!(words & 1))
        vif(NOP, 0, 0);
    vif(MPG, (end - start) / 2, addr);
    data(start, (end - start) * 4);
}
static void send(int channel)
{
    while (words & 3)
        vif(NOP, 0, 0);
    dma_channel_send_normal(channel, packet, words / 4, 0, 0);
    dma_channel_wait(channel, 0);
    words = 0;
}

static u32 f2u(float f)
{
    union { float f; u32 u; } v;
    v.f = f;
    return v.u;
}
static float u2f(u32 u)
{
    union { float f; u32 u; } v;
    v.u = u;
    return v.f;
}

// ---- VU0 ------------------------------------------------------------------------------
static void wait_vu0(void)
{
    u32 stat;
    do {
        __asm__ __volatile__("cfc2 %0, $vi29" : "=r"(stat));
    } while (stat & 1);
}

static void vu0_tests(void)
{
    static const float in[4][4] = {{3, 4, 0, 1}, {1, 1, 1, 2}, {0, -2, 0, 3}, {1, 2, 2, 4}};
    static const float scale[4] __attribute__((aligned(16))) = {2, 2, 2, 0};
    u32 count = 4, vi, link, start;
    int i;

    mpg(Vu0CodeStart, Vu0CodeEnd, 0);
    send(DMA_CHANNEL_VIF0);

    for (i = 0; i < 16; i++)
        VU0_DATA[i] = f2u(in[i / 4][i % 4]);
    __asm__ __volatile__("lqc2 $vf10, 0(%0)\n"
                         "ctc2 %1, $vi1\n"
                         "vcallms 0\n" ::"r"(scale), "r"(count) : "memory");
    wait_vu0();
    for (i = 0; i < 4; i++)
        printf("vu0 normalize %d: %.4f %.4f %.4f %.4f length %.4f\n", i, u2f(VU0_DATA[32 + 4 * i]),
               u2f(VU0_DATA[33 + 4 * i]), u2f(VU0_DATA[34 + 4 * i]), u2f(VU0_DATA[35 + 4 * i]), u2f(VU0_DATA[64 + 4 * i]));
    __asm__ __volatile__("cfc2 %0, $vi1" : "=r"(vi));
    printf("vu0 loop counter %u\n", vi);

    // Inputs of the flags program: qw 24-28, and qw 38 for the load/store checks.
    {
        static const float f[4][4] = {{1, -2, 3, 1}, {0, 0, 0, 2}, {3, -3, 0, 0}, {0, 0, -5, 0}};
        for (i = 0; i < 16; i++)
            VU0_DATA[96 + i] = f2u(f[i / 4][i % 4]);
        VU0_DATA[112] = 0x12345678;
        for (i = 0; i < 4; i++)
            VU0_DATA[152 + i] = 0xA1 + i;
    }
    start = (Vu0Flags - Vu0CodeStart) / 2;
    __asm__ __volatile__("ctc2 %0, $vi27\n"
                         "vcallmsr $vi27\n" ::"r"(start) : "memory");
    wait_vu0();
    printf("vu0 flags: mac %04x status %04x after div %04x clip %03x\n", VU0_DATA[128], VU0_DATA[129], VU0_DATA[130],
           VU0_DATA[131]);
    printf("vu0 div by zero: q %08x\n", VU0_DATA[132]);
    printf("vu0 clip tests: fceq %u fcand %u fcor %u\n", VU0_DATA[133], VU0_DATA[134], VU0_DATA[135]);
    printf("vu0 integer: and %04x or %04x sub %04x mfir %08x mtir %04x\n", VU0_DATA[136], VU0_DATA[137],
           VU0_DATA[138], VU0_DATA[140], VU0_DATA[141]);
    link = (Vu0Link - Vu0CodeStart) / 2;
    printf("vu0 bal: link %s, delay slots %04x %04x\n", VU0_DATA[144] == link ? "ok" : "wrong", VU0_DATA[145],
           VU0_DATA[146]);
    printf("vu0 sqd: %x %x %x %x, iswr %u %u %x\n", VU0_DATA[124], VU0_DATA[125], VU0_DATA[126], VU0_DATA[127],
           VU0_DATA[156], VU0_DATA[157], VU0_DATA[158]);
    __asm__ __volatile__("cfc2 %0, $vi3" : "=r"(vi));
    printf("vu0 e bit: vi03 %u\n", vi);
}

// ---- VIF1 UNPACK ----------------------------------------------------------------------
static void unpack_tests(void)
{
    static const u32 s32 = 0x11223344;
    static const u16 v2s[2] = {0xFFFE, 300};
    static const u32 v3[6] = {1, 2, 3, 4, 5, 6};
    static const u16 v45 = 0x8000 | (31 << 10) | (16 << 5) | 1;
    static const u8 v48[4] = {0x80, 0x7F, 0xFF, 0x01};
    static const u32 row[4] = {100, 200, 300, 400}, col[4] = {7, 8, 9, 10};
    static const u32 one[4] = {1, 2, 3, 4}, ones[8] = {1, 1, 1, 1, 2, 2, 2, 2};
    static const u32 fillA[4] = {0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA};
    static const u32 fives[8] = {5, 5, 5, 5, 6, 6, 6, 6};
    u32 fillB[16];
    int i;

    for (i = 0; i < 16; i++)
        fillB[i] = 0xBBBBBBBB;
    vif(STCYCL, 0, 0x0101);
    vif(STMOD, 0, 0);
    unpack(S_32, 120, 1, &s32, 4);
    unpack(V2_16, 121, 1, v2s, 4);
    unpack(V2_16, 122 | USN, 1, v2s, 4);
    unpack(V3_32, 123, 2, v3, 24);
    unpack(V4_5, 125, 1, &v45, 2);
    unpack(V4_8, 126, 1, v48, 4);
    vif(STROW, 0, 0);
    data(row, 16);
    vif(STMOD, 0, 1);   // offset: data + ROW
    unpack(V4_32, 127, 1, one, 16);
    vif(STMOD, 0, 2);   // difference: ROW += data
    unpack(V4_32, 128, 2, ones, 32);
    vif(STMOD, 0, 0);
    unpack(V4_32, 130, 1, fillA, 16);
    vif(STMASK, 0, 0);
    packet[words++] = 0xE4;   // first row: x data, y ROW, z COL, w protected
    vif(STCOL, 0, 0);
    data(col, 16);
    unpack(V4_32 | MASKED, 130, 1, one, 16);
    unpack(V4_32, 131, 4, fillB, 64);
    vif(STCYCL, 0, 0x0102);   // CL 2, WL 1: write one, skip one
    unpack(V4_32, 131, 2, fives, 32);
    vif(STCYCL, 0, 0x0101);
    send(DMA_CHANNEL_VIF1);

    printf("unpack S-32: %08x %08x %08x %08x\n", VU1_DATA[480], VU1_DATA[481], VU1_DATA[482], VU1_DATA[483]);
    printf("unpack V2-16: %08x %08x, unsigned %08x %08x\n", VU1_DATA[484], VU1_DATA[485], VU1_DATA[488], VU1_DATA[489]);
    printf("unpack V3-32: %u %u %u / %u %u %u\n", VU1_DATA[492], VU1_DATA[493], VU1_DATA[494], VU1_DATA[496],
           VU1_DATA[497], VU1_DATA[498]);
    printf("unpack V4-5: %u %u %u %u\n", VU1_DATA[500], VU1_DATA[501], VU1_DATA[502], VU1_DATA[503]);
    printf("unpack V4-8: %d %d %d %d\n", (int)VU1_DATA[504], (int)VU1_DATA[505], (int)VU1_DATA[506], (int)VU1_DATA[507]);
    printf("unpack offset mode: %u %u %u %u\n", VU1_DATA[508], VU1_DATA[509], VU1_DATA[510], VU1_DATA[511]);
    printf("unpack difference mode: %u %u %u %u / %u %u %u %u\n", VU1_DATA[512], VU1_DATA[513], VU1_DATA[514],
           VU1_DATA[515], VU1_DATA[516], VU1_DATA[517], VU1_DATA[518], VU1_DATA[519]);
    printf("unpack masked: %u %u %u %08x\n", VU1_DATA[520], VU1_DATA[521], VU1_DATA[522], VU1_DATA[523]);
    printf("unpack skipping:");
    for (i = 131; i < 135; i++)
        printf(" %08x", VU1_DATA[4 * i]);
    printf("\n");
}

// ---- VU1 and the GS ----------------------------------------------------------------------
static void gs_setup(void)
{
    // A 320x224 PSMCT32 frame at address 0, cleared with a sprite (PATH3).
    static const u64 gif[2 * 10] __attribute__((aligned(16))) = {
        9 | (1 << 15) | (1ULL << 60), 0xE,                               // GIFtag: NLOOP 9, EOP, PACKED, A+D
        5 << 16, 0x4C,                                                   // FRAME_1: FBW 5 (320), PSMCT32
        ((2048ULL - 160) << 4) | (((2048ULL - 112) << 4) << 32), 0x18,   // XYOFFSET_1
        (319 << 16) | (223ULL << 48), 0x40,                              // SCISSOR_1
        0, 0x47,                                                         // TEST_1
        1, 0x1A,                                                         // PRMODECONT
        6, 0x00,                                                         // PRIM: sprite
        0x80302010, 0x01,                                                // RGBAQ
        ((2048ULL - 160) << 4) | (((2048ULL - 112) << 4) << 16), 0x05,   // XYZ2: top left
        ((2048ULL + 160) << 4) | (((2048ULL + 112) << 4) << 16), 0x05,   // XYZ2: bottom right
    };

    GS_REG(0x00) = 0x8025;                                               // PMODE: circuit 1, ALP 0x80
    GS_REG(0x20) = 0;                                                    // SMODE2: progressive
    GS_REG(0x70) = 5 << 9;                                               // DISPFB1: FBW 5, PSMCT32
    GS_REG(0x80) = 656 | (36 << 12) | (3 << 23) | (1279ULL << 32) | (223ULL << 44);   // DISPLAY1
    dma_channel_send_normal(DMA_CHANNEL_GIF, (void *)gif, 10, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
}

static void vu1_tests(void)
{
    static const float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0.25f, 0, 0, 0};
    // GIF tag: NLOOP 3, EOP, PRE, PRIM triangle with Gouraud shading, PACKED, NREG 2; REGS RGBAQ, XYZ2.
    static const u64 tag[2] = {3 | (1 << 15) | (1ULL << 46) | (0xBULL << 47) | (2ULL << 60), 0x51};
    static const u32 logStart[4] = {300, 0, 0, 0};
    static const float pos[3][3][4] = {
        {{-1, -0.5f, 2, 1}, {0, 1, 2, 1}, {-0.5f, -1, 4, 1}},
        {{0.25f, 0.25f, 1, 1}, {0.5f, 0.25f, 1, 1}, {0.5f, 0.75f, 1, 1}},
        {{-1, 0.5f, 4, 1}, {-0.25f, 0.5f, 4, 1}, {-1, -1, 4, 1}},
    };
    static const u8 color[3][3][4] = {
        {{255, 0, 0, 128}, {0, 255, 0, 128}, {0, 0, 255, 128}},
        {{200, 200, 0, 128}, {255, 128, 0, 128}, {128, 255, 64, 128}},
        {{0, 128, 255, 128}, {0, 128, 255, 128}, {0, 128, 255, 128}},
    };
    int b, i;

    mpg(Vu1CodeStart, Vu1CodeEnd, 0);
    unpack(V4_32, 0, 4, matrix, 64);
    unpack(V4_32, 4, 1, tag, 16);
    unpack(V4_32, 5, 1, logStart, 16);
    vif(BASE, 0, 16);
    vif(OFFSET, 0, 24);
    vif(ITOP, 0, 5);
    for (b = 0; b < 3; b++) {
        u32 header[8] = {f2u(160), f2u(-112), f2u(100), 3, f2u(2048), f2u(2048), f2u(1000), 0};
        unpack(V4_32, 0 | FLG, 2, header, 32);
        unpack(V4_32, 2 | FLG, 3, pos[b], 48);
        unpack(V4_8, 5 | FLG | USN, 3, color[b], 12);
        vif(MSCAL, 0, 0);
    }
    vif(FLUSH, 0, 0);
    send(DMA_CHANNEL_VIF1);

    printf("vif1: base %u offset %u tops %u top %u dbf %u itops %u itop %u\n", VIF1_REG(0xA0), VIF1_REG(0xB0),
           VIF1_REG(0xC0), VIF1_REG(0xE0), (VIF1_REG(0x00) >> 7) & 1, VIF1_REG(0x90), VIF1_REG(0xD0));
    printf("vu1 log pointer %u\n", VU1_DATA[20]);
    for (i = 0; i < 9; i++) {
        volatile u32 *xyz = VU1_DATA + 4 * (300 + 2 * i), *c = xyz + 4;
        printf("vu1 vertex %d: xyz %u %u %u (%.4f %.4f) color %.1f %.1f %.1f length %.3f\n", i, xyz[0], xyz[1], xyz[2],
               xyz[0] / 16.0f - 2048 + 160, xyz[1] / 16.0f - 2048 + 112, u2f(c[0]), u2f(c[1]), u2f(c[2]), u2f(c[3]));
    }
}

int main(void)
{
    dma_channel_initialize(DMA_CHANNEL_VIF0, NULL, 0);
    dma_channel_initialize(DMA_CHANNEL_VIF1, NULL, 0);
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);

    vu0_tests();
    unpack_tests();
    gs_setup();
    vu1_tests();
    printf("done\n");
    for (;;)
        ;   // --max-vsyncs ends the run; the test checks the displayed frame
    return 0;
}
