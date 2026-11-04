#include "hw/gs.h"

#include "clock.h"
#include "hw/hw.h"
#include "kernel/kernel.h"
#include "log.h"
#include "ring.h"
#include "video/video.h"
#include "vu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfenv>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

namespace ps2::gs {

namespace {

// ===========================================================================
// Local memory (4 MB) and pixel addressing
// ===========================================================================
// The swizzle formulas below reproduce the block/column tables of the GS
// User's Manual (verified entry-for-entry against PCSX2's tables).
uint32_t g_vram[1024 * 1024];

enum Psm : uint32_t {
    PSMCT32 = 0x00, PSMCT24 = 0x01, PSMCT16 = 0x02, PSMCT16S = 0x0A, PSMT8 = 0x13, PSMT4 = 0x14,
    PSMT8H = 0x1B, PSMT4HL = 0x24, PSMT4HH = 0x2C, PSMZ32 = 0x30, PSMZ24 = 0x31, PSMZ16 = 0x32, PSMZ16S = 0x3A,
};

constexpr uint32_t ct32(uint32_t x, uint32_t y) { return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((x & 4) << 1) | ((y & 6) << 3); }
constexpr uint32_t bt32(uint32_t bx, uint32_t by) { return (bx & 1) | ((by & 1) << 1) | ((bx & 2) << 1) | ((by & 2) << 2) | ((bx & 4) << 2); }
constexpr uint32_t bt16(uint32_t bx, uint32_t by) { return (by & 1) | ((bx & 1) << 1) | ((by & 2) << 1) | ((bx & 2) << 2) | ((by & 4) << 2); }
constexpr uint32_t bt16s(uint32_t bx, uint32_t by) { return (by & 1) | ((bx & 1) << 1) | (by & 4) | ((by & 2) << 2) | ((bx & 2) << 3); }
constexpr uint32_t ct16(uint32_t x, uint32_t y) { return 2 * ct32(x & 7, y) + ((x >> 3) & 1); }
constexpr uint32_t ct8(uint32_t x, uint32_t y) {
    const uint32_t c = y >> 2, r = y & 3, rot = ((r >> 1) ^ (c & 1)) & 1;
    return 4 * ct32((x + 4 * rot) & 7, c * 2 + (r & 1)) + ((r >> 1) & 1) + 2 * ((x >> 3) & 1);
}
constexpr uint32_t ct4(uint32_t x, uint32_t y) {
    const uint32_t c = y >> 2, r = y & 3, rot = ((r >> 1) ^ (c & 1)) & 1;
    return 2 * (4 * ct32((x + 4 * rot) & 7, c * 2 + (r & 1)) + ((x >> 3) & 3)) + ((r >> 1) & 1);
}

bool isZ(uint32_t psm) { return (psm & 0x30) == 0x30; }

// The swizzles as tables: each pixel's offset within its page (64x32 pixels
// for 32-bit formats, 64x64 for 16-bit, 128x64 for 8-bit, 128x128 for 4-bit),
// in the format's units, filled once from the formulas above. A pixel's
// address is then its page's first unit plus one table entry.
struct Swizzle {
    uint16_t c32[2][32][64];    // words; [1] the Z layout (blocks ^ 0x18)
    uint16_t c16[4][64][64];    // halfwords; [s | z << 1]
    uint16_t c8[64][128];       // bytes
    uint16_t c4[128][128];      // nibbles
    Swizzle() {
        for (uint32_t y = 0; y < 128; ++y) {
            for (uint32_t x = 0; x < 128; ++x) {
                if (y < 32 && x < 64)
                    for (uint32_t z = 0; z < 2; ++z) c32[z][y][x] = static_cast<uint16_t>((bt32(x >> 3, y >> 3) ^ (z ? 0x18 : 0)) * 64 + ct32(x & 7, y & 7));
                if (y < 64 && x < 64) {
                    for (uint32_t v = 0; v < 4; ++v) {
                        const uint32_t block = (v & 1) ? bt16s(x >> 4, y >> 3) : bt16(x >> 4, y >> 3);
                        c16[v][y][x] = static_cast<uint16_t>((block ^ ((v & 2) ? 0x18 : 0)) * 128 + ct16(x & 15, y & 7));
                    }
                }
                if (y < 64) c8[y][x] = static_cast<uint16_t>(bt32(x >> 4, y >> 4) * 256 + ct8(x & 15, y & 15));
                c4[y][x] = static_cast<uint16_t>(bt16(x >> 5, y >> 4) * 512 + ct4(x & 31, y & 15));
            }
        }
    }
};
const Swizzle g_swizzle;

// Where each CLUT entry sits in the CLUT's first page, in the CLUT format's
// units: [0] 32-bit (words), [1] CT16, [2] CT16S (halfwords). An 8-bit
// texel's entry comes from its index in CSM1 order; a 4-bit texel's from its
// index plus CSA's offset, the first 16 entries in two rows of 8 and the
// rest in CSM1 order.
struct ClutOffsets {
    uint16_t t8[3][256];
    uint16_t t4[3][512];
    static uint32_t csm1(uint32_t i) { return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); }
    static uint16_t at(int kind, uint32_t x, uint32_t y) {
        return kind ? g_swizzle.c16[kind - 1][y][x] : g_swizzle.c32[0][y][x];
    }
    ClutOffsets() {
        for (int kind = 0; kind < 3; ++kind) {
            for (uint32_t index = 0; index < 256; ++index) {
                const uint32_t i = csm1(index);
                t8[kind][index] = at(kind, i & 15, i >> 4);
            }
            for (uint32_t i = 0; i < 512; ++i) {
                const uint32_t j = csm1(i);
                t4[kind][i] = i < 16 ? at(kind, i & 7, (i >> 3) & 1) : at(kind, j & 15, j >> 4);
            }
        }
    }
};
const ClutOffsets g_clutOffsets;

// Word index of a 32-bit pixel (CT32/CT24/Z32/Z24 and the T8H/T4HL/T4HH host words).
uint32_t addr32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, bool z) {
    const uint32_t page = (y >> 5) * std::max(bw, 1u) + (x >> 6);
    return (bp * 64 + page * 2048 + g_swizzle.c32[z][y & 31][x & 63]) & 0xFFFFF;
}
// Halfword index of a 16-bit pixel.
uint32_t addr16(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, bool s, bool z) {
    const uint32_t page = (y >> 6) * std::max(bw, 1u) + (x >> 6);
    return (bp * 128 + page * 4096 + g_swizzle.c16[(s ? 1 : 0) | (z ? 2 : 0)][y & 63][x & 63]) & 0x1FFFFF;
}
uint32_t addr8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (y >> 6) * std::max(bw >> 1, 1u) + (x >> 7);
    return (bp * 256 + page * 8192 + g_swizzle.c8[y & 63][x & 127]) & 0x3FFFFF;
}
uint32_t addr4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (y >> 7) * std::max(bw >> 1, 1u) + (x >> 7);
    return (bp * 512 + page * 16384 + g_swizzle.c4[y & 127][x & 127]) & 0x7FFFFF;
}

uint8_t* vramBytes() { return reinterpret_cast<uint8_t*>(g_vram); }

// The memory unit holding pixel (x, y) in format psm: a word, halfword, byte
// or nibble, as the format addresses memory.
uint32_t pixelUnit(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y) {
    switch (psm) {
    case PSMCT32: case PSMZ32: case PSMCT24: case PSMZ24: return addr32(bp, bw, x, y, isZ(psm));
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return addr16(bp, bw, x, y, (psm & 0x08) != 0, isZ(psm));
    case PSMT8: return addr8(bp, bw, x, y);
    case PSMT4: return addr4(bp, bw, x, y);
    case PSMT8H: case PSMT4HL: case PSMT4HH: return addr32(bp, bw, x, y, false);
    default: return 0;
    }
}

// A frame or depth buffer's addressing, worked out with the drawing state:
// the unit pixelUnit() gives, the 32 and 16-bit layouts' sums inline.
struct Buffer {
    uint32_t bp, bw, psm;
    int layout;                  // 32 or 16 bits; 0: through pixelUnit
    uint32_t base, rowOfPages;   // in words or halfwords
    int table;                   // the swizzle: Z for 32 bits, S | Z << 1 for 16
};
Buffer bufferOf(uint32_t bp, uint32_t bw, uint32_t psm) {
    Buffer b{bp, bw, psm, 0, 0, 0, 0};
    switch (psm) {
    case PSMCT32: case PSMZ32: case PSMCT24: case PSMZ24: case PSMT8H: case PSMT4HL: case PSMT4HH:
        b.layout = 32;
        b.base = bp * 64;
        b.rowOfPages = std::max(bw, 1u) * 2048;
        b.table = isZ(psm) ? 1 : 0;
        break;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S:
        b.layout = 16;
        b.base = bp * 128;
        b.rowOfPages = std::max(bw, 1u) * 4096;
        b.table = ((psm & 0x08) ? 1 : 0) | (isZ(psm) ? 2 : 0);
        break;
    default: break;
    }
    return b;
}
PS2_ALWAYS_INLINE uint32_t unitOf(const Buffer& b, uint32_t x, uint32_t y) {
    if (b.layout == 32) return (b.base + (y >> 5) * b.rowOfPages + (x >> 6) * 2048 + g_swizzle.c32[b.table][y & 31][x & 63]) & 0xFFFFF;
    if (b.layout == 16) return (b.base + (y >> 6) * b.rowOfPages + (x >> 6) * 4096 + g_swizzle.c16[b.table][y & 63][x & 63]) & 0x1FFFFF;
    return pixelUnit(b.bp, b.bw, b.psm, x, y);
}

// Raw read: the format's own bits (32/24/16-bit colour, Z, or a CLUT index).
PS2_ALWAYS_INLINE uint32_t readUnit(uint32_t psm, uint32_t n) {
    switch (psm) {
    case PSMCT32: case PSMZ32: return g_vram[n];
    case PSMCT24: case PSMZ24: return g_vram[n] & 0xFFFFFF;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: {
        uint16_t h;
        std::memcpy(&h, vramBytes() + 2 * n, 2);
        return h;
    }
    case PSMT8: return vramBytes()[n];
    case PSMT4: return (vramBytes()[n >> 1] >> ((n & 1) * 4)) & 0xF;
    case PSMT8H: return g_vram[n] >> 24;
    case PSMT4HL: return (g_vram[n] >> 24) & 0xF;
    case PSMT4HH: return g_vram[n] >> 28;
    default: return 0;
    }
}
uint32_t readRaw(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y) { return readUnit(psm, pixelUnit(bp, bw, psm, x, y)); }

PS2_ALWAYS_INLINE void writeUnit(uint32_t psm, uint32_t n, uint32_t v) {
    switch (psm) {
    case PSMCT32: case PSMZ32: g_vram[n] = v; return;
    case PSMCT24: case PSMZ24: g_vram[n] = (g_vram[n] & 0xFF000000u) | (v & 0xFFFFFF); return;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: {
        const auto h = static_cast<uint16_t>(v);
        std::memcpy(vramBytes() + 2 * n, &h, 2);
        return;
    }
    case PSMT8: vramBytes()[n] = static_cast<uint8_t>(v); return;
    case PSMT4: {
        uint8_t& b = vramBytes()[n >> 1];
        b = (n & 1) ? static_cast<uint8_t>((b & 0x0F) | ((v & 0xF) << 4)) : static_cast<uint8_t>((b & 0xF0) | (v & 0xF));
        return;
    }
    case PSMT8H: g_vram[n] = (g_vram[n] & 0x00FFFFFFu) | (v << 24); return;
    case PSMT4HL: g_vram[n] = (g_vram[n] & 0xF0FFFFFFu) | ((v & 0xF) << 24); return;
    case PSMT4HH: g_vram[n] = (g_vram[n] & 0x0FFFFFFFu) | ((v & 0xF) << 28); return;
    default: return;
    }
}
uint32_t bitsPerPixel(uint32_t psm) {
    switch (psm) {
    case PSMCT32: case PSMZ32: return 32;
    case PSMCT24: case PSMZ24: return 24;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return 16;
    case PSMT8: case PSMT8H: return 8;
    case PSMT4: case PSMT4HL: case PSMT4HH: return 4;
    default: return 32;
    }
}

// 16-bit 1:5:5:5 colour -> 32-bit RGBA, alpha per TEXA: TA1 with the A bit
// set, else TA0, or 0 with AEM when all 16 bits are 0. Black with the A bit
// set keeps TA1.
uint32_t expand16(uint32_t c, uint32_t ta0 = 0, uint32_t ta1 = 0x80, bool aem = false) {
    const uint32_t r = (c & 0x1F) << 3, g = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
    uint32_t a = (c & 0x8000) ? ta1 : ta0;
    if (aem && (c & 0xFFFF) == 0) a = 0;
    return r | (g << 8) | (b << 16) | (a << 24);
}
uint32_t pack16(uint32_t c) {
    return ((c >> 3) & 0x1F) | (((c >> 11) & 0x1F) << 5) | (((c >> 19) & 0x1F) << 10) | ((c >> 31) << 15);
}

// A frame buffer pixel as 32-bit RGBA; fbp in pages.
uint32_t pixel32(uint32_t fbp, uint32_t fbw, uint32_t psm, int x, int y) {
    const uint32_t raw = readRaw(fbp * 32, fbw, psm, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    if (psm == PSMCT16 || psm == PSMCT16S) return expand16(raw);
    if (psm == PSMCT24) return raw | 0xFF000000u;
    return raw;
}

// ===========================================================================
// Registers and state
// ===========================================================================
uint64_t g_regs[0x100];
uint64_t g_priv[0x10];   // PMODE ... BGCOLOR, indexed by (addr >> 4) & 0xF
constexpr const char* kPrivName[0x10] = {"PMODE", "SMODE1", "SMODE2", "SRFSH", "SYNCH1", "SYNCH2", "SYNCV", "DISPFB1",
                                         "DISPLAY1", "DISPFB2", "DISPLAY2", "EXTBUF", "EXTDATA", "EXTWRITE", "BGCOLOR", "0xF0"};
// CSR and SIGLBLID change on the GS thread too (FINISH, SIGNAL, LABEL).
std::atomic<uint64_t> g_csr{0x551B4000ull};   // FIFO empty, REV 0x1B, ID 0x55
uint64_t g_imr = 0x7F00;
std::atomic<uint64_t> g_siglblid{0};
bool g_path3Masked = false;

// Deinterlacing (FIELD mode): the mode, the vsyncs so far (which tell
// consecutive fields apart), the two newest fields shown on even and on odd
// lines, and the whole picture the last vsync showed.
std::atomic<Deinterlace> g_deinterlace{Deinterlace::Adaptive};
uint64_t g_fieldCount = 0;
struct ShownField {
    std::vector<uint32_t> pixels;
    uint64_t when = 0;   // the vsync that last showed it
    bool valid = false;
};
struct FieldHistory {
    int width = 0, lines = 0;
    std::array<std::array<ShownField, 2>, 2> fields;   // [even lines, odd lines][newest, the one before]
    int newest = 0;
    std::vector<uint32_t> picture;
};
FieldHistory g_history;
// Which field the game drew each frame buffer for (by its first page): a game
// that renders every field draws the odd one with a half-line Y offset, its
// samples a display line lower. -1: not drawn yet. And the vsync (+1) that
// last showed a field drawn for each.
std::array<int8_t, 512> g_drawnFor = [] {
    std::array<int8_t, 512> a;
    a.fill(-1);
    return a;
}();
std::array<uint64_t, 2> g_drawnShown;

enum Reg : uint32_t {
    PRIM = 0x00, RGBAQ = 0x01, ST = 0x02, UV = 0x03, XYZF2 = 0x04, XYZ2 = 0x05, TEX0_1 = 0x06, TEX0_2 = 0x07,
    CLAMP_1 = 0x08, CLAMP_2 = 0x09, FOG = 0x0A, XYZF3 = 0x0C, XYZ3 = 0x0D, TEX1_1 = 0x14, TEX1_2 = 0x15,
    TEX2_1 = 0x16, TEX2_2 = 0x17, XYOFFSET_1 = 0x18, XYOFFSET_2 = 0x19, PRMODECONT = 0x1A, PRMODE = 0x1B,
    TEXCLUT = 0x1C, SCANMSK = 0x22, MIPTBP1_1 = 0x34, MIPTBP1_2 = 0x35, MIPTBP2_1 = 0x36, MIPTBP2_2 = 0x37,
    TEXA = 0x3B, FOGCOL = 0x3D, TEXFLUSH = 0x3F, SCISSOR_1 = 0x40,
    SCISSOR_2 = 0x41, ALPHA_1 = 0x42, ALPHA_2 = 0x43, DIMX = 0x44, DTHE = 0x45, COLCLAMP = 0x46, TEST_1 = 0x47,
    TEST_2 = 0x48, PABE = 0x49, FBA_1 = 0x4A, FBA_2 = 0x4B, FRAME_1 = 0x4C, FRAME_2 = 0x4D, ZBUF_1 = 0x4E,
    ZBUF_2 = 0x4F, BITBLTBUF = 0x50, TRXPOS = 0x51, TRXREG = 0x52, TRXDIR = 0x53, HWREG = 0x54, SIGNAL = 0x60,
    FINISH = 0x61, LABEL = 0x62,
};

constexpr uint64_t field(uint64_t v, int lo, int bits) { return (v >> lo) & ((1ull << bits) - 1); }

void updateGsInterrupt() {
    // CSR bits 0-4 (SIGNAL FINISH HSINT VSINT EDWINT) vs IMR bits 8-12.
    const uint64_t pending = g_csr.load() & 0x1F & ~((g_imr >> 8) & 0x1F);
    if (pending) hw::raiseIntc(hw::INTC_GS);
}

// The GS draws on its own thread (setOwnThread, unless one thread draws).
// Only the guest thread changes it, while the GS thread is stopped.
bool g_async = false;
// The CSR events the GS thread raised since the guest thread last looked.
std::atomic<uint64_t> g_events{0};

// SIGNAL and FINISH: their CSR bit, and the interrupt if IMR lets it through.
// From the GS thread the guest thread raises it at its next safepoint
// (pollEvents), and wakes for it if it is waiting idle.
void gsEvent(uint64_t bit) {
    g_csr.fetch_or(bit);
    if (!g_async) return updateGsInterrupt();
    g_events.fetch_or(bit);
    clockWake();
}

void setSiglblid(uint64_t mask, uint64_t value) {
    uint64_t old = g_siglblid.load();
    while (!g_siglblid.compare_exchange_weak(old, (old & ~mask) | (value & mask))) {}
}

// ---- vertices -----------------------------------------------------------------
struct Vertex {
    float x = 0, y = 0;          // window coordinates (pixels)
    uint32_t z = 0;
    float r = 0, g = 0, b = 0, a = 0;
    float s = 0, t = 0, q = 1;
    float u = 0, v = 0;          // texels (FST=1)
    float fog = 0;
};

uint8_t g_rgba[4] = {0x80, 0x80, 0x80, 0x80};
float g_q = 1.0f, g_s = 0, g_t = 0;
uint32_t g_u = 0, g_v = 0, g_fog = 0;
Vertex g_queue[3];
int g_queueCount = 0;

// The queuing thread: whether the draw threads have a batch queued, and waits
// until it is drawn (see DrawThreads).
bool drawingQueued();
void finishDrawing();

// ---- the CLUT buffer ------------------------------------------------------------
// The console draws a CLUT texture through its CLUT buffer: 32 slots of 16
// halfwords, which a TEX0 or TEX2 write loads from local memory as CLD says.
// The rasterizer reads the CLUT from local memory when it draws instead. The
// buffer is not modelled, but where each slot was loaded from is, so that a
// draw whose entries the buffer would give from elsewhere is reported. A slot
// holds the low or the high halves of the 16 words of one column (64 bytes)
// of local memory: 16 entries of a 16-bit CLUT, or one half of 16 entries of
// a 32-bit one (the low halves in a slot, the high ones 16 slots on). A
// transfer that changes those halves after the load leaves the slot out of
// date; memory that primitives draw into is not checked.
constexpr uint32_t kClutNone = 0, kClutStale = 1, kClutCsm2 = 2;
constexpr uint32_t kClutColumn = 3;   // a column's tag: kClutColumn + (column << 1 | high halves)
constexpr uint64_t kClutFields = 0x3Full << 20 | 0xFFFFFFull << 37;   // TEX0's PSM, CBP, CPSM, CSM and CSA
struct ClutBuffer {
    std::array<uint32_t, 32> slot{};       // tags; kClutNone until loaded
    uint32_t cbp0 = 0, cbp1 = 0;           // CLD's CBP0 and CBP1
    std::array<uint8_t, 65536> columns{};  // how many slots each column of local memory filled
    uint64_t version = 0;                  // counts the slots' changes
    uint64_t loadedFields = ~uint64_t(0), loadedVersion = 0;   // the last load's CLUT fields, and the version it left
    // The last draw checked: its CLUT fields and the version then, whether
    // the slots it read held its entries, and what the first that did not held.
    uint64_t checkedFields = ~uint64_t(0), checkedVersion = 0;
    bool checkedSame = true;
    uint32_t checkedHeld = kClutNone;
};
ClutBuffer g_clut;

void resetClut() {
    g_clut.slot.fill(kClutNone);
    g_clut.cbp0 = g_clut.cbp1 = 0;
    g_clut.columns.fill(0);
    g_clut.loadedFields = g_clut.checkedFields = ~uint64_t(0);
}
void setClutSlot(uint32_t slot, uint32_t tag) {
    uint32_t& s = g_clut.slot[slot & 31];
    if (s == tag) return;
    if (s >= kClutColumn) --g_clut.columns[(s - kClutColumn) >> 1];
    if (tag >= kClutColumn) ++g_clut.columns[(tag - kClutColumn) >> 1];
    s = tag;
    ++g_clut.version;
}
bool clutFormat(uint32_t psm) { return psm == PSMT8 || psm == PSMT8H || psm == PSMT4 || psm == PSMT4HL || psm == PSMT4HH; }
// Entries 16g to 16g + 15 of a CLUT at block cbp, 16 entries wide in CSM1's
// order, are its 8x2 pixels at (8 (g & 1), 2 (g >> 1)): a column of 32-bit
// pixels, or half of a column of 16-bit ones, whose entries are the low
// (even x) or high halves of the same words. Their slot's tag:
uint32_t clutTag32(uint32_t cbp, uint32_t g, uint32_t high) {
    const uint32_t word = addr32(cbp, 1, 8 * (g & 1), 2 * (g >> 1), false);
    return kClutColumn + ((word >> 4) << 1 | high);
}
uint32_t clutTag16(uint32_t cbp, uint32_t g, bool s) {
    const uint32_t half = addr16(cbp, 1, 8 * (g & 1), 2 * (g >> 1), s, false);
    return kClutColumn + ((half >> 5) << 1 | (half & 1));
}

// A TEX0 or TEX2 write (`tex0` as it leaves TEX0): loads the buffer as CLD
// says, as PCSX2 found the console does. Only a CLUT format loads, or
// updates CBP0 and CBP1; CLD 0 loads nothing, and nor do the reserved 6 and 7.
// 32-bit entries take CSA's low 4 bits.
void loadClut(uint64_t tex0) {
    const auto psm = static_cast<uint32_t>(field(tex0, 20, 6));
    if (!clutFormat(psm)) return;
    const auto cbp = static_cast<uint32_t>(field(tex0, 37, 14));
    switch (field(tex0, 61, 3)) {
    case 1: break;
    case 2: g_clut.cbp0 = cbp; break;
    case 3: g_clut.cbp1 = cbp; break;
    case 4:
        if (g_clut.cbp0 == cbp) return;
        g_clut.cbp0 = cbp;
        break;
    case 5:
        if (g_clut.cbp1 == cbp) return;
        g_clut.cbp1 = cbp;
        break;
    default: return;
    }
    if ((tex0 & kClutFields) == g_clut.loadedFields && g_clut.version == g_clut.loadedVersion) return;   // the same again
    const auto cpsm = static_cast<uint32_t>(field(tex0, 51, 4)), csa = static_cast<uint32_t>(field(tex0, 56, 5));
    const bool csm2 = field(tex0, 55, 1) != 0, eight = psm == PSMT8 || psm == PSMT8H;
    for (uint32_t g = 0; g < (eight ? 16u : 1u); ++g) {
        if (cpsm == PSMCT32 || cpsm == PSMCT24) {
            const uint32_t slot = ((csa & 15) + g) & 15;
            setClutSlot(slot, csm2 ? kClutCsm2 : clutTag32(cbp, g, 0));
            setClutSlot(slot + 16, csm2 ? kClutCsm2 : clutTag32(cbp, g, 1));
        } else {
            setClutSlot(csa + g, csm2 ? kClutCsm2 : clutTag16(cbp, g, cpsm == PSMCT16S));
        }
    }
    g_clut.loadedFields = tex0 & kClutFields;
    g_clut.loadedVersion = g_clut.version;
}

// A draw of a texture with TEX0 `tex0`: reported when it is in a CLUT format
// and the buffer would give it other entries than the rasterizer reads from
// local memory, or when it is in CSM2's layout, which is read as CSM1's. The
// rasterizer reads an 8-bit texel's entries whatever CSA says, and a 4-bit
// texel's 16 entries CSA's low 4 bits in (ClutOffsets: CSM1 has 256 places).
void checkClut(uint64_t tex0) {
    const auto psm = static_cast<uint32_t>(field(tex0, 20, 6));
    if (!clutFormat(psm)) return;
    const auto cbp = static_cast<uint32_t>(field(tex0, 37, 14)), cpsm = static_cast<uint32_t>(field(tex0, 51, 4));
    const auto csa = static_cast<uint32_t>(field(tex0, 56, 5));
    if (field(tex0, 55, 1)) {
        PS2_UNIMPL("gs", "CLUT CSM2", "CLUT storage mode CSM2 is not implemented: the CLUT at CBP 0x%x is read in CSM1's order (TEX0 0x%llx)",
                   cbp, static_cast<unsigned long long>(tex0));
        return;
    }
    ClutBuffer& b = g_clut;
    if ((tex0 & kClutFields) != b.checkedFields || b.version != b.checkedVersion) {   // else as last time
        const bool eight = psm == PSMT8 || psm == PSMT8H;
        uint32_t held = kClutNone;   // what the first slot that differs holds
        bool same = true;
        for (uint32_t g = 0; same && g < (eight ? 16u : 1u); ++g) {
            const uint32_t group = eight ? g : csa & 15;   // the entries the rasterizer reads
            if (cpsm == PSMCT32 || cpsm == PSMCT24) {
                const uint32_t slot = ((csa & 15) + g) & 15;
                for (uint32_t high = 0; same && high < 2; ++high) {
                    held = b.slot[slot + 16 * high];
                    same = held == clutTag32(cbp, group, high);
                }
            } else {
                held = b.slot[(csa + g) & 31];
                same = held == clutTag16(cbp, group, cpsm == PSMCT16S);
            }
        }
        b.checkedFields = tex0 & kClutFields;
        b.checkedVersion = b.version;
        b.checkedSame = same;
        b.checkedHeld = held;
    }
    if (b.checkedSame) return;
    const uint32_t held = b.checkedHeld;
    if (held == kClutCsm2) {
        PS2_UNIMPL("gs", "CLUT CSM2", "CLUT storage mode CSM2 is not implemented: the CLUT buffer was loaded in CSM2's layout, read here "
                   "in CSM1's from CBP 0x%x (TEX0 0x%llx)", cbp, static_cast<unsigned long long>(tex0));
        return;
    }
    PS2_UNIMPL("gs", "CLUT buffer", "the CLUT buffer is not modelled: the CLUT is read from local memory at CBP 0x%x (TEX0 0x%llx), "
               "where the console's buffer holds entries %s", cbp, static_cast<unsigned long long>(tex0),
               held == kClutNone ? "it never loaded" : held == kClutStale ? "a transfer has changed since they were loaded" :
                                                        "loaded from other memory");
}

// ---- image transfer -------------------------------------------------------------
// A transfer's write of unit n in format psm, which leaves out of date the
// slots of the CLUT buffer loaded from halves of the word that it changes.
void transferWrite(uint32_t psm, uint32_t n, uint32_t v) {
    uint32_t word = n;   // the 32-bit layouts
    switch (psm) {
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: word = n >> 1; break;
    case PSMT8: word = n >> 2; break;
    case PSMT4: word = n >> 3; break;
    default: break;
    }
    if (!g_clut.columns[word >> 4]) return writeUnit(psm, n, v);
    const uint32_t before = g_vram[word];
    writeUnit(psm, n, v);
    const uint32_t changed = before ^ g_vram[word];
    for (uint32_t slot = 0; changed && slot < 32; ++slot) {
        const uint32_t tag = g_clut.slot[slot];
        if (tag >= kClutColumn && (tag - kClutColumn) >> 1 == word >> 4 && (changed >> (16 * ((tag - kClutColumn) & 1)) & 0xFFFF))
            setClutSlot(slot, kClutStale);
    }
}

struct Transfer {
    bool active = false;
    uint32_t dbp = 0, dbw = 0, dpsm = 0;
    uint32_t dsax = 0, dsay = 0, rrw = 0, rrh = 0;
    uint32_t x = 0, y = 0;
    uint64_t bitBuf = 0;
    uint32_t bitCount = 0;
} g_xfer;

void writeTransferPixel(uint32_t v) {
    Transfer& t = g_xfer;
    if (!t.active) return;
    transferWrite(t.dpsm, pixelUnit(t.dbp, t.dbw, t.dpsm, (t.dsax + t.x) & 2047, (t.dsay + t.y) & 2047), v);
    if (++t.x >= t.rrw) {
        t.x = 0;
        if (++t.y >= t.rrh) t.active = false;
    }
}

void transferData(uint64_t data) {
    Transfer& t = g_xfer;
    if (!t.active) return;
    if (drawingQueued()) finishDrawing();   // primitives sent in the middle of the transfer
    const uint32_t bpp = bitsPerPixel(t.dpsm);
    if (bpp == 24) {   // three bytes per pixel, packed across 64-bit words
        for (int i = 0; i < 8; ++i) {
            t.bitBuf |= ((data >> (8 * i)) & 0xFF) << t.bitCount;
            t.bitCount += 8;
            if (t.bitCount == 24) {
                writeTransferPixel(static_cast<uint32_t>(t.bitBuf));
                t.bitBuf = 0;
                t.bitCount = 0;
            }
        }
        return;
    }
    const uint64_t mask = bpp == 32 ? 0xFFFFFFFFull : (1ull << bpp) - 1;
    for (uint32_t i = 0; i < 64 / bpp; ++i) writeTransferPixel(static_cast<uint32_t>((data >> (i * bpp)) & mask));
}

void startTransfer(uint32_t dir) {
    finishDrawing();   // queued primitives may read what it writes, or write what it reads
    const uint64_t bb = g_regs[BITBLTBUF], pos = g_regs[TRXPOS], reg = g_regs[TRXREG];
    if (dir == 0) {   // host -> local
        g_xfer.active = true;
        g_xfer.dbp = static_cast<uint32_t>(field(bb, 32, 14));
        g_xfer.dbw = static_cast<uint32_t>(field(bb, 48, 6));
        g_xfer.dpsm = static_cast<uint32_t>(field(bb, 56, 6));
        g_xfer.dsax = static_cast<uint32_t>(field(pos, 32, 11));
        g_xfer.dsay = static_cast<uint32_t>(field(pos, 48, 11));
        g_xfer.rrw = static_cast<uint32_t>(field(reg, 0, 12));
        g_xfer.rrh = static_cast<uint32_t>(field(reg, 32, 12));
        g_xfer.x = g_xfer.y = 0;
        g_xfer.bitBuf = 0;
        g_xfer.bitCount = 0;
        if (!g_xfer.rrw || !g_xfer.rrh) g_xfer.active = false;
    } else if (dir == 2) {   // local -> local
        const auto sbp = static_cast<uint32_t>(field(bb, 0, 14)), sbw = static_cast<uint32_t>(field(bb, 16, 6));
        const auto spsm = static_cast<uint32_t>(field(bb, 24, 6));
        const auto dbp = static_cast<uint32_t>(field(bb, 32, 14)), dbw = static_cast<uint32_t>(field(bb, 48, 6));
        const auto dpsm = static_cast<uint32_t>(field(bb, 56, 6));
        const auto ssax = static_cast<uint32_t>(field(pos, 0, 11)), ssay = static_cast<uint32_t>(field(pos, 16, 11));
        const auto dsax = static_cast<uint32_t>(field(pos, 32, 11)), dsay = static_cast<uint32_t>(field(pos, 48, 11));
        const auto w = static_cast<uint32_t>(field(reg, 0, 12)), h = static_cast<uint32_t>(field(reg, 32, 12));
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                transferWrite(dpsm, pixelUnit(dbp, dbw, dpsm, dsax + x, dsay + y), readRaw(sbp, sbw, spsm, ssax + x, ssay + y));
    } else if (dir == 1) {
        PS2_UNIMPL("gs", "local->host transfer", "GS local->host image transfer (readback) is not implemented");
    }
}

// ---- drawing ------------------------------------------------------------------------
struct Env {
    uint64_t prim;
    uint32_t fbp, fbw, fpsm, fbmsk;
    uint32_t zbp, zpsm;
    bool zmsk;
    uint64_t test, alpha, tex0, tex1, clamp, miptbp1, miptbp2;
    int ofx, ofy;   // 12.4 fixed point
    int scx0, scx1, scy0, scy1;
    bool fba;
};

Env currentEnv() {
    Env e{};
    e.prim = (g_regs[PRMODECONT] & 1) ? g_regs[PRIM] : ((g_regs[PRIM] & 7) | (g_regs[PRMODE] & ~7ull));
    const int ctx = (e.prim >> 9) & 1;
    const uint64_t frame = g_regs[FRAME_1 + ctx], zbuf = g_regs[ZBUF_1 + ctx];
    e.fbp = static_cast<uint32_t>(field(frame, 0, 9)) * 32;   // FBP in 2048-word pages = 32 blocks
    e.fbw = static_cast<uint32_t>(field(frame, 16, 6));
    e.fpsm = static_cast<uint32_t>(field(frame, 24, 6));
    e.fbmsk = static_cast<uint32_t>(field(frame, 32, 32));
    e.zbp = static_cast<uint32_t>(field(zbuf, 0, 9)) * 32;
    e.zpsm = static_cast<uint32_t>(field(zbuf, 24, 4)) | 0x30;
    e.zmsk = field(zbuf, 32, 1) != 0;
    e.test = g_regs[TEST_1 + ctx];
    e.alpha = g_regs[ALPHA_1 + ctx];
    e.tex0 = g_regs[TEX0_1 + ctx];
    e.tex1 = g_regs[TEX1_1 + ctx];
    e.miptbp1 = g_regs[MIPTBP1_1 + ctx];
    e.miptbp2 = g_regs[MIPTBP2_1 + ctx];
    e.clamp = g_regs[CLAMP_1 + ctx];
    const uint64_t off = g_regs[XYOFFSET_1 + ctx];
    e.ofx = static_cast<int>(field(off, 0, 16));
    e.ofy = static_cast<int>(field(off, 32, 16));
    const uint64_t sc = g_regs[SCISSOR_1 + ctx];
    e.scx0 = static_cast<int>(field(sc, 0, 11));
    e.scx1 = static_cast<int>(field(sc, 16, 11));
    e.scy0 = static_cast<int>(field(sc, 32, 11));
    e.scy1 = static_cast<int>(field(sc, 48, 11));
    e.fba = field(g_regs[FBA_1 + ctx], 0, 1) != 0;
    return e;
}

// One level of a texture: the first is TEX0's, the next six (mipmaps) are
// MIPTBP1's and MIPTBP2's, each half the size of the one before.
struct Level {
    uint32_t tbp, tbw;
    int tw, th;                          // size, texels
    int minu, maxu, minv, maxv;          // CLAMP's region, scaled to the level
    uint32_t base, rowOfPages;           // its first unit and a row of pages, in the format's units
};

struct Draw;
using SampleFn = uint32_t (*)(const Draw& d, float u, float v, float q);

// What a primitive's pixels need, decoded from the registers when they
// change rather than for every pixel.
struct Draw {
    Env e;
    bool tex, fst, gouraud;              // PRIM: TME, FST, IIP
    uint32_t tpsm;                       // TEX0
    float twf, thf;                      // the first level's size, texels
    uint32_t tfx;                        // modulate, decal, highlight, highlight2
    bool tcc;
    uint32_t wms, wmt;                   // CLAMP
    // TEX1: nearest or bilinear, from one level or blending two. The level
    // of detail (LOD) picks the filter for a magnified texture (MMAG) or a
    // reduced one (MMIN), and MMIN's level. `linear` holds the filter unless
    // the LOD depends on the pixel's Q (lodByQ) or there are mipmaps.
    bool linear, lodByQ, magLinear, minLinear;
    int lodK, lodL;                      // K in sixteenths, L
    uint32_t magnifiedFrom;              // with lodByQ: the bits of the least |Q| whose LOD is below 0
    int mipLevels;                       // the levels after the first that MMIN reaches (MXL)
    int firstLevel, lastLevel;           // the levels sampling can reach: by Q all of them, else those K picks
    bool mipLinear;                      // MMIN blends the two levels around the LOD
    Level level[7];
    SampleFn sample;                     // sampleTexture for the texture's format
    uint32_t ta0, ta1;                   // TEXA
    bool aem;
    uint32_t cbp, cpsm, csa;             // the CLUT
    bool clutWide;                       // 32-bit entries, else 16-bit
    uint32_t clutBase;                   // CBP in the entries' units
    const uint16_t* clutOffsets;         // by a texel's index (ClutOffsets)
    bool ate;                            // alpha test
    uint32_t atst, aref, afail;
    bool zte;                            // depth test
    uint32_t ztst, zmax;
    bool fge;                            // fog
    uint32_t fogr, fogg, fogb;
    bool abe, pabe, colclamp;            // blending
    uint64_t ba, bb, bc, bd;
    uint32_t blendA[2], blendB[2], blendD[2];   // A, B and D as masks on Cs and Cd: one, or neither for 0
    uint32_t fix;
    bool fb16;                           // a 16-bit frame buffer, and its mask
    uint32_t fbmsk16;
    Buffer frame, depth;                 // their addressing
};
Draw g_draw;
bool g_drawDirty = true;   // a drawing register changed since g_draw was decoded
uint64_t g_drawGen = 0;    // counts the decodings, which tells states apart

// A CLUT entry as RGBA, read from local memory as it is now. A 16-bit entry
// is expanded as a 16-bit texel is, its alpha from TEXA.
PS2_ALWAYS_INLINE uint32_t clutColour(const Draw& d, uint32_t index) {
    const uint32_t at = d.clutBase + d.clutOffsets[index];
    if (d.clutWide) return g_vram[at & 0xFFFFF];
    uint16_t h;
    std::memcpy(&h, vramBytes() + 2 * (at & 0x1FFFFF), 2);
    return expand16(h, d.ta0, d.ta1, d.aem);
}

uint32_t wrapCoord(int c, int size, uint32_t mode, int minv, int maxv) {
    switch (mode) {
    case 0: return static_cast<uint32_t>(c & (size - 1));                                  // REPEAT
    case 1: return static_cast<uint32_t>(std::clamp(c, 0, size - 1));                      // CLAMP
    case 2: return static_cast<uint32_t>(std::clamp(c, minv, maxv));                       // REGION_CLAMP
    default: return static_cast<uint32_t>((c & minv) | maxv);                              // REGION_REPEAT
    }
}

// The bits of the texel at (x, y) of level l, wrapped already: its colour,
// or its index in the CLUT. Psm is the texture's format; each level's
// addressing was worked out with the drawing state.
template <uint32_t Psm>
PS2_ALWAYS_INLINE uint32_t rawTexel(const Level& l, uint32_t x, uint32_t y) {
    if constexpr (Psm == PSMCT16 || Psm == PSMCT16S) {
        const uint32_t at = l.base + (y >> 6) * l.rowOfPages + (x >> 6) * 4096 + g_swizzle.c16[Psm == PSMCT16S ? 1 : 0][y & 63][x & 63];
        uint16_t h;
        std::memcpy(&h, vramBytes() + 2 * (at & 0x1FFFFF), 2);
        return h;
    } else if constexpr (Psm == PSMT8) {
        return vramBytes()[(l.base + (y >> 6) * l.rowOfPages + (x >> 7) * 8192 + g_swizzle.c8[y & 63][x & 127]) & 0x3FFFFF];
    } else if constexpr (Psm == PSMT4) {
        const uint32_t n = (l.base + (y >> 7) * l.rowOfPages + (x >> 7) * 16384 + g_swizzle.c4[y & 127][x & 127]) & 0x7FFFFF;
        return (vramBytes()[n >> 1] >> ((n & 1) * 4)) & 0xF;
    } else {   // the 32-bit layout: CT32, CT24, T8H, T4HL, T4HH
        const uint32_t w = g_vram[(l.base + (y >> 5) * l.rowOfPages + (x >> 6) * 2048 + g_swizzle.c32[0][y & 31][x & 63]) & 0xFFFFF];
        if constexpr (Psm == PSMCT24) return w & 0xFFFFFF;
        else if constexpr (Psm == PSMT8H) return w >> 24;
        else if constexpr (Psm == PSMT4HL) return (w >> 24) & 0xF;
        else if constexpr (Psm == PSMT4HH) return w >> 28;
        else return w;
    }
}

constexpr uint32_t kAnyFormat = 0x100;   // a texture format the rasterizer does not support

// The texel at (x, y) of level l, wrapped already, as RGBA: a CLUT format
// looked up, a 16 or 24-bit one expanded with TEXA.
template <uint32_t Psm>
PS2_ALWAYS_INLINE uint32_t texel(const Draw& d, const Level& l, uint32_t x, uint32_t y) {
    if constexpr (Psm == kAnyFormat) {
        (void)l, (void)x, (void)y;
        PS2_UNIMPL("gs", "texture format", "texture format 0x%x is not supported", d.tpsm);
        return 0xFFFFFFFFu;
    } else {
        const uint32_t c = rawTexel<Psm>(l, x, y);
        if constexpr (Psm == PSMCT32) return c;
        else if constexpr (Psm == PSMCT24) return c | ((d.aem && c == 0) ? 0 : d.ta0 << 24);
        else if constexpr (Psm == PSMCT16 || Psm == PSMCT16S) return expand16(c, d.ta0, d.ta1, d.aem);
        else return clutColour(d, c);
    }
}

// A texture coordinate in sixteenths of a texel (the precision of UV),
// rounded down. Far out of range (Q near 0) it saturates.
int sixteenths(float c) {
    const float f = c * 16.0f;
    if (f >= 1073741824.0f) return 1 << 30;
    if (!(f >= -1073741824.0f)) return -(1 << 30);   // NaN too
    const int i = static_cast<int>(f);   // toward zero: one more than the floor below a negative fraction
    return static_cast<float>(i) > f ? i - 1 : i;
}

// static_cast<int>(std::floor(c)) without the library call (SSE2 has no
// rounding instruction), INT_MIN past the range as there.
int floorInt(float c) {
    const int i = static_cast<int>(c);
    return static_cast<float>(i) > c && i != INT_MIN ? i - 1 : i;
}

// Colours are blended two channels at a time, 16 bits each.
constexpr uint32_t kLanes = 0x00FF00FFu;

// Level `n` of the texture at (u, v), texel coordinates of the first level.
// Nearest takes the texel the point falls in. Bilinear blends the four
// around the point half a texel (of the level) up and left, each wrapped on
// its own, by where that point lies between them in sixteenths of a texel,
// and rounds down.
template <uint32_t Psm>
uint32_t sampleLevel(const Draw& d, int n, bool linear, float u, float v) {
    const Level& l = d.level[n];
    // Coordinates wrapped as CLAMP says.
    const auto wrapU = [&](int c) { return wrapCoord(c, l.tw, d.wms, l.minu, l.maxu); };
    const auto wrapV = [&](int c) { return wrapCoord(c, l.th, d.wmt, l.minv, l.maxv); };
    if (!linear) return texel<Psm>(d, l, wrapU(floorInt(u) >> n), wrapV(floorInt(v) >> n));
    const int su = (sixteenths(u) >> n) - 8, sv = (sixteenths(v) >> n) - 8;
    const int x = su >> 4, y = sv >> 4;
    const auto fx = static_cast<uint32_t>(su & 15), fy = static_cast<uint32_t>(sv & 15);
    const uint32_t x0 = wrapU(x), y0 = wrapV(y);
    const uint32_t c00 = texel<Psm>(d, l, x0, y0);
    if ((fx | fy) == 0) return c00;
    // A texel whose weight is 0 is not read.
    const uint32_t x1 = fx ? wrapU(x + 1) : 0, y1 = fy ? wrapV(y + 1) : 0;
    const uint32_t c10 = fx ? texel<Psm>(d, l, x1, y0) : 0, c01 = fy ? texel<Psm>(d, l, x0, y1) : 0;
    const uint32_t c11 = fx && fy ? texel<Psm>(d, l, x1, y1) : 0;
    const uint32_t w00 = (16 - fx) * (16 - fy), w10 = fx * (16 - fy), w01 = (16 - fx) * fy, w11 = fx * fy;   // sum 256
    // A weighted sum stays below 2^16.
    const uint32_t rb = ((c00 & kLanes) * w00 + (c10 & kLanes) * w10 + (c01 & kLanes) * w01 + (c11 & kLanes) * w11) >> 8;
    const uint32_t ga = (((c00 >> 8) & kLanes) * w00 + ((c10 >> 8) & kLanes) * w10 + ((c01 >> 8) & kLanes) * w01 +
                         ((c11 >> 8) & kLanes) * w11) >> 8;
    return (rb & kLanes) | (ga & kLanes) << 8;
}

// floor(128 * log2(1 + m / 2^23)) for a float's mantissa m: log2 in steps
// of 1/128 of an octave. Step i starts at the mantissa
// ceil(2^23 * (2^(i / 128) - 1)), which doubles get exactly (none is within
// 10^-5 of a whole number). The steps are wider than 2^15, so each 2^15 of
// mantissa holds at most one start: the table keeps the steps before each,
// and where a step starts inside it.
struct Log2Steps {
    std::array<uint8_t, 256> before{};
    std::array<uint32_t, 256> start{};
    Log2Steps() {
        auto starts = [](int i) {
            return i < 128 ? static_cast<uint32_t>(std::ceil(8388608.0 * (std::exp2(i / 128.0) - 1.0))) : 1u << 23;
        };
        int i = 1;
        for (uint32_t b = 0; b < 256; ++b) {
            while (starts(i) <= b << 15) ++i;
            before[b] = static_cast<uint8_t>(i - 1);
            start[b] = starts(i) < (b + 1) << 15 ? starts(i) : ~0u;
        }
    }
    int operator()(uint32_t m) const { return before[m >> 15] + (m >= start[m >> 15] ? 1 : 0); }
};
const Log2Steps g_log2Steps;

constexpr int kFarthest = 1 << 20;   // a level of detail past every level

// The level of detail at |Q|, given as its float's bits, in sixteenths (K's
// precision): log2(1 / |Q|) * 2^L + K, rounded down. That is K less
// ceil(log2|Q| * 2^(4 + L)): the exponent's octaves and the mantissa's
// steps, plus one unless the mantissa is 0 (any other has an irrational
// log2). Q = 0 is the farthest. It falls as |Q| grows.
int levelOfDetail(const Draw& d, uint32_t bits) {
    const int e = static_cast<int>(bits >> 23);
    if (e == 0) return kFarthest;   // 0 (the PS2 has no denormals)
    const uint32_t m = bits & 0x7FFFFFu;
    return d.lodK - (e - 127) * (16 << d.lodL) - (m ? (g_log2Steps(m) >> (3 - d.lodL)) + 1 : 0);
}

// The texture at (u, v), texel coordinates of the first level; the level of
// detail (LOD) comes from the pixel's Q or from K. Below 0 the texture is
// magnified and MMAG filters the first level. From 0 on MMIN filters the
// first level, or with mipmaps the level nearest the LOD, or blends the two
// around it by the LOD's sixteenths, rounding down; never past level MXL.
template <uint32_t Psm>
uint32_t sampleTexture(const Draw& d, float u, float v, float q) {
    if (!d.lodByQ && !d.mipLevels) return sampleLevel<Psm>(d, 0, d.linear, u, v);
    int lod = d.lodK;
    if (d.lodByQ) {   // from magnifiedFrom on, only that it is magnified matters
        const uint32_t bits = std::bit_cast<uint32_t>(q) & 0x7FFFFFFFu;
        lod = bits >= d.magnifiedFrom ? -1 : levelOfDetail(d, bits);
    }
    if (lod < 0) return sampleLevel<Psm>(d, 0, d.magLinear, u, v);
    if (!d.mipLevels) return sampleLevel<Psm>(d, 0, d.minLinear, u, v);
    if (!d.mipLinear) return sampleLevel<Psm>(d, std::min((lod + 8) >> 4, d.mipLevels), d.minLinear, u, v);
    const int n = lod >> 4;
    if (n >= d.mipLevels) return sampleLevel<Psm>(d, d.mipLevels, d.minLinear, u, v);
    const uint32_t a = sampleLevel<Psm>(d, n, d.minLinear, u, v);
    const auto f = static_cast<uint32_t>(lod & 15);
    if (f == 0) return a;
    const uint32_t b = sampleLevel<Psm>(d, n + 1, d.minLinear, u, v);
    const uint32_t rb = ((a & kLanes) * (16 - f) + (b & kLanes) * f) >> 4;
    const uint32_t ga = (((a >> 8) & kLanes) * (16 - f) + ((b >> 8) & kLanes) * f) >> 4;
    return (rb & kLanes) | (ga & kLanes) << 8;
}

SampleFn samplerFor(uint32_t tpsm) {
    switch (tpsm) {
    case PSMCT32: return sampleTexture<PSMCT32>;
    case PSMCT24: return sampleTexture<PSMCT24>;
    case PSMCT16: return sampleTexture<PSMCT16>;
    case PSMCT16S: return sampleTexture<PSMCT16S>;
    case PSMT8: return sampleTexture<PSMT8>;
    case PSMT4: return sampleTexture<PSMT4>;
    case PSMT8H: return sampleTexture<PSMT8H>;
    case PSMT4HL: return sampleTexture<PSMT4HL>;
    case PSMT4HH: return sampleTexture<PSMT4HH>;
    default: return sampleTexture<kAnyFormat>;
    }
}

// Each level's addressing, in the units of the texture's format: its first
// unit, and the units in a row of pages (addr32, addr16, addr8 and addr4).
void address(Level& l, uint32_t psm) {
    switch (psm) {
    case PSMCT16: case PSMCT16S: l.base = l.tbp * 128; l.rowOfPages = std::max(l.tbw, 1u) * 4096; break;
    case PSMT8: l.base = l.tbp * 256; l.rowOfPages = std::max(l.tbw >> 1, 1u) * 8192; break;
    case PSMT4: l.base = l.tbp * 512; l.rowOfPages = std::max(l.tbw >> 1, 1u) * 16384; break;
    default: l.base = l.tbp * 64; l.rowOfPages = std::max(l.tbw, 1u) * 2048; break;   // CT32, CT24, T8H, T4HL, T4HH
    }
}

bool alphaTestPass(const Draw& d, uint32_t a) {
    if (!d.ate) return true;
    switch (d.atst) {
    case 0: return false;
    case 1: return true;
    case 2: return a < d.aref;
    case 3: return a <= d.aref;
    case 4: return a == d.aref;
    case 5: return a >= d.aref;
    case 6: return a > d.aref;
    default: return a != d.aref;
    }
}

uint32_t zMax(uint32_t zpsm) {
    switch (zpsm) {
    case PSMZ24: return 0xFFFFFF;
    case PSMZ16: case PSMZ16S: return 0xFFFF;
    default: return 0xFFFFFFFF;
    }
}

// What a drawing state asks for that the rasterizer leaves out, reported
// where it would change the picture, as PCSX2 draws it: the destination alpha
// test on any frame (on a 24-bit one the console draws nothing), dithering
// into a 16-bit frame, anti-aliased lines and triangles, a scan mask, and a
// CLUT that the CLUT buffer would give differently.
void reportGaps(const Draw& d) {
    const Env& e = d.e;
    if (field(e.test, 14, 1))
        PS2_UNIMPL("gs", "destination alpha test", "the destination alpha test (TEST's DATE) is not implemented: pixels are drawn "
                   "whatever the frame's alpha, and into a 24-bit frame, where the console draws none (TEST 0x%llx, FRAME PSM 0x%x)",
                   static_cast<unsigned long long>(e.test), e.fpsm);
    if (d.fb16 && field(g_regs[DTHE], 0, 1))
        PS2_UNIMPL("gs", "dithering", "dithering (DTHE) is not implemented: a 16-bit frame is drawn without DIMX's offsets");
    const auto type = field(e.prim, 0, 3);
    if (field(e.prim, 7, 1) && type >= 1 && type <= 5)
        PS2_UNIMPL("gs", "anti-aliasing", "anti-aliasing (PRIM's AA1) is not implemented: lines and triangles are drawn without "
                   "their edges' coverage");
    if (const auto msk = field(g_regs[SCANMSK], 0, 2); msk >= 2)
        PS2_UNIMPL("gs", "scan mask", "SCANMSK %u is not implemented: lines with %s Y are drawn too", static_cast<unsigned>(msk),
                   msk == 2 ? "even" : "odd");
    if (d.tex) checkClut(e.tex0);
}

const Draw& currentDraw() {
    if (!g_drawDirty) return g_draw;
    g_drawDirty = false;
    ++g_drawGen;
    Draw& d = g_draw;
    d.e = currentEnv();
    const Env& e = d.e;
    d.tex = field(e.prim, 4, 1) != 0;
    d.fst = field(e.prim, 8, 1) != 0;
    d.gouraud = field(e.prim, 3, 1) != 0;
    d.tpsm = static_cast<uint32_t>(field(e.tex0, 20, 6));
    const int tw = 1 << std::min<int>(10, static_cast<int>(field(e.tex0, 26, 4)));
    const int th = 1 << std::min<int>(10, static_cast<int>(field(e.tex0, 30, 4)));
    d.twf = static_cast<float>(tw);
    d.thf = static_cast<float>(th);
    d.tcc = field(e.tex0, 34, 1) != 0;
    d.tfx = static_cast<uint32_t>(field(e.tex0, 35, 2));
    d.wms = static_cast<uint32_t>(field(e.clamp, 0, 2));
    d.wmt = static_cast<uint32_t>(field(e.clamp, 2, 2));
    // TEX1. The LOD is log2(1 / |Q|) * 2^L + K, or K alone with LCM and for
    // texture coordinates by UV, where Q is not interpolated. Below 0 the
    // texture is magnified and MMAG filters it; from 0 on MMIN does, and its
    // mipmap modes take the levels up to MXL. With MXL 0, MMAG filters
    // whatever the LOD: PCSX2 found MMIN ignored on a console.
    const auto mmin = static_cast<uint32_t>(field(e.tex1, 6, 3));
    const auto mxl = static_cast<int>(field(e.tex1, 2, 3));
    const bool lodFromQ = !field(e.tex1, 0, 1) && !d.fst;
    d.magLinear = field(e.tex1, 5, 1) != 0;
    // MMIN LINEAR, LINEAR_MIPMAP_NEAREST or LINEAR_MIPMAP_LINEAR; with MXL 0, as MMAG.
    d.minLinear = mxl ? mmin == 1 || mmin == 4 || mmin == 5 : d.magLinear;
    d.mipLinear = mmin == 3 || mmin == 5;   // NEAREST_MIPMAP_LINEAR, LINEAR_MIPMAP_LINEAR
    d.mipLevels = mmin >= 2 && mmin <= 5 ? std::min(mxl, 6) : 0;
    d.lodK = static_cast<int>(field(e.tex1, 32, 12) ^ 0x800) - 0x800;   // sixteenths, signed
    d.lodL = static_cast<int>(field(e.tex1, 19, 2));
    d.lodByQ = lodFromQ && (d.magLinear != d.minLinear || d.mipLevels > 0);
    d.linear = !lodFromQ && d.lodK >= 0 ? d.minLinear : d.magLinear;
    d.firstLevel = 0;
    d.lastLevel = d.mipLevels;
    if (!d.lodByQ && d.mipLevels) {   // the LOD is K
        if (d.lodK < 0) {
            d.lastLevel = 0;
        } else if (!d.mipLinear) {
            d.firstLevel = d.lastLevel = std::min((d.lodK + 8) >> 4, d.mipLevels);
        } else {
            d.firstLevel = std::min(d.lodK >> 4, d.mipLevels);
            d.lastLevel = d.firstLevel < d.mipLevels && (d.lodK & 15) ? d.firstLevel + 1 : d.firstLevel;
        }
    }
    // The LOD falls as |Q| grows: find the least |Q| magnified. It is below 0
    // at the largest float whatever K and L.
    d.magnifiedFrom = 0x7F800000u;
    if (d.lodByQ)
        for (uint32_t lo = 0; d.magnifiedFrom - lo > 1;) {
            const uint32_t mid = lo + (d.magnifiedFrom - lo) / 2;
            if (levelOfDetail(d, mid) < 0) d.magnifiedFrom = mid;
            else lo = mid;
        }
    if (d.tex && d.mipLevels && mxl > 6)
        PS2_UNIMPL("gs", "MXL 7", "TEX1 MXL is 7, past the six levels after the first; drawn as 6 (TEX1 0x%llx)",
                   static_cast<unsigned long long>(e.tex1));
    // The first level is TEX0's. MIPTBP1 holds levels 1-3 and MIPTBP2 levels
    // 4-6, each as TBP in 14 bits and TBW in 6. Each level is half the size of
    // the one before (at least a texel), and so is CLAMP's region.
    const int minu = static_cast<int>(field(e.clamp, 4, 10)), maxu = static_cast<int>(field(e.clamp, 14, 10));
    const int minv = static_cast<int>(field(e.clamp, 24, 10)), maxv = static_cast<int>(field(e.clamp, 34, 10));
    for (int n = 0; n <= d.mipLevels; ++n) {
        Level& l = d.level[n];
        const uint64_t at = n ? (n <= 3 ? e.miptbp1 : e.miptbp2) >> (20 * ((n - 1) % 3)) : e.tex0;
        l.tbp = static_cast<uint32_t>(field(at, 0, 14));
        l.tbw = static_cast<uint32_t>(field(at, 14, 6));
        l.tw = std::max(tw >> n, 1);
        l.th = std::max(th >> n, 1);
        l.minu = minu >> n;
        l.maxu = maxu >> n;
        l.minv = minv >> n;
        l.maxv = maxv >> n;
        address(l, d.tpsm);
    }
    const uint64_t texa = g_regs[TEXA];
    d.ta0 = static_cast<uint32_t>(field(texa, 0, 8));
    d.ta1 = static_cast<uint32_t>(field(texa, 32, 8));
    d.aem = field(texa, 15, 1) != 0;
    d.cbp = static_cast<uint32_t>(field(e.tex0, 37, 14));
    d.cpsm = static_cast<uint32_t>(field(e.tex0, 51, 4));
    d.csa = static_cast<uint32_t>(field(e.tex0, 56, 5));
    d.sample = samplerFor(d.tpsm);
    // CPSM 32 and 24-bit read 32-bit entries, every other value 16-bit ones.
    // A 4-bit texel's entries start CSA's 16 entries in, only CSA's low 4
    // bits with CPSM 32.
    const int clutKind = d.cpsm == PSMCT32 || d.cpsm == PSMCT24 ? 0 : d.cpsm == PSMCT16S ? 2 : 1;
    d.clutWide = clutKind == 0;
    d.clutBase = d.cbp * (d.clutWide ? 64 : 128);
    d.clutOffsets = d.tpsm == PSMT8 || d.tpsm == PSMT8H ? g_clutOffsets.t8[clutKind]
                                                         : g_clutOffsets.t4[clutKind] + (d.cpsm == PSMCT32 ? (d.csa & 15) * 16 : d.csa * 16);
    d.ate = field(e.test, 0, 1) != 0;
    d.atst = static_cast<uint32_t>(field(e.test, 1, 3));
    d.aref = static_cast<uint32_t>(field(e.test, 4, 8));
    d.afail = static_cast<uint32_t>(field(e.test, 12, 2));
    d.zte = field(e.test, 16, 1) != 0;
    d.ztst = static_cast<uint32_t>(field(e.test, 17, 2));
    d.zmax = zMax(e.zpsm);
    d.fge = field(e.prim, 5, 1) != 0;
    const uint64_t fc = g_regs[FOGCOL];
    d.fogr = static_cast<uint32_t>(field(fc, 0, 8));
    d.fogg = static_cast<uint32_t>(field(fc, 8, 8));
    d.fogb = static_cast<uint32_t>(field(fc, 16, 8));
    d.abe = field(e.prim, 6, 1) != 0;
    d.pabe = field(g_regs[PABE], 0, 1) != 0;
    d.colclamp = field(g_regs[COLCLAMP], 0, 1) != 0;
    d.ba = field(e.alpha, 0, 2);
    d.bb = field(e.alpha, 2, 2);
    d.bc = field(e.alpha, 4, 2);
    d.bd = field(e.alpha, 6, 2);
    for (int i = 0; i < 2; ++i) {   // 0 picks Cs, 1 Cd, 2 and 3 nothing
        d.blendA[i] = d.ba == static_cast<uint64_t>(i) ? ~0u : 0;
        d.blendB[i] = d.bb == static_cast<uint64_t>(i) ? ~0u : 0;
        d.blendD[i] = d.bd == static_cast<uint64_t>(i) ? ~0u : 0;
    }
    d.fix = static_cast<uint32_t>(field(e.alpha, 32, 8));
    d.fb16 = e.fpsm == PSMCT16 || e.fpsm == PSMCT16S;
    d.fbmsk16 = pack16(e.fbmsk);
    d.frame = bufferOf(e.fbp, e.fbw, e.fpsm);
    d.depth = bufferOf(e.zbp, e.fbw, e.zpsm);   // the frame's width
    reportGaps(d);
    return d;
}

// Shades and writes one pixel. Colour components are 0..255 floats; q is the
// pixel's Q, which gives the texture's level of detail. The depth test comes
// first: a pixel that fails it writes nothing, whatever the alpha test says.
PS2_ALWAYS_INLINE void plot(const Draw& dr, int x, int y, uint32_t z, float r, float g, float b, float a, float u, float v, float q, float fog,
          bool textured) {
    const Env& e = dr.e;
    if (x < e.scx0 || x > e.scx1 || y < e.scy0 || y > e.scy1) return;
    const auto ux = static_cast<uint32_t>(x), uy = static_cast<uint32_t>(y);
    uint32_t zc = 0, zUnit = 0;
    if (dr.zte) {
        if (dr.ztst == 0) return;   // NEVER
        zc = std::min(z, dr.zmax);
        zUnit = unitOf(dr.depth, ux, uy);
        if (dr.ztst >= 2) {   // GEQUAL, GREATER
            const uint32_t zd = readUnit(e.zpsm, zUnit);
            if (dr.ztst == 2 ? zc < zd : zc <= zd) return;
        }
    }
    uint32_t cr = static_cast<uint32_t>(std::clamp(r, 0.f, 255.f));
    uint32_t cg = static_cast<uint32_t>(std::clamp(g, 0.f, 255.f));
    uint32_t cb = static_cast<uint32_t>(std::clamp(b, 0.f, 255.f));
    uint32_t ca = static_cast<uint32_t>(std::clamp(a, 0.f, 255.f));

    if (textured) {
        const uint32_t t = dr.sample(dr, u, v, q);
        const uint32_t tr = t & 0xFF, tg = (t >> 8) & 0xFF, tb = (t >> 16) & 0xFF, ta = t >> 24;
        const bool tcc = dr.tcc;
        switch (dr.tfx) {
        case 0:   // MODULATE
            cr = std::min(255u, (tr * cr) >> 7);
            cg = std::min(255u, (tg * cg) >> 7);
            cb = std::min(255u, (tb * cb) >> 7);
            if (tcc) ca = std::min(255u, (ta * ca) >> 7);
            break;
        case 1:   // DECAL
            cr = tr;
            cg = tg;
            cb = tb;
            if (tcc) ca = ta;
            break;
        case 2:   // HIGHLIGHT
            cr = std::min(255u, ((tr * cr) >> 7) + ca);
            cg = std::min(255u, ((tg * cg) >> 7) + ca);
            cb = std::min(255u, ((tb * cb) >> 7) + ca);
            if (tcc) ca = std::min(255u, ta + ca);
            break;
        default:  // HIGHLIGHT2
            cr = std::min(255u, ((tr * cr) >> 7) + ca);
            cg = std::min(255u, ((tg * cg) >> 7) + ca);
            cb = std::min(255u, ((tb * cb) >> 7) + ca);
            if (tcc) ca = ta;
            break;
        }
    }
    if (dr.fge) {   // fog
        const auto f = static_cast<uint32_t>(std::clamp(fog, 0.f, 255.f));
        cr = (f * cr + (255 - f) * dr.fogr) >> 8;
        cg = (f * cg + (255 - f) * dr.fogg) >> 8;
        cb = (f * cb + (255 - f) * dr.fogb) >> 8;
    }

    bool writeFb = true, writeZ = !e.zmsk, writeA = true;
    if (!alphaTestPass(dr, ca)) {
        switch (dr.afail) {
        case 0: return;
        case 1: writeZ = false; break;
        case 2: writeFb = false; break;
        default: writeZ = false; writeA = false; break;
        }
    }
    if (dr.zte && writeZ) writeUnit(e.zpsm, zUnit, zc);
    if (!writeFb) return;

    // The frame buffer's pixel, if blending, the mask or keeping its alpha needs it.
    const uint32_t fbUnit = unitOf(dr.frame, ux, uy);
    const bool blending = dr.abe && !(dr.pabe && !(ca & 0x80));
    const uint32_t mask = dr.fb16 ? dr.fbmsk16 : e.fbmsk;
    uint32_t dst = 0, d = 0;
    if (blending || !writeA || mask) {
        dst = readUnit(e.fpsm, fbUnit);
        d = dr.fb16 ? expand16(dst) : dst;
    }
    if (blending) {
        const uint32_t dR = d & 0xFF, dG = (d >> 8) & 0xFF, dB = (d >> 16) & 0xFF, da = d >> 24;
        const int c = dr.bc == 0 ? int(ca) : dr.bc == 1 ? int(da) : int(dr.fix);
        auto blend = [&](uint32_t s, uint32_t dd) {
            const auto pick = [&](const uint32_t (&m)[2]) { return static_cast<int>((s & m[0]) | (dd & m[1])); };
            int v = ((pick(dr.blendA) - pick(dr.blendB)) * c >> 7) + pick(dr.blendD);
            return dr.colclamp ? static_cast<uint32_t>(std::clamp(v, 0, 255)) : static_cast<uint32_t>(v & 0xFF);
        };
        cr = blend(cr, dR);
        cg = blend(cg, dG);
        cb = blend(cb, dB);
    }
    if (e.fba) ca |= 0x80;
    uint32_t out = cr | (cg << 8) | (cb << 16) | (ca << 24);
    if (!writeA) out = (out & 0x00FFFFFF) | (d & 0xFF000000);
    writeUnit(e.fpsm, fbUnit, ((dr.fb16 ? pack16(out) : out) & ~mask) | (dst & mask));
}

void texCoord(const Draw& d, float s, float t, float q, float u, float v, float& outU, float& outV) {
    if (d.fst) {
        outU = u;
        outV = v;
        return;
    }
    const float iq = q != 0 ? 1.0f / q : 1.0f;
    outU = s * iq * d.twf;
    outV = t * iq * d.thf;
}

// The draw threads share every primitive: row y belongs to thread (y / 4) %
// parts, in bands of four rows (see DrawThreads). A band never shares a cache
// line of the frame or depth buffer with another.
bool myRow(int y, int part, int parts) { return parts == 1 || ((y >> 2) % parts) == part; }
// The first row from y on that belongs to `part` (y >= 0), and the next one after y.
int firstRow(int y, int part, int parts) {
    if (parts == 1) return y;
    const int band = y >> 2, ahead = (part - band % parts + parts) % parts;
    return ahead ? (band + ahead) * 4 : y;
}
int nextRow(int y, int parts) { return parts == 1 || ((y + 1) & 3) ? y + 1 : y + 1 + 4 * (parts - 1); }

struct Box {
    int x0, x1, y0, y1;   // inclusive, after the scissor
};
Box spriteBox(const Draw& d, const Vertex& a, const Vertex& b) {
    const Env& e = d.e;
    const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
    const float y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    return {std::max(static_cast<int>(std::ceil(x0)), e.scx0), std::min(static_cast<int>(std::ceil(x1)) - 1, e.scx1),
            std::max(static_cast<int>(std::ceil(y0)), e.scy0), std::min(static_cast<int>(std::ceil(y1)) - 1, e.scy1)};
}
Box triangleBox(const Draw& d, const Vertex& v0, const Vertex& v1, const Vertex& v2) {
    const Env& e = d.e;
    return {std::max(static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))), e.scx0),
            std::min(static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))), e.scx1),
            std::max(static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))), e.scy0),
            std::min(static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))), e.scy1)};
}

void drawSprite(const Draw& d, const Vertex& a, const Vertex& b, int part, int parts) {
    const Box box = spriteBox(d, a, b);
    const int ix0 = box.x0, ix1 = box.x1, iy0 = firstRow(std::max(box.y0, 0), part, parts), iy1 = box.y1;
    if (iy0 > iy1) return;
    const bool tex = d.tex;
    float ua, va, ub, vb;
    texCoord(d, a.s, a.t, a.q, a.u, a.v, ua, va);
    texCoord(d, b.s, b.t, b.q, b.u, b.v, ub, vb);
    const float dx = b.x - a.x, dy = b.y - a.y;
    for (int y = iy0; y <= iy1; y = nextRow(y, parts)) {
        const float fy = dy != 0 ? (static_cast<float>(y) - a.y) / dy : 0;
        const float v = va + (vb - va) * fy;
        float us[8] = {};   // u of eight pixels at a time, as lanes the compiler can vectorise
        for (int x = ix0; x <= ix1; ++x) {
            const int lane = (x - ix0) & 7;
            if (lane == 0) {
                if (dx != 0)
                    for (int i = 0; i < 8; ++i) us[i] = ua + (ub - ua) * ((static_cast<float>(x + i) - a.x) / dx);
                else
                    for (int i = 0; i < 8; ++i) us[i] = ua + (ub - ua) * 0.0f;
            }
            plot(d, x, y, b.z, b.r, b.g, b.b, b.a, us[lane], v, b.q, b.fog, tex);
        }
    }
}

// The edge function (p.x - px) * (q.y - py) - (q.x - px) * (p.y - py) as
// a * px + b * py + c. Vertices are 12.4 fixed point, so doubles hold every
// product exactly.
struct Edge {
    double a, b, c;
};
Edge edgeFunction(const Vertex& p, const Vertex& q, double sign) {
    return Edge{sign * (static_cast<double>(p.y) - q.y), sign * (static_cast<double>(q.x) - p.x),
                sign * (static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y)};
}

void drawTriangle(const Draw& d, const Vertex& v0, const Vertex& v1, const Vertex& v2, int part, int parts) {
    const float area = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
    if (area == 0) return;
    const Box box = triangleBox(d, v0, v1, v2);
    const int minX = box.x0, maxX = box.x1, minY = firstRow(std::max(box.y0, 0), part, parts), maxY = box.y1;
    if (minY > maxY) return;
    const bool gouraud = d.gouraud;
    const bool tex = d.tex;
    const bool persp = tex && !d.fst;
    // A pixel is drawn when its centre (whole coordinates) is inside every
    // edge, as the exact edge functions say: positive inside, as `sign` turns
    // them. A centre on an edge belongs to a left edge (the triangle to its
    // right) or a top one (level, the triangle below it), not to a right or a
    // bottom one, so an edge two triangles share is drawn once. Each row's
    // span comes from the edge functions: -k / a is within 2^-40 of the exact
    // quotient, which is a multiple of 1 / (16 * a) (k in 256ths, a in
    // sixteenths below 2^17): one that is not whole lies at least 2^-21 from
    // a whole number, so ceil takes the exact quotient's. Float weights would
    // round (toward zero on the guest's thread) and miss or add pixels on
    // edges: half of a sprite-like quad's bottom row came out drawn.
    const double sign = area > 0 ? 1.0 : -1.0;
    const Edge edges[3] = {edgeFunction(v1, v2, sign), edgeFunction(v2, v0, sign), edgeFunction(v0, v1, sign)};
    for (int y = minY; y <= maxY; y = nextRow(y, parts)) {
        double lo = minX, hi = maxX;
        bool empty = false;
        for (const Edge& g : edges) {
            const double k = g.b * y + g.c;   // the edge function at x = 0, exact
            if (g.a > 0) lo = std::max(lo, std::ceil(-k / g.a));                 // left: a * x + k >= 0
            else if (g.a < 0) hi = std::min(hi, std::ceil(-k / g.a) - 1.0);      // right: a * x + k > 0
            else if (k < 0 || (k == 0 && g.b < 0)) empty = true;                // level: below it, or a bottom edge's row
        }
        if (empty || lo > hi) continue;
        const float py = static_cast<float>(y);
        const float dy0 = v0.y - py, dy1 = v1.y - py, dy2 = v2.y - py;
        const int xFirst = static_cast<int>(lo), xLast = static_cast<int>(hi);
        // The weights of eight pixels at a time, as lanes the compiler can vectorise.
        float w0s[8] = {}, w1s[8] = {};
        for (int x = xFirst; x <= xLast; ++x) {
            const int lane = (x - xFirst) & 7;
            if (lane == 0)
                for (int i = 0; i < 8; ++i) {
                    const float lx = static_cast<float>(x + i);
                    w0s[i] = ((v1.x - lx) * dy2 - (v2.x - lx) * dy1) / area;
                    w1s[i] = ((v2.x - lx) * dy0 - (v0.x - lx) * dy2) / area;
                }
            const float w0 = w0s[lane], w1 = w1s[lane];
            const float w2 = 1.0f - w0 - w1;
            // From the first vertex by the other two's weights, as the GS steps a value from its start: one
            // the three vertices share comes out exact. Weighing all three rounds below it (toward zero on the
            // guest's thread): an alpha of 128 became 127 at scattered pixels and failed an alpha test
            // against 128, and a texture coordinate on a texel's edge took the texel before.
            auto lerp = [&](float a, float b, float c) { return a + (b - a) * w1 + (c - a) * w2; };
            // Depth in double, relative to the first vertex: a float keeps 24 of Z's 32 bits, so
            // layers drawn at one depth fought each other under GEQUAL, and a flat triangle stays exact.
            uint32_t z = 0;   // only the depth test reads it
            if (d.zte) {
                const double zd = static_cast<double>(v0.z) + w1 * (static_cast<double>(v1.z) - v0.z) + w2 * (static_cast<double>(v2.z) - v0.z);
                z = static_cast<uint32_t>(std::clamp(zd, 0.0, 4294967295.0));
            }
            float r = v2.r, g = v2.g, b = v2.b, a = v2.a;
            if (gouraud) {
                r = lerp(v0.r, v1.r, v2.r);
                g = lerp(v0.g, v1.g, v2.g);
                b = lerp(v0.b, v1.b, v2.b);
                a = lerp(v0.a, v1.a, v2.a);
            }
            float u = 0, v = 0, q = v2.q;
            if (tex) {
                if (persp) {
                    const float s = lerp(v0.s, v1.s, v2.s), t = lerp(v0.t, v1.t, v2.t);
                    q = lerp(v0.q, v1.q, v2.q);
                    texCoord(d, s, t, q, 0, 0, u, v);
                } else {
                    u = lerp(v0.u, v1.u, v2.u);
                    v = lerp(v0.v, v1.v, v2.v);
                }
            }
            plot(d, x, y, z, r, g, b, a, u, v, q, d.fge ? lerp(v0.fog, v1.fog, v2.fog) : 0.0f, tex);
        }
    }
}

void drawLine(const Draw& d, const Vertex& a, const Vertex& b, int part, int parts) {
    const int steps = static_cast<int>(std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y)));
    for (int i = 0; i <= steps; ++i) {
        const float f = steps ? static_cast<float>(i) / static_cast<float>(steps) : 0;
        const int y = static_cast<int>(a.y + (b.y - a.y) * f);
        if (!myRow(y, part, parts)) continue;
        const Vertex& c = d.gouraud ? (f < 0.5f ? a : b) : b;
        plot(d, static_cast<int>(a.x + (b.x - a.x) * f), y, b.z, c.r, c.g, c.b, c.a, 0, 0, b.q, b.fog, false);
    }
}

enum class Shape : uint8_t { Point, Line, Triangle, Sprite };

// Draws the rows of a primitive that belong to `part` of `parts`.
void drawShape(const Draw& d, Shape shape, const Vertex* v, int part, int parts) {
    switch (shape) {
    case Shape::Point: {
        const int y = static_cast<int>(v[0].y);
        if (myRow(y, part, parts))
            plot(d, static_cast<int>(v[0].x), y, v[0].z, v[0].r, v[0].g, v[0].b, v[0].a, 0, 0, v[0].q, v[0].fog, false);
        break;
    }
    case Shape::Line: drawLine(d, v[0], v[1], part, parts); break;
    case Shape::Triangle: drawTriangle(d, v[0], v[1], v[2], part, parts); break;
    case Shape::Sprite: drawSprite(d, v[0], v[1], part, parts); break;
    }
}

// ---- drawing on several threads ----------------------------------------------------
// The words of local memory that pixels (0..w-1, 0..h-1) of a buffer can
// address: from its first block to the end of the last page they reach.
struct Span {
    uint64_t lo, hi;
};
Span footprint(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t w, uint32_t h) {
    uint32_t pw = 64, ph = 32, wide = std::max(bw, 1u);   // 32-bit layouts, T8H/T4HL/T4HH too
    switch (psm) {
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: ph = 64; break;
    case PSMT8: pw = 128; ph = 64; wide = std::max(bw >> 1, 1u); break;
    case PSMT4: pw = 128; ph = 128; wide = std::max(bw >> 1, 1u); break;
    default: break;
    }
    const uint64_t pages = static_cast<uint64_t>((std::max(h, 1u) - 1) / ph) * wide + (std::max(w, 1u) - 1) / pw + 1;
    return {static_cast<uint64_t>(bp) * 64, static_cast<uint64_t>(bp) * 64 + pages * 2048};
}
constexpr uint64_t kWords = uint64_t(1) << 20;   // local memory, in words
bool meets(Span a, Span b) {
    if (a.hi > kWords || b.hi > kWords) return true;   // wraps round the end of memory: assume the worst
    return a.lo < b.hi && b.lo < a.hi;
}

// Local memory's 512 pages of 2048 words, a bit each.
struct Pages {
    std::array<uint64_t, 8> bits{};
    void add(Span s) {
        if (s.hi > kWords) return bits.fill(~uint64_t(0));   // wraps round: all of it
        for (uint64_t p = s.lo >> 11; p <= (s.hi - 1) >> 11; ++p) bits[p >> 6] |= uint64_t(1) << (p & 63);
    }
    void add(const Pages& o) {
        for (size_t i = 0; i < bits.size(); ++i) bits[i] |= o.bits[i];
    }
    bool meets(const Pages& o) const {
        for (size_t i = 0; i < bits.size(); ++i)
            if (bits[i] & o.bits[i]) return true;
        return false;
    }
};

// What primitives drawn with a state touch: the frame and depth buffers they
// write (with the layout that maps a pixel to its word: other rows own other
// words under another one), and the texels and CLUT they read.
struct Access {
    uint32_t frameLayout = 0, depthLayout = 0;
    Pages frame, depth, reads;
    bool depthUsed = false;
    bool alone = false;   // reads what it writes, or in an unsupported format
};
uint32_t layout(uint32_t bp, uint32_t bw, uint32_t psm) {
    uint32_t family = 0;   // 32 and 24-bit colour
    switch (psm) {
    case PSMCT16: family = 1; break;
    case PSMCT16S: family = 2; break;
    case PSMZ32: case PSMZ24: family = 3; break;
    case PSMZ16: family = 4; break;
    case PSMZ16S: family = 5; break;
    default: break;
    }
    return bp | bw << 14 | family << 20;
}
// The words holding the CLUT entries a texel can pick: 256 for an 8-bit
// texture, 16 from CSA's for a 4-bit one. A page from CBP on was too much:
// a CLUT in the last blocks of memory seemed to wrap round its end, and
// every primitive using it went to one thread (Code: Veronica X's fire).
Span clutSpan(const Draw& d) {
    const uint32_t entries = d.tpsm == PSMT8 || d.tpsm == PSMT8H ? 256 : 16;
    uint32_t lo = ~0u, hi = 0;
    for (uint32_t i = 0; i < entries; ++i) {
        lo = std::min<uint32_t>(lo, d.clutOffsets[i]);
        hi = std::max<uint32_t>(hi, d.clutOffsets[i]);
    }
    const uint64_t first = static_cast<uint64_t>(d.clutBase) + lo, last = static_cast<uint64_t>(d.clutBase) + hi;
    return d.clutWide ? Span{first, last + 1} : Span{first / 2, last / 2 + 1};   // 16-bit entries: halfwords
}

Access accessOf(const Draw& d) {
    const Env& e = d.e;
    Access a;
    // As far as the scissor lets it draw.
    const auto w = static_cast<uint32_t>(std::max(e.scx1, 0) + 1), h = static_cast<uint32_t>(std::max(e.scy1, 0) + 1);
    const Span frame = footprint(e.fbp, e.fbw, e.fpsm, w, h), depth = footprint(e.zbp, e.fbw, e.zpsm, w, h);
    a.frameLayout = layout(e.fbp, e.fbw, e.fpsm);
    a.frame.add(frame);
    if (d.zte) {
        a.depthUsed = true;
        a.depthLayout = layout(e.zbp, e.fbw, e.zpsm);
        a.depth.add(depth);
        if (meets(frame, depth)) a.alone = true;
    }
    if (d.tex) {
        switch (d.tpsm) {
        case PSMCT32: case PSMCT24: case PSMCT16: case PSMCT16S: break;
        case PSMT8: case PSMT4: case PSMT8H: case PSMT4HL: case PSMT4HH: {
            const Span clut = clutSpan(d);
            if (meets(clut, frame) || (d.zte && meets(clut, depth))) a.alone = true;
            a.reads.add(clut);
            break;
        }
        default: a.alone = true; break;   // reported as unsupported, from one thread
        }
        // Every level sampling can read. Region clamp and repeat can reach past
        // a level's size, up to 1024 texels on the first level, 512 on the next...
        for (int n = d.firstLevel; n <= d.lastLevel; ++n) {
            const Level& l = d.level[n];
            const auto tw = static_cast<uint32_t>(d.wms >= 2 ? 1024 >> n : l.tw);
            const auto th = static_cast<uint32_t>(d.wmt >= 2 ? 1024 >> n : l.th);
            const Span texels = footprint(l.tbp, l.tbw, d.tpsm, tw, th);
            if (meets(texels, frame) || (d.zte && meets(texels, depth))) a.alone = true;
            a.reads.add(texels);
        }
    }
    return a;
}

// The draw threads work through a batch of primitives together, each drawing
// its own bands of rows of every primitive, in the order they were queued. A
// pixel sees the same operations in the same order as with one thread, so the
// picture is the same. The thread that queues them (the GS thread, or the
// guest thread) waits for the batch to be drawn before anything else touches
// local memory (an image transfer, the display, a read-back), and before a
// primitive that reads what the batch writes, writes what it reads, or writes
// memory the batch writes under another layout. A primitive that reads what
// it writes itself is drawn by the queuing thread alone, after the batch.
// Primitives drawn by the queuing thread alone (primitivesDrawnAlone).
std::atomic<uint64_t> g_drawnAlone{0};

class DrawThreads {
public:
    ~DrawThreads() { stop(); }
    int count() {
        if (want_ <= 0) want_ = static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u));
        return want_;
    }
    // From the guest thread, with nothing queued.
    void setCount(int n) {
        stop();
        want_ = n;
    }

    // The queuing thread: one primitive with the drawing state `d`, whose
    // decoding `gen` tells apart. With one thread, drawn here and now.
    void draw(const Draw& d, uint64_t gen, Shape shape, const Vertex* v) {
        if (count() <= 1) return drawShape(d, shape, v, 0, 1);
        start();
        const int rounding = std::fegetround();   // the guest's, which the drawing inherits
        if (gen != gen_ || rounding != rounding_) {
            gen_ = gen;
            rounding_ = rounding;
            state_ = -1;
        }
        if (queued_ == kMaxPrims) finish();
        if (state_ == -1) state_ = addState(d);
        if (state_ == kAlone) {
            finish();
            g_drawnAlone.fetch_add(1, std::memory_order_relaxed);
            return drawShape(d, shape, v, 0, 1);
        }
        Prim& p = prims_[queued_++];
        p.state = static_cast<uint32_t>(state_);
        p.shape = shape;
        std::copy_n(v, shape == Shape::Triangle ? 3 : shape == Shape::Point ? 1 : 2, p.v);
        if (queued_ - published_ >= kPublishEvery) publish();
    }
    // The queuing thread: lets the draw threads see what is queued.
    void publish() {
        if (published_ == queued_) return;
        published_ = queued_;
        shared_.store(static_cast<uint64_t>(epoch_) << 32 | published_);
        if (sleepers_.load()) shared_.notify_all();
    }
    bool busy() const { return queued_ != 0; }
    // The queuing thread: waits until the batch is drawn, then empties it.
    void finish() {
        if (queued_) {
            publish();
            const uint64_t want = static_cast<uint64_t>(epoch_) << 32 | queued_;
            finishing_.store(true);
            for (int i = 0; i < threads_; ++i) {
                std::atomic<uint64_t>& done = workers_[static_cast<size_t>(i)].done;
                for (uint64_t got = done.load(); got != want; got = done.load())
                    if (!spinUntil([&] { return done.load() == want; })) done.wait(got);
            }
            finishing_.store(false);
            ++epoch_;
            queued_ = published_ = 0;
            shared_.store(static_cast<uint64_t>(epoch_) << 32);
        }
        states_ = 0;
        if (state_ >= 0) state_ = -1;
        reads_ = Pages{};
        writes_.clear();
    }

private:
    static constexpr int kAlone = -2;
    static constexpr uint32_t kMaxPrims = 1u << 16, kMaxStates = 1u << 12, kPublishEvery = 16;
    struct Prim {
        uint32_t state;
        Shape shape;
        Vertex v[3];
    };
    struct State {
        Draw draw;
        int rounding;
    };
    struct alignas(64) Worker {
        std::atomic<uint64_t> done{0};   // what it has drawn: the batch (epoch) << 32 | primitives
    };

    // Spins briefly (tens of microseconds) before a thread sleeps: the other side is usually close.
    template <typename F>
    static bool spinUntil(F ready) {
        for (int i = 0; i < 1024; ++i) {
            if (ready()) return true;
#if defined(_M_X64) || defined(__x86_64__)
            _mm_pause();
#else
            std::this_thread::yield();
#endif
        }
        return false;
    }

    // The batch's index for state `d`, after waiting for the batch when `d`
    // depends on what it does; kAlone if it reads what it writes.
    int addState(const Draw& d) {
        const Access a = accessOf(d);
        if (a.alone) return kAlone;
        bool wait = states_ == kMaxStates;
        for (const auto& [lay, pages] : writes_) {
            if (a.reads.meets(pages)) wait = true;   // reads what the batch writes
            if (a.frame.meets(pages) && lay != a.frameLayout) wait = true;
            if (a.depthUsed && a.depth.meets(pages) && lay != a.depthLayout) wait = true;
        }
        if (a.frame.meets(reads_) || (a.depthUsed && a.depth.meets(reads_))) wait = true;   // writes what it reads
        if (wait) finish();
        record(a.frameLayout, a.frame);
        if (a.depthUsed) record(a.depthLayout, a.depth);
        reads_.add(a.reads);
        stateList_[states_] = State{d, rounding_};
        return static_cast<int>(states_++);
    }
    void record(uint32_t lay, const Pages& pages) {
        for (auto& [l, p] : writes_)
            if (l == lay) return p.add(pages);
        writes_.emplace_back(lay, pages);
    }

    void start() {
        if (threads_) return;
        if (!prims_) {
            prims_ = std::make_unique<Prim[]>(kMaxPrims);
            stateList_ = std::make_unique<State[]>(kMaxStates);
        }
        threads_ = count();
        workers_ = std::make_unique<Worker[]>(static_cast<size_t>(threads_));
        for (int part = 0; part < threads_; ++part) pool_.emplace_back([this, part] { work(part); });
    }
    void stop() {
        if (!threads_) return;
        finish();
        stopping_.store(true);
        shared_.store(shared_.load() ^ (uint64_t(1) << 63));   // a change the sleepers wake for
        shared_.notify_all();
        for (std::thread& t : pool_) t.join();
        pool_.clear();
        threads_ = 0;
        stopping_.store(false);
        ++epoch_;
        shared_.store(static_cast<uint64_t>(epoch_) << 32);
    }
    void work(int part) {
        Worker& me = workers_[static_cast<size_t>(part)];
        uint32_t epoch = 0, done = 0;
        int rounding = std::fegetround();
        for (;;) {
            const uint64_t now = shared_.load();
            if (stopping_.load()) return;
            const auto batch = static_cast<uint32_t>(now >> 32), queued = static_cast<uint32_t>(now);
            if (batch != epoch) {
                epoch = batch;
                done = 0;
            }
            if (done < queued) {
                for (; done < queued; ++done) {
                    const Prim& p = prims_[done];
                    const State& s = stateList_[p.state];
                    if (s.rounding != rounding) std::fesetround(rounding = s.rounding);
                    drawShape(s.draw, p.shape, p.v, part, threads_);
                }
                me.done.store(now);
                if (finishing_.load()) me.done.notify_one();
                continue;
            }
            if (spinUntil([&] { return shared_.load() != now || stopping_.load(); })) continue;
            sleepers_.fetch_add(1);
            shared_.wait(now);
            sleepers_.fetch_sub(1);
        }
    }

    // The queuing thread's side.
    std::unique_ptr<Prim[]> prims_;
    std::unique_ptr<State[]> stateList_;
    uint32_t queued_ = 0, published_ = 0, states_ = 0, epoch_ = 1;
    int state_ = -1;          // the current state's index in the batch, -1 if not in it, kAlone
    uint64_t gen_ = ~uint64_t(0);
    int rounding_ = FE_TONEAREST;
    Pages reads_;
    std::vector<std::pair<uint32_t, Pages>> writes_;   // by layout
    // Shared with the draw threads.
    int threads_ = 0;
    std::vector<std::thread> pool_;
    std::unique_ptr<Worker[]> workers_;
    std::atomic<uint64_t> shared_{uint64_t(1) << 32};   // the batch (epoch) << 32 | primitives published
    std::atomic<int> sleepers_{0};
    std::atomic<bool> finishing_{false}, stopping_{false};
    int want_ = 0;   // 0: half the host's threads, at most 8
};
DrawThreads g_drawThreads;

bool drawingQueued() { return g_drawThreads.busy(); }
void finishDrawing() { g_drawThreads.finish(); }

void kick(bool draw) {
    const Draw& d = currentDraw();
    const Env& e = d.e;
    if (draw) g_drawnFor[(e.fbp >> 5) & 511] = static_cast<int8_t>((e.ofy >> 3) & 1);
    const uint64_t xyz = g_regs[XYZ2];
    Vertex v;
    v.x = static_cast<float>(static_cast<int>(field(xyz, 0, 16)) - e.ofx) / 16.0f;
    v.y = static_cast<float>(static_cast<int>(field(xyz, 16, 16)) - e.ofy) / 16.0f;
    v.z = static_cast<uint32_t>(field(xyz, 32, 32));
    v.r = g_rgba[0];
    v.g = g_rgba[1];
    v.b = g_rgba[2];
    v.a = g_rgba[3];
    v.s = g_s;
    v.t = g_t;
    v.q = g_q;
    v.u = static_cast<float>(g_u) / 16.0f;
    v.v = static_cast<float>(g_v) / 16.0f;
    v.fog = static_cast<float>(g_fog);

    const auto type = static_cast<uint32_t>(field(e.prim, 0, 3));
    if (g_queueCount < 3) g_queue[g_queueCount++] = v;
    switch (type) {
    case 0:   // point
        if (draw) g_drawThreads.draw(d, g_drawGen, Shape::Point, &v);
        g_queueCount = 0;
        break;
    case 1:   // line
        if (g_queueCount == 2) {
            if (draw) g_drawThreads.draw(d, g_drawGen, Shape::Line, g_queue);
            g_queueCount = 0;
        }
        break;
    case 2:   // line strip
        if (g_queueCount == 2) {
            if (draw) g_drawThreads.draw(d, g_drawGen, Shape::Line, g_queue);
            g_queue[0] = g_queue[1];
            g_queueCount = 1;
        }
        break;
    case 3:   // triangle
        if (g_queueCount == 3) {
            if (draw) g_drawThreads.draw(d, g_drawGen, Shape::Triangle, g_queue);
            g_queueCount = 0;
        }
        break;
    case 4:   // triangle strip
        if (g_queueCount == 3) {
            if (draw) g_drawThreads.draw(d, g_drawGen, Shape::Triangle, g_queue);
            g_queue[0] = g_queue[1];
            g_queue[1] = g_queue[2];
            g_queueCount = 2;
        }
        break;
    case 5:   // triangle fan
        if (g_queueCount == 3) {
            if (draw) g_drawThreads.draw(d, g_drawGen, Shape::Triangle, g_queue);
            g_queue[1] = g_queue[2];
            g_queueCount = 2;
        }
        break;
    case 6:   // sprite
        if (g_queueCount == 2) {
            if (draw) g_drawThreads.draw(d, g_drawGen, Shape::Sprite, g_queue);
            g_queueCount = 0;
        }
        break;
    default:
        g_queueCount = 0;
        break;
    }
}

// TEX1's MTBA: writing TEX0 places levels 1-3 (MIPTBP1) one after another
// behind the first, each a quarter of the one before (at least a block) and
// half as wide (at least 64 texels). It reckons from the width alone, as if
// the texture were square, and only for colour formats from 32 texels wide
// up to 512 (1024 below 32 bits a texel in memory). TEX2 does not do it.
void placeMipLevels(int ctx, uint64_t tex0) {
    const auto psm = static_cast<uint32_t>(field(tex0, 20, 6));
    const auto tw = static_cast<uint32_t>(field(tex0, 26, 4));
    // Bits a texel takes in memory: 32 for the 24-bit and T8H/T4HL/T4HH layouts too.
    const uint32_t bits = psm == PSMCT16 || psm == PSMCT16S ? 16 : psm == PSMT8 ? 8 : psm == PSMT4 ? 4 : 32;
    if (isZ(psm) || tw < 5 || tw > (bits == 32 ? 9u : 10u)) return;
    auto bp = static_cast<uint32_t>(field(tex0, 0, 14));
    uint32_t bw = std::max((1u << tw) >> 6, 1u), blocks = ((1u << (2 * tw)) * bits) >> 11;   // 256-byte blocks
    uint64_t mip = g_regs[MIPTBP1_1 + ctx] & ~((uint64_t(1) << 60) - 1);
    for (int i = 0; i < 3; ++i) {
        bp = (bp + blocks) & 0x3FFF;
        bw = std::max(bw >> 1, 1u);
        blocks = std::max(blocks >> 2, 1u);
        mip |= (static_cast<uint64_t>(bp) | static_cast<uint64_t>(bw) << 14) << (20 * i);
    }
    g_regs[MIPTBP1_1 + ctx] = mip;
}

void writeReg(uint32_t reg, uint64_t v) {
    reg &= 0xFF;
    switch (reg) {   // vertex data and image data leave the drawing state (g_draw) alone
    case RGBAQ: case ST: case UV: case XYZF2: case XYZ2: case XYZF3: case XYZ3: case FOG: case HWREG: break;
    default: g_drawDirty = true; break;
    }
    switch (reg) {
    case PRIM:
        g_regs[PRIM] = v & 0x7FF;
        g_queueCount = 0;
        return;
    case RGBAQ:
        g_rgba[0] = static_cast<uint8_t>(v);
        g_rgba[1] = static_cast<uint8_t>(v >> 8);
        g_rgba[2] = static_cast<uint8_t>(v >> 16);
        g_rgba[3] = static_cast<uint8_t>(v >> 24);
        g_q = std::bit_cast<float>(static_cast<uint32_t>(v >> 32));
        break;
    case ST:
        g_s = std::bit_cast<float>(static_cast<uint32_t>(v));
        g_t = std::bit_cast<float>(static_cast<uint32_t>(v >> 32));
        break;
    case UV:
        g_u = static_cast<uint32_t>(field(v, 0, 14));
        g_v = static_cast<uint32_t>(field(v, 16, 14));
        break;
    case XYZF2:
    case XYZF3:
        g_fog = static_cast<uint32_t>(field(v, 56, 8));
        g_regs[XYZ2] = field(v, 0, 32) | (field(v, 32, 24) << 32);
        kick(reg == XYZF2);
        return;
    case XYZ2:
    case XYZ3:
        g_regs[XYZ2] = v;
        kick(reg == XYZ2);
        return;
    case FOG: g_fog = static_cast<uint32_t>(field(v, 56, 8)); break;
    case TEX0_1:
    case TEX0_2:
        if (field(g_regs[TEX1_1 + (reg - TEX0_1)], 9, 1)) placeMipLevels(static_cast<int>(reg - TEX0_1), v);   // MTBA
        loadClut(v);
        break;
    case TEX2_1:
    case TEX2_2: {   // replaces the CLUT/PSM fields of TEX0
        const uint64_t keep = 0xFFFFFull | (0xFull << 26) | (0xFull << 30) | (0x7ull << 34);   // TBP0 TBW TW TH TCC TFX
        uint64_t& t0 = g_regs[TEX0_1 + (reg - TEX2_1)];
        t0 = (t0 & keep) | (v & ~keep);
        loadClut(t0);
        break;
    }
    case TRXDIR:
        g_regs[TRXDIR] = v;
        startTransfer(static_cast<uint32_t>(v & 3));
        return;
    case HWREG: transferData(v); return;
    case SIGNAL: {
        const auto id = static_cast<uint32_t>(v), mask = static_cast<uint32_t>(v >> 32);
        setSiglblid(mask, id);
        gsEvent(1);
        break;
    }
    case FINISH: gsEvent(2); break;
    case LABEL: {
        const auto id = static_cast<uint32_t>(v), mask = static_cast<uint32_t>(v >> 32);
        setSiglblid(static_cast<uint64_t>(mask) << 32, static_cast<uint64_t>(id) << 32);
        break;
    }
    default: break;
    }
    g_regs[reg] = v;
}

// ---- GIF packet parsing ----------------------------------------------------------------
struct GifPath {
    uint32_t nloop = 0, nreg = 0, flg = 0, regIdx = 0;
    uint64_t regs = 0;
    bool eop = false;
};
GifPath g_paths[4];

void packedReg(uint32_t desc, const uint64_t q[2]) {
    switch (desc) {
    case 0x0: writeReg(PRIM, q[0] & 0x7FF); break;
    case 0x1:
        g_rgba[0] = static_cast<uint8_t>(q[0]);
        g_rgba[1] = static_cast<uint8_t>(q[0] >> 32);
        g_rgba[2] = static_cast<uint8_t>(q[1]);
        g_rgba[3] = static_cast<uint8_t>(q[1] >> 32);
        g_regs[RGBAQ] = g_rgba[0] | (g_rgba[1] << 8) | (g_rgba[2] << 16) | (static_cast<uint64_t>(g_rgba[3]) << 24) |
                        (static_cast<uint64_t>(std::bit_cast<uint32_t>(g_q)) << 32);
        break;
    case 0x2:
        g_s = std::bit_cast<float>(static_cast<uint32_t>(q[0]));
        g_t = std::bit_cast<float>(static_cast<uint32_t>(q[0] >> 32));
        g_q = std::bit_cast<float>(static_cast<uint32_t>(q[1]));
        break;
    case 0x3:
        g_u = static_cast<uint32_t>(q[0] & 0x3FFF);
        g_v = static_cast<uint32_t>((q[0] >> 32) & 0x3FFF);
        break;
    case 0x4:
    case 0xC: {   // XYZF2 / XYZF3
        const bool adc = (q[1] >> 47) & 1;
        g_fog = static_cast<uint32_t>((q[1] >> 36) & 0xFF);
        g_regs[XYZ2] = (q[0] & 0xFFFF) | (((q[0] >> 32) & 0xFFFF) << 16) | (((q[1] >> 4) & 0xFFFFFF) << 32);
        kick(desc == 0x4 && !adc);
        break;
    }
    case 0x5:
    case 0xD: {   // XYZ2 / XYZ3
        const bool adc = (q[1] >> 47) & 1;
        g_regs[XYZ2] = (q[0] & 0xFFFF) | (((q[0] >> 32) & 0xFFFF) << 16) | ((q[1] & 0xFFFFFFFFull) << 32);
        kick(desc == 0x5 && !adc);
        break;
    }
    case 0xA: g_fog = static_cast<uint32_t>((q[1] >> 36) & 0xFF); break;
    case 0xE: writeReg(static_cast<uint32_t>(q[1] & 0xFF), q[0]); break;   // A+D
    case 0xF: break;                                                          // NOP
    default: writeReg(desc, q[0]); break;                                      // TEX0_x, CLAMP_x
    }
}

void gifQword(GifPath& p, const uint64_t q[2]) {
    if (p.nloop == 0) {   // a new GIFtag
        p.nloop = static_cast<uint32_t>(q[0] & 0x7FFF);
        p.eop = (q[0] >> 15) & 1;
        if ((q[0] >> 46) & 1) writeReg(PRIM, (q[0] >> 47) & 0x7FF);   // PRE
        p.flg = static_cast<uint32_t>((q[0] >> 58) & 3);
        p.nreg = static_cast<uint32_t>((q[0] >> 60) & 0xF);
        if (p.nreg == 0) p.nreg = 16;
        p.regs = q[1];
        p.regIdx = 0;
        return;
    }
    switch (p.flg) {
    case 0:   // PACKED
        packedReg(static_cast<uint32_t>((p.regs >> (4 * p.regIdx)) & 0xF), q);
        if (++p.regIdx == p.nreg) {
            p.regIdx = 0;
            --p.nloop;
        }
        break;
    case 1:   // REGLIST: two 64-bit register values per quadword
        for (int half = 0; half < 2 && p.nloop; ++half) {
            writeReg(static_cast<uint32_t>((p.regs >> (4 * p.regIdx)) & 0xF), q[half]);
            if (++p.regIdx == p.nreg) {
                p.regIdx = 0;
                --p.nloop;
            }
        }
        break;
    default:  // IMAGE
        transferData(q[0]);
        transferData(q[1]);
        --p.nloop;
        break;
    }
}

void resetPaths() {
    for (auto& p : g_paths) p = GifPath{};
}

// What the display circuits show at a vsync: the privileged registers, CSR
// (its FIELD bit) and the vsync count, which tells consecutive fields apart.
struct Display {
    uint64_t priv[0x10];
    uint64_t csr;
    uint64_t fieldCount;
};
Display displayNow() {
    Display d{};
    std::memcpy(d.priv, g_priv, sizeof g_priv);
    d.csr = g_csr.load();
    d.fieldCount = g_fieldCount;
    return d;
}
bool buildFrame(const Display& d, Frame& out);

// ---- PATH1 from the VU1 thread ----------------------------------------------------------
// With VU1 on a thread of its own, its XGKICK packets come in a ring of their
// own, then each program's end. The GS thread decodes a program's packets
// where the guest queued the program in its own ring (a Path1 entry), as they
// come: PATH1 keeps its place among the paths, and a program that kicks more
// than the ring holds streams through it.
enum Path1Kind : uint32_t { Path1Data, Path1End };
WordRing g_path1{uint64_t(1) << 21};   // 16 MB; before g_gsThread, which uses it: destroyed after it

// GS thread: what program `program` kicks, up to its end.
void decodePath1(uint64_t program) {
    WordRing::Entry e;
    while (g_path1.next(e)) {
        if (e.kind == Path1End) {
            g_path1.done(e);
            if (e.arg != program) {
                PS2_ERROR("gs", "PATH1 from the VU1 thread: program %llu ended where the GS expected program %llu",
                          static_cast<unsigned long long>(e.arg), static_cast<unsigned long long>(program));
                std::abort();
            }
            return;
        }
        for (uint64_t i = 0; i < e.qwords; ++i) gifQword(g_paths[1], g_path1.qword(e, i));
        g_path1.done(e);
        g_drawThreads.publish();
    }
}

// ---- the GS thread --------------------------------------------------------------------
// The guest thread queues GIF data, and the commands that act on GIF state,
// in a ring of 64-bit words; the GS thread runs them in order. An entry is a
// header (kind, path and payload quadwords; the guest's rounding mode, which
// the drawing inherits) and its payload. Entries are whole quadwords, so a
// quadword never wraps round the end of the ring. A VU1 program on the VU1
// thread is a Path1 entry (its number), where the GS thread decodes the
// packets the program kicks.
class GsThread {
public:
    enum Kind : uint64_t { Gif, ResetPaths, ResetGs, Vsync, Finish, Path1 };

    ~GsThread() { stop(false); }

    void push(Kind kind, int path, const uint64_t* qwords, size_t count) {
        do {   // a transfer larger than a sixteenth of the ring goes in parts
            const size_t n = std::min(count, kMaxQwords);
            put(kind, path, qwords, n);
            qwords += 2 * n;
            count -= n;
        } while (count);
    }

    // Queues a vsync's picture, once the GS thread has shown the one two
    // vsyncs before it: it stays at most two vsyncs behind.
    void vsync(const Display& d, const video::Present& how) {
        uint64_t& before = vsyncEnd_[vsyncs_++ & 1];
        waitFor(before);
        const VsyncEntry v{d, how};
        uint64_t q[2 * kVsyncQwords] = {};
        std::memcpy(q, &v, sizeof v);
        put(Vsync, 0, q, kVsyncQwords);
        before = head_.load(std::memory_order_relaxed);
    }

    // Waits until the GS thread has run everything queued, and the draw threads drawn it.
    void flush() {
        if (!thread_.joinable()) return;
        put(Finish, 0, nullptr, 0);
        waitFor(head_.load(std::memory_order_relaxed));
    }

    // finish: run what is queued first, else drop it (and stop waiting for a
    // VU1 program's packets).
    void stop(bool finish) {
        if (!thread_.joinable()) return;
        if (finish) flush();
        else g_path1.close();
        stopping_ = true;
        wake_.fetch_add(1);
        wake_.notify_one();
        thread_.join();
        stopping_ = false;
        tail_ = head_.load();
    }

private:
    static constexpr uint64_t kWords = uint64_t(1) << 22;   // 32 MB
    static constexpr uint64_t kMask = kWords - 1;
    static constexpr size_t kMaxQwords = kWords / 32;
    struct VsyncEntry {
        Display display;
        video::Present how;
    };
    static constexpr size_t kVsyncQwords = (sizeof(VsyncEntry) + 15) / 16;

    void put(Kind kind, int path, const uint64_t* payload, size_t qwords) {
        if (!thread_.joinable()) start();
        const uint64_t at = head_.load(std::memory_order_relaxed), words = 2 + 2 * qwords;
        if (at + words > kWords) waitFor(at + words - kWords);   // room the GS thread has freed
        ring_[at & kMask] = kind | static_cast<uint64_t>(path) << 8 | static_cast<uint64_t>(qwords) << 32;
        ring_[(at + 1) & kMask] = static_cast<uint32_t>(std::fegetround());
        const uint64_t start = (at + 2) & kMask, first = std::min<uint64_t>(2 * qwords, kWords - start);
        if (first) std::memcpy(&ring_[start], payload, first * 8);
        if (2 * qwords > first) std::memcpy(&ring_[0], payload + first, (2 * qwords - first) * 8);
        head_.store(at + words);
        if (idle_.load()) {
            wake_.fetch_add(1);
            wake_.notify_one();
        }
    }

    // Guest thread: until the GS thread has run everything before word `pos`.
    void waitFor(uint64_t pos) {
        while (tail_.load() < pos) {
            waitPos_ = pos;
            const uint64_t done = tail_.load();
            if (done < pos) tail_.wait(done);
            waitPos_ = UINT64_MAX;
        }
    }

    void start() {
        if (!ring_) ring_.reset(new uint64_t[kWords]);
        thread_ = std::thread([this] { work(); });
    }

    void work() {
        int rounding = std::fegetround();
        for (;;) {
            const uint32_t seen = wake_.load();
            if (stopping_) return;
            uint64_t at = tail_.load(std::memory_order_relaxed);
            const uint64_t end = head_.load();
            if (at == end) {
                idle_ = true;
                if (head_.load() == at && !stopping_) wake_.wait(seen);
                idle_ = false;
                continue;
            }
            while (at != end && !stopping_) {
                const uint64_t h = ring_[at & kMask];
                const auto mode = static_cast<int>(ring_[(at + 1) & kMask]);
                if (mode != rounding) std::fesetround(rounding = mode);
                run(static_cast<Kind>(h & 0xFF), static_cast<int>((h >> 8) & 3), at + 2, h >> 32);
                at += 2 + 2 * (h >> 32);
                tail_.store(at);
                if (at >= waitPos_) tail_.notify_one();
            }
        }
    }

    void run(Kind kind, int path, uint64_t data, uint64_t qwords) {
        switch (kind) {
        case Gif:
            for (uint64_t i = 0; i < qwords; ++i) gifQword(g_paths[path], &ring_[(data + 2 * i) & kMask]);
            g_drawThreads.publish();
            break;
        case ResetPaths: resetPaths(); break;
        case ResetGs:
            g_queueCount = 0;
            resetPaths();
            break;
        case Vsync: {
            uint64_t q[2 * kVsyncQwords];
            for (size_t i = 0; i < 2 * kVsyncQwords; ++i) q[i] = ring_[(data + i) & kMask];
            VsyncEntry v;
            std::memcpy(&v, q, sizeof v);
            Frame f;
            if (v.how.dump || v.how.show) {
                g_drawThreads.finish();
                if (buildFrame(v.display, f)) video::present(std::move(f), v.how);
            }
            break;
        }
        case Finish: g_drawThreads.finish(); break;
        case Path1: decodePath1(ring_[data & kMask]); break;
        }
    }

    std::unique_ptr<uint64_t[]> ring_;
    std::thread thread_;
    std::atomic<uint64_t> head_{0};       // words queued
    std::atomic<uint64_t> tail_{0};       // words run
    std::atomic<uint32_t> wake_{0};       // moves to wake the GS thread
    std::atomic<bool> idle_{false};       // the GS thread waits for work
    std::atomic<uint64_t> waitPos_{UINT64_MAX};   // the guest thread waits for tail_ to reach it
    std::atomic<bool> stopping_{false};
    uint64_t vsyncEnd_[2] = {0, 0};       // where the last two vsyncs' entries end
    uint64_t vsyncs_ = 0;
};
GsThread g_gsThread;   // after g_drawThreads, which it uses: destroyed first

// The GS thread runs when it is wanted and more than one thread draws.
bool g_wantOwnThread = false;

// Guest thread: waits until everything sent so far is drawn.
void drawn() {
    if (g_async) g_gsThread.flush();
    else g_drawThreads.finish();
}
// VU1's thread runs only beside the GS thread, which decodes what it kicks:
// it stops first, while the GS thread still takes its packets, and starts last.
void updateMode() {
    drawn();
    const bool async = g_wantOwnThread && g_drawThreads.count() > 1;
    if (async == g_async) return;
    if (!async) vu::updateOwnThread(false);
    g_gsThread.stop(true);
    g_async = async;
    if (async) vu::updateOwnThread(true);
}

} // namespace

// ===========================================================================
// Public interface
// ===========================================================================
void init() {
    drawn();
    std::memset(g_vram, 0, sizeof g_vram);
    std::memset(g_regs, 0, sizeof g_regs);
    std::memset(g_priv, 0, sizeof g_priv);
    g_csr = 0x551B4000ull;
    g_imr = 0x7F00;
    g_events = 0;
    g_history = FieldHistory{};
    g_drawnFor.fill(-1);
    g_drawnShown = {};
    g_drawDirty = true;
    g_queueCount = 0;
    resetPaths();
    resetClut();
    g_regs[PRMODECONT] = 1;
}

void setOwnThread(bool on) {
    g_wantOwnThread = on;
    updateMode();
}

bool ownThread() { return g_async; }

void shutdown() {
    vu::updateOwnThread(false);
    g_gsThread.stop(true);
}

void path1Run(uint64_t program) {
    const uint64_t q[2] = {program, 0};
    g_gsThread.push(GsThread::Path1, 1, q, 1);
}

void path1Kick(const uint64_t* qwords, size_t count) {
    while (count) {   // in parts the ring takes
        const size_t n = std::min<size_t>(count, g_path1.maxQwords());
        g_path1.put(Path1Data, 0, qwords, n);
        qwords += 2 * n;
        count -= n;
    }
}

void path1End(uint64_t program) { g_path1.put(Path1End, program, nullptr, 0); }

void path1Close() { g_path1.close(); }

uint64_t csr() { return g_csr; }
uint64_t imr() { return g_imr; }
uint64_t putImr(uint64_t v) {
    const uint64_t old = g_imr;
    g_imr = v;
    updateGsInterrupt();
    return old;
}

void pollEvents() {
    if (!g_events.load(std::memory_order_relaxed)) return;
    const uint64_t raised = g_events.exchange(0);
    if ((raised | g_csr.load()) & 0x1F & ~((g_imr >> 8) & 0x1F)) hw::raiseIntc(hw::INTC_GS);
}

uint64_t privRead(uint32_t addr) {
    if (addr >= 0x12001000u) {
        switch (addr & 0xFF0) {
        case 0x000: return g_csr | (1ull << 2);   // HSINT always pending-looking (no hsync interrupts are generated)
        case 0x010: return g_imr;
        case 0x080: return g_siglblid;
        default: return 0;
        }
    }
    return g_priv[(addr >> 4) & 0xF];
}

void privWrite(uint32_t addr, uint64_t v) {
    if (addr >= 0x12001000u) {
        switch (addr & 0xFF0) {
        case 0x000:   // CSR: event bits are write-1-to-clear; RESET resets the GS
            g_csr &= ~(v & 0x1F);
            if (v & 0x200) {
                if (g_async) {
                    g_gsThread.push(GsThread::ResetGs, 0, nullptr, 0);
                } else {
                    g_queueCount = 0;
                    resetPaths();
                }
            }
            break;
        case 0x010: g_imr = v; updateGsInterrupt(); break;
        case 0x080: g_siglblid = v; break;
        default: break;
        }
        return;
    }
    const uint32_t reg = (addr >> 4) & 0xF;
    // Display set-up changes, not the page flips of double buffering (DISPFB's FBP).
    const uint64_t ignored = (reg == 0x7 || reg == 0x9) ? 0x1FFull : 0;
    if (((g_priv[reg] ^ v) & ~ignored) != 0)
        PS2_DEBUG("gs", "%s = 0x%016llx", kPrivName[reg], static_cast<unsigned long long>(v));
    g_priv[reg] = v;
}

void privWrite32(uint32_t addr, uint32_t v) {
    const uint32_t a = addr & ~7u;
    uint64_t cur = privRead(a);
    if (a >= 0x12001000u && (a & 0xFF0) == 0) cur = 0;   // CSR write: only the written bits act
    if (addr & 4) cur = (cur & 0xFFFFFFFFull) | (static_cast<uint64_t>(v) << 32);
    else cur = (cur & 0xFFFFFFFF00000000ull) | v;
    privWrite(a, cur);
}

void onVblankStart() {
    g_csr |= 8;             // VSINT
    g_csr ^= 1ull << 13;    // FIELD toggles every field
    ++g_fieldCount;
    updateGsInterrupt();
    const video::Present how = video::onVsync();
    if (g_async) return g_gsThread.vsync(displayNow(), how);   // also when nothing shows: it paces the GS thread
    if (!how.dump && !how.show) return;
    g_drawThreads.finish();
    Frame f;
    if (buildFrame(displayNow(), f)) video::present(std::move(f), how);
}

void onVblankEnd() {}

void setVideoMode(bool, bool) {}

uint32_t gifRegRead(uint32_t addr) {
    switch (addr) {
    case 0x10003020u: return g_path3Masked ? 1u : 0u;   // GIF_STAT: idle, M3R
    default: return 0;
    }
}

void gifRegWrite(uint32_t addr, uint32_t v) {
    if (addr == 0x10003000u && (v & 1)) {   // GIF_CTRL.RST
        if (g_async) g_gsThread.push(GsThread::ResetPaths, 0, nullptr, 0);
        else resetPaths();
    }
    if (addr == 0x10003010u) g_path3Masked = v & 1;
}

void gifFifoWrite(const uint64_t q[2]) {
    if (g_async) return g_gsThread.push(GsThread::Gif, 3, q, 1);
    gifQword(g_paths[3], q);
    g_drawThreads.publish();
}

void gifTransfer(const uint64_t* qwords, size_t count, int path) {
    if (g_async) return g_gsThread.push(GsThread::Gif, path & 3, qwords, count);
    GifPath& p = g_paths[path & 3];
    for (size_t i = 0; i < count; ++i) gifQword(p, qwords + 2 * i);
    g_drawThreads.publish();
}

void setPath3Masked(bool masked) { g_path3Masked = masked; }

uint32_t readPixel32(uint32_t fbp, uint32_t fbw, uint32_t psm, int x, int y) {
    drawn();
    return pixel32(fbp, fbw, psm, x, y);
}

uint64_t primitivesDrawnAlone() {
    drawn();
    return g_drawnAlone.load(std::memory_order_relaxed);
}

uint32_t debugAddress(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    switch (psm) {
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return addr16(bp, bw, x, y, (psm & 0x08) != 0, isZ(psm));
    case PSMT8: return addr8(bp, bw, x, y);
    case PSMT4: return addr4(bp, bw, x, y);
    default: return addr32(bp, bw, x, y, isZ(psm));
    }
}

namespace {

// A CRTC read-out circuit: where its picture sits on screen (DISPLAY, in video
// clocks and lines) and where it is read from (DISPFB).
struct Circuit {
    uint32_t fbp = 0, fbw = 0, psm = 0;
    int dbx = 0, dby = 0;
    int dx = 0, dy = 0, magh = 1, magv = 1;
    int width = 0, height = 0;   // video clocks and lines
    bool valid = false;
};

Circuit readCircuit(const Display& d, int i) {
    const uint64_t dispfb = d.priv[i == 0 ? 0x7 : 0x9], display = d.priv[i == 0 ? 0x8 : 0xA];
    Circuit c;
    c.fbp = static_cast<uint32_t>(field(dispfb, 0, 9));
    c.fbw = static_cast<uint32_t>(field(dispfb, 9, 6));
    c.psm = static_cast<uint32_t>(field(dispfb, 15, 5));
    c.dbx = static_cast<int>(field(dispfb, 32, 11));
    c.dby = static_cast<int>(field(dispfb, 43, 11));
    c.dx = static_cast<int>(field(display, 0, 12));
    c.dy = static_cast<int>(field(display, 12, 11));
    c.magh = static_cast<int>(field(display, 23, 4)) + 1;
    c.magv = static_cast<int>(field(display, 27, 2)) + 1;
    c.width = static_cast<int>(field(display, 32, 12)) + 1;
    c.height = static_cast<int>(field(display, 44, 11)) + 1;
    c.valid = c.width / c.magh > 0 && c.width / c.magh <= 2048 && c.height / c.magv <= 2048;
    return c;
}

// ---- Deinterlacing -----------------------------------------------------------
//
// Adaptive deinterlacing, as PCSX2's adaptive mode does it: a line the field
// lacks comes from the field before (woven) where the picture is still, and
// halfway between the field's lines above and below (interpolated) where it
// moves. It moves where a channel changed by more than kMotion: in the lines
// above or below since the field two back, or in the woven line itself since
// the field three back. A smaller change keeps the woven line if that line did
// not change (without the field three back: if the lines around it did not),
// or if it fits between the lines above and below. A line that stands out
// from both is the comb of something moving slowly, and is interpolated.

constexpr int kMotion = 16;   // a channel changing by more than this is motion
constexpr int kOutside = 2;   // how far a woven line may stand out from the lines around it

// The largest change of a colour channel between two pixels.
int change(uint32_t a, uint32_t b) {
    int m = 0;
    for (int s = 0; s < 24; s += 8) {
        const int d = static_cast<int>((a >> s) & 0xFF) - static_cast<int>((b >> s) & 0xFF);
        m = std::max(m, d < 0 ? -d : d);
    }
    return m;
}

// How far a channel of `c` lies outside the range of `a`'s and `b`'s, at
// most: above kOutside, a line that does not fit between its neighbours.
int outside(uint32_t a, uint32_t b, uint32_t c) {
    int m = 0;
    for (int s = 0; s < 24; s += 8) {
        const int va = static_cast<int>((a >> s) & 0xFF), vb = static_cast<int>((b >> s) & 0xFF);
        const int vc = static_cast<int>((c >> s) & 0xFF);
        m = std::max(m, std::max(vc - std::max(va, vb), std::min(va, vb) - vc));
    }
    return m;
}

uint32_t average(uint32_t a, uint32_t b) {
    uint32_t px = 0xFF000000u;
    for (int s = 0; s < 24; s += 8) px |= ((((a >> s) & 0xFF) + ((b >> s) & 0xFF) + 1) / 2) << s;
    return px;
}

// Fills `out` (w x h) from the current field `cur` (`lines` lines, on the
// display lines of parity `odd`, shown at vsync `when`) and the fields before it.
void deinterlaceField(Frame& out, std::vector<uint32_t>&& cur, int w, int h, int lines, int odd, uint64_t when) {
    FieldHistory& hist = g_history;
    if (hist.width != w || hist.lines != lines) hist = FieldHistory{w, lines, {}, 0, {}};
    const Deinterlace mode = g_deinterlace;
    const bool adaptive = mode == Deinterlace::Adaptive;
    // The field the last vsync showed, again: a game late with its next
    // picture shows the same one twice. Adaptive shows the same picture, not
    // that field moved to the other lines.
    ShownField& last = hist.fields[static_cast<size_t>(hist.newest)][0];
    const bool lastShown = last.valid && when <= last.when + 1 && hist.picture.size() == out.pixels.size();
    if (adaptive && lastShown && last.pixels == cur) {
        last.when = when;
        std::copy(hist.picture.begin(), hist.picture.end(), out.pixels.begin());
        return;
    }
    // The newest field on the other lines, if the last vsync showed it (the
    // field one back), the newest on these lines before that one (two back),
    // and the one before it on the other lines (three back).
    const ShownField& prev = hist.fields[static_cast<size_t>(odd ^ 1)][0];
    const ShownField& prev2 = hist.fields[static_cast<size_t>(odd)][0];
    const ShownField& prev3 = hist.fields[static_cast<size_t>(odd ^ 1)][1];
    const bool havePrev = prev.valid && prev.when + 1 == when;
    const bool havePrev2 = havePrev && prev2.valid && prev2.when < prev.when;
    const bool havePrev3 = havePrev2 && prev3.valid && prev3.when < prev2.when;
    // A new field on the lines the last vsync showed one on (a late picture,
    // drawn for the field before): where it matches that field, the last
    // picture's other lines stay, by the same rules as a woven line.
    const bool again = adaptive && !havePrev && lastShown && hist.newest == odd;
    const auto row = [&](const std::vector<uint32_t>& f, int k) {
        return f.data() + static_cast<size_t>(std::clamp(k, 0, lines - 1)) * static_cast<size_t>(w);
    };
    for (int y = 0; y < h; ++y) {
        uint32_t* o = out.pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        if ((y & 1) == odd) {
            std::copy_n(row(cur, (y - odd) / 2), w, o);
            continue;
        }
        // A line this field lacks: between its lines above and below.
        const int above = y > 0 ? (y - 1 - odd) / 2 : (y + 1 - odd) / 2;
        const int below = y + 1 < h ? (y + 1 - odd) / 2 : above;
        const uint32_t* a = row(cur, above);
        const uint32_t* b = row(cur, below);
        if (again) {
            const uint32_t* la = row(last.pixels, above);
            const uint32_t* lb = row(last.pixels, below);
            const uint32_t* p = hist.picture.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
            for (int x = 0; x < w; ++x) {
                const int ma = change(a[x], la[x]), mb = change(b[x], lb[x]);
                const int moving = std::max(ma, mb) > kMotion;
                const int changed = (ma | mb) != 0;
                const int stray = outside(a[x], b[x], p[x]) > kOutside;
                o[x] = moving | (changed & stray) ? average(a[x], b[x]) : p[x];
            }
            continue;
        }
        if (mode == Deinterlace::Bob || !havePrev) {
            std::copy_n(a, w, o);
            continue;
        }
        const int k = (y - (1 - odd)) / 2;
        const uint32_t* p = row(prev.pixels, k);
        if (mode == Deinterlace::Weave) {
            std::copy_n(p, w, o);
            continue;
        }
        // Adaptive: without the field two back, nothing is known to be still.
        if (!havePrev2) {
            for (int x = 0; x < w; ++x) o[x] = average(a[x], b[x]);
            continue;
        }
        const uint32_t* pa = row(prev2.pixels, above);
        const uint32_t* pb = row(prev2.pixels, below);
        const uint32_t* p3 = havePrev3 ? row(prev3.pixels, k) : p;   // without it, the woven line counts as unchanged
        for (int x = 0; x < w; ++x) {
            const int ma = change(a[x], pa[x]), mb = change(b[x], pb[x]), mc = change(p[x], p3[x]);
            const int moving = std::max(std::max(ma, mb), mc) > kMotion;
            const int changed = havePrev3 ? mc != 0 : (ma | mb) != 0;
            const int stray = outside(a[x], b[x], p[x]) > kOutside;
            o[x] = moving | (changed & stray) ? average(a[x], b[x]) : p[x];
        }
    }
    auto& fields = hist.fields[static_cast<size_t>(odd)];
    fields[1] = std::move(fields[0]);
    fields[0] = ShownField{std::move(cur), when, true};
    hist.newest = odd;
    hist.picture = out.pixels;
}

// The lines a field goes on: the odd ones, a line lower, for the odd field
// (CSR.FIELD). Adaptive goes by the field the game drew the picture for
// instead, once it has seen the game draw for both within a second: a game
// late with a picture shows it a vsync later, in the other field, where by
// FIELD it would jump a line.
int fieldLines(const Display& disp, const Circuit* c1, const Circuit* c2) {
    const int field = static_cast<int>((disp.csr >> 13) & 1);
    if (g_deinterlace != Deinterlace::Adaptive) return field;
    int drawn = -1;
    for (const Circuit* c : {c1, c2}) {
        if (!c) continue;
        const int d = g_drawnFor[c->fbp & 511];
        if (d < 0 || (drawn >= 0 && d != drawn)) return field;
        drawn = d;
    }
    if (drawn < 0) return field;
    g_drawnShown[static_cast<size_t>(drawn)] = disp.fieldCount + 1;
    const uint64_t other = g_drawnShown[static_cast<size_t>(drawn ^ 1)];
    return other != 0 && disp.fieldCount + 1 - other <= 50 ? drawn : field;
}

// The circuit's pixel at a screen position, if it covers it. Interlaced FRAME
// mode (FFMD) shows one field per vblank: half the lines, read one after the
// other, each covering two display lines, the odd field one line lower.
bool circuitPixel(const Circuit& c, int clock, int line, bool perField, int odd, uint32_t& px) {
    if (clock < c.dx || line < c.dy || clock >= c.dx + c.width || line >= c.dy + c.height) return false;
    const int x = (clock - c.dx) / c.magh;
    int y = (line - c.dy) / c.magv;
    if (perField) y = std::clamp((y - odd) / 2, 0, std::max(c.height / c.magv / 2, 1) - 1);
    px = pixel32(c.fbp, c.fbw, c.psm, c.dbx + x, c.dby + y);
    return true;
}

bool buildFrame(const Display& disp, Frame& out) {
    const uint64_t pmode = disp.priv[0x0], smode2 = disp.priv[0x2];
    const Circuit c1 = readCircuit(disp, 0), c2 = readCircuit(disp, 1);
    const bool en1 = (pmode & 1) && c1.valid, en2 = (pmode & 2) && c2.valid;
    if (!en1 && !en2) return false;
    const bool perField = (smode2 & 1) && (smode2 & 2);
    const int odd = perField ? fieldLines(disp, en1 ? &c1 : nullptr, en2 ? &c2 : nullptr) : 0;
    // The screen covers both pictures when both circuits show; a pixel is the
    // narrower circuit's.
    int x0 = 1 << 20, y0 = 1 << 20, x1 = 0, y1 = 0, clocks = 16;
    for (const Circuit* c : {en1 ? &c1 : nullptr, en2 ? &c2 : nullptr}) {
        if (!c) continue;
        x0 = std::min(x0, c->dx);
        y0 = std::min(y0, c->dy);
        x1 = std::max(x1, c->dx + c->width);
        y1 = std::max(y1, c->dy + c->height);
        clocks = std::min(clocks, c->magh);
    }
    const int w = (x1 - x0) / clocks, h = y1 - y0;
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return false;
    // Circuit 1 blends over circuit 2, or over the background colour with SLBG
    // (how games fade), by its pixels' alpha (0x80 = opaque) or by ALP (MMOD).
    // Circuit 1 alone without SLBG, and circuit 2 alone, show as they are.
    const bool mmod = (pmode >> 5) & 1, slbg = (pmode >> 7) & 1;
    const auto alp = static_cast<uint32_t>(field(pmode, 8, 8));
    const uint32_t bgcolor = static_cast<uint32_t>(disp.priv[0xE]) & 0xFFFFFFu;
    const bool blend = en1 && (en2 || slbg);
    const auto pixel = [&](int x, int line) {
        const int clock = x0 + x * clocks;
        uint32_t p1 = 0, p2 = 0, px = 0;
        const bool has1 = en1 && circuitPixel(c1, clock, line, perField, odd, p1);
        const bool has2 = en2 && circuitPixel(c2, clock, line, perField, odd, p2);
        if (!blend) {
            px = has1 ? p1 : p2;
        } else {
            const uint32_t bg = slbg ? bgcolor : (has2 ? p2 : 0);
            const uint32_t a = !has1 ? 0 : mmod ? alp : std::min<uint32_t>((p1 >> 24) * 2, 255);
            for (int shift = 0; shift < 24; shift += 8) {
                const uint32_t s = (p1 >> shift) & 0xFF, d = (bg >> shift) & 0xFF;
                px |= ((s * a + d * (255 - a)) / 255) << shift;
            }
        }
        return px | 0xFF000000u;
    };
    out.width = w;
    out.height = h;
    out.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    if (!perField) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) out.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = pixel(x, y0 + y);
        return true;
    }
    // FIELD mode: this field's lines, then the deinterlaced picture.
    const int lines = (h + 1) / 2;
    std::vector<uint32_t> cur(static_cast<size_t>(w) * static_cast<size_t>(lines));
    for (int k = 0; k < lines; ++k) {
        const int line = y0 + std::min(2 * k + odd, h - 1);
        for (int x = 0; x < w; ++x) cur[static_cast<size_t>(k) * static_cast<size_t>(w) + static_cast<size_t>(x)] = pixel(x, line);
    }
    deinterlaceField(out, std::move(cur), w, h, lines, odd, disp.fieldCount);
    return true;
}

} // namespace

bool currentFrame(Frame& out) {
    drawn();
    return buildFrame(displayNow(), out);
}

void setDeinterlace(Deinterlace mode) { g_deinterlace = mode; }
Deinterlace deinterlace() { return g_deinterlace; }

const char* deinterlaceName(Deinterlace mode) {
    switch (mode) {
    case Deinterlace::Adaptive: return "Adaptive";
    case Deinterlace::Weave: return "Weave";
    case Deinterlace::Bob: return "Bob";
    default: return "?";
    }
}

void setDrawThreads(int n) {
    drawn();
    g_drawThreads.setCount(n);
    updateMode();
}

} // namespace ps2::gs
