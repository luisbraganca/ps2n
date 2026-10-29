#include "hw/gs.h"

#include "hw/hw.h"
#include "kernel/kernel.h"
#include "log.h"
#include "video/video.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

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

// Raw read: the format's own bits (32/24/16-bit colour, Z, or a CLUT index).
uint32_t readRaw(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y) {
    switch (psm) {
    case PSMCT32: case PSMZ32: return g_vram[addr32(bp, bw, x, y, isZ(psm))];
    case PSMCT24: case PSMZ24: return g_vram[addr32(bp, bw, x, y, isZ(psm))] & 0xFFFFFF;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: {
        uint16_t h;
        std::memcpy(&h, vramBytes() + 2 * addr16(bp, bw, x, y, (psm & 0x08) != 0, isZ(psm)), 2);
        return h;
    }
    case PSMT8: return vramBytes()[addr8(bp, bw, x, y)];
    case PSMT4: {
        const uint32_t n = addr4(bp, bw, x, y);
        return (vramBytes()[n >> 1] >> ((n & 1) * 4)) & 0xF;
    }
    case PSMT8H: return g_vram[addr32(bp, bw, x, y, false)] >> 24;
    case PSMT4HL: return (g_vram[addr32(bp, bw, x, y, false)] >> 24) & 0xF;
    case PSMT4HH: return g_vram[addr32(bp, bw, x, y, false)] >> 28;
    default: return 0;
    }
}

void writeRaw(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t v) {
    switch (psm) {
    case PSMCT32: case PSMZ32: g_vram[addr32(bp, bw, x, y, isZ(psm))] = v; return;
    case PSMCT24: case PSMZ24: {
        uint32_t& w = g_vram[addr32(bp, bw, x, y, isZ(psm))];
        w = (w & 0xFF000000u) | (v & 0xFFFFFF);
        return;
    }
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: {
        const auto h = static_cast<uint16_t>(v);
        std::memcpy(vramBytes() + 2 * addr16(bp, bw, x, y, (psm & 0x08) != 0, isZ(psm)), &h, 2);
        return;
    }
    case PSMT8: vramBytes()[addr8(bp, bw, x, y)] = static_cast<uint8_t>(v); return;
    case PSMT4: {
        const uint32_t n = addr4(bp, bw, x, y);
        uint8_t& b = vramBytes()[n >> 1];
        b = (n & 1) ? static_cast<uint8_t>((b & 0x0F) | ((v & 0xF) << 4)) : static_cast<uint8_t>((b & 0xF0) | (v & 0xF));
        return;
    }
    case PSMT8H: { uint32_t& w = g_vram[addr32(bp, bw, x, y, false)]; w = (w & 0x00FFFFFFu) | (v << 24); return; }
    case PSMT4HL: { uint32_t& w = g_vram[addr32(bp, bw, x, y, false)]; w = (w & 0xF0FFFFFFu) | ((v & 0xF) << 24); return; }
    case PSMT4HH: { uint32_t& w = g_vram[addr32(bp, bw, x, y, false)]; w = (w & 0x0FFFFFFFu) | ((v & 0xF) << 28); return; }
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

// 16-bit 1:5:5:5 colour -> 32-bit RGBA (alpha per TEXA).
uint32_t expand16(uint32_t c, uint32_t ta0 = 0, uint32_t ta1 = 0x80, bool aem = false) {
    const uint32_t r = (c & 0x1F) << 3, g = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
    uint32_t a = (c & 0x8000) ? ta1 : ta0;
    if (aem && (c & 0x7FFF) == 0) a = 0;
    return r | (g << 8) | (b << 16) | (a << 24);
}
uint32_t pack16(uint32_t c) {
    return ((c >> 3) & 0x1F) | (((c >> 11) & 0x1F) << 5) | (((c >> 19) & 0x1F) << 10) | ((c >> 31) << 15);
}

// ===========================================================================
// Registers and state
// ===========================================================================
uint64_t g_regs[0x100];
uint64_t g_priv[0x10];   // PMODE ... BGCOLOR, indexed by (addr >> 4) & 0xF
constexpr const char* kPrivName[0x10] = {"PMODE", "SMODE1", "SMODE2", "SRFSH", "SYNCH1", "SYNCH2", "SYNCV", "DISPFB1",
                                         "DISPLAY1", "DISPFB2", "DISPLAY2", "EXTBUF", "EXTDATA", "EXTWRITE", "BGCOLOR", "0xF0"};
uint64_t g_csr = 0x551B4000ull;   // FIFO empty, REV 0x1B, ID 0x55
uint64_t g_imr = 0x7F00;
uint64_t g_siglblid = 0;
bool g_path3Masked = false;

// Deinterlacing (FIELD mode): the mode, the vsyncs so far (which tell
// consecutive fields apart) and the two fields shown before this one, newest first.
std::atomic<Deinterlace> g_deinterlace{Deinterlace::Adaptive};
uint64_t g_fieldCount = 0;
struct ShownField {
    std::vector<uint32_t> pixels;
    int odd = 0;
    uint64_t when = 0;
    bool valid = false;
};
struct FieldHistory {
    int width = 0, lines = 0;
    std::array<ShownField, 2> fields;
};
FieldHistory g_history;

enum Reg : uint32_t {
    PRIM = 0x00, RGBAQ = 0x01, ST = 0x02, UV = 0x03, XYZF2 = 0x04, XYZ2 = 0x05, TEX0_1 = 0x06, TEX0_2 = 0x07,
    CLAMP_1 = 0x08, CLAMP_2 = 0x09, FOG = 0x0A, XYZF3 = 0x0C, XYZ3 = 0x0D, TEX1_1 = 0x14, TEX1_2 = 0x15,
    TEX2_1 = 0x16, TEX2_2 = 0x17, XYOFFSET_1 = 0x18, XYOFFSET_2 = 0x19, PRMODECONT = 0x1A, PRMODE = 0x1B,
    TEXCLUT = 0x1C, SCANMSK = 0x22, TEXA = 0x3B, FOGCOL = 0x3D, TEXFLUSH = 0x3F, SCISSOR_1 = 0x40,
    SCISSOR_2 = 0x41, ALPHA_1 = 0x42, ALPHA_2 = 0x43, DIMX = 0x44, DTHE = 0x45, COLCLAMP = 0x46, TEST_1 = 0x47,
    TEST_2 = 0x48, PABE = 0x49, FBA_1 = 0x4A, FBA_2 = 0x4B, FRAME_1 = 0x4C, FRAME_2 = 0x4D, ZBUF_1 = 0x4E,
    ZBUF_2 = 0x4F, BITBLTBUF = 0x50, TRXPOS = 0x51, TRXREG = 0x52, TRXDIR = 0x53, HWREG = 0x54, SIGNAL = 0x60,
    FINISH = 0x61, LABEL = 0x62,
};

constexpr uint64_t field(uint64_t v, int lo, int bits) { return (v >> lo) & ((1ull << bits) - 1); }

void updateGsInterrupt() {
    // CSR bits 0-4 (SIGNAL FINISH HSINT VSINT EDWINT) vs IMR bits 8-12.
    const uint64_t pending = g_csr & 0x1F & ~((g_imr >> 8) & 0x1F);
    if (pending) hw::raiseIntc(hw::INTC_GS);
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

// ---- image transfer -------------------------------------------------------------
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
    writeRaw(t.dbp, t.dbw, t.dpsm, (t.dsax + t.x) & 2047, (t.dsay + t.y) & 2047, v);
    if (++t.x >= t.rrw) {
        t.x = 0;
        if (++t.y >= t.rrh) t.active = false;
    }
}

void transferData(uint64_t data) {
    Transfer& t = g_xfer;
    if (!t.active) return;
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
                writeRaw(dbp, dbw, dpsm, dsax + x, dsay + y, readRaw(sbp, sbw, spsm, ssax + x, ssay + y));
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
    uint64_t test, alpha, tex0, clamp;
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

// What a primitive's pixels need, decoded from the registers when they
// change rather than for every pixel.
struct Draw {
    Env e;
    bool tex, fst, gouraud;              // PRIM: TME, FST, IIP
    uint32_t tbp, tbw, tpsm;             // TEX0
    int tw, th;                          // texture size, texels
    float twf, thf;
    uint32_t tfx;                        // modulate, decal, highlight, highlight2
    bool tcc;
    uint32_t wms, wmt;                   // CLAMP
    int minu, maxu, minv, maxv;
    uint32_t ta0, ta1;                   // TEXA
    bool aem;
    uint32_t cbp, cpsm, csa;             // the CLUT
    bool ate;                            // alpha test
    uint32_t atst, aref, afail;
    bool zte;                            // depth test
    uint32_t ztst, zmax;
    bool fge;                            // fog
    uint32_t fogr, fogg, fogb;
    bool abe, pabe, colclamp;            // blending
    uint64_t ba, bb, bc, bd;
    uint32_t fix;
    bool fb16;                           // a 16-bit frame buffer, and its mask
    uint32_t fbmsk16;
};
Draw g_draw;
bool g_drawDirty = true;   // a drawing register changed since g_draw was decoded

uint32_t clutLookup(const Draw& d, uint32_t index) {
    const uint32_t psm = d.tpsm, cbp = d.cbp, cpsm = d.cpsm, csa = d.csa;
    uint32_t x, y;
    if (psm == PSMT8 || psm == PSMT8H) {
        const uint32_t i = (index & 0xE7) | ((index & 0x08) << 1) | ((index & 0x10) >> 1);   // CSM1 order
        x = i & 15;
        y = i >> 4;
    } else {
        const uint32_t i = (index & 15) + (cpsm == PSMCT32 ? (csa & 15) * 16 : csa * 16);
        x = i & 7;
        y = (i >> 3) & 1;
        if (i >= 16) {
            const uint32_t j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
            x = j & 15;
            y = j >> 4;
        }
    }
    if (cpsm == PSMCT32 || cpsm == PSMCT24) return readRaw(cbp, 1, PSMCT32, x, y);
    return expand16(readRaw(cbp, 1, cpsm == PSMCT16S ? PSMCT16S : PSMCT16, x, y));
}

uint32_t wrapCoord(int c, int size, uint32_t mode, int minv, int maxv) {
    switch (mode) {
    case 0: return static_cast<uint32_t>(c & (size - 1));                                  // REPEAT
    case 1: return static_cast<uint32_t>(std::clamp(c, 0, size - 1));                      // CLAMP
    case 2: return static_cast<uint32_t>(std::clamp(c, minv, maxv));                       // REGION_CLAMP
    default: return static_cast<uint32_t>((c & minv) | maxv);                              // REGION_REPEAT
    }
}

uint32_t sampleTexture(const Draw& d, float u, float v) {
    const uint32_t x = wrapCoord(static_cast<int>(std::floor(u)), d.tw, d.wms, d.minu, d.maxu);
    const uint32_t y = wrapCoord(static_cast<int>(std::floor(v)), d.th, d.wmt, d.minv, d.maxv);
    switch (d.tpsm) {
    case PSMCT32: return readRaw(d.tbp, d.tbw, d.tpsm, x, y);
    case PSMCT24: {
        const uint32_t c = readRaw(d.tbp, d.tbw, d.tpsm, x, y);
        return c | ((d.aem && c == 0) ? 0 : d.ta0 << 24);
    }
    case PSMCT16: case PSMCT16S: return expand16(readRaw(d.tbp, d.tbw, d.tpsm, x, y), d.ta0, d.ta1, d.aem);
    case PSMT8: case PSMT4: case PSMT8H: case PSMT4HL: case PSMT4HH:
        return clutLookup(d, readRaw(d.tbp, d.tbw, d.tpsm, x, y));
    default:
        PS2_UNIMPL("gs", "texture format", "texture format 0x%x is not supported", d.tpsm);
        return 0xFFFFFFFFu;
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

const Draw& currentDraw() {
    if (!g_drawDirty) return g_draw;
    g_drawDirty = false;
    Draw& d = g_draw;
    d.e = currentEnv();
    const Env& e = d.e;
    d.tex = field(e.prim, 4, 1) != 0;
    d.fst = field(e.prim, 8, 1) != 0;
    d.gouraud = field(e.prim, 3, 1) != 0;
    d.tbp = static_cast<uint32_t>(field(e.tex0, 0, 14));
    d.tbw = static_cast<uint32_t>(field(e.tex0, 14, 6));
    d.tpsm = static_cast<uint32_t>(field(e.tex0, 20, 6));
    d.tw = 1 << std::min<int>(10, static_cast<int>(field(e.tex0, 26, 4)));
    d.th = 1 << std::min<int>(10, static_cast<int>(field(e.tex0, 30, 4)));
    d.twf = static_cast<float>(d.tw);
    d.thf = static_cast<float>(d.th);
    d.tcc = field(e.tex0, 34, 1) != 0;
    d.tfx = static_cast<uint32_t>(field(e.tex0, 35, 2));
    d.wms = static_cast<uint32_t>(field(e.clamp, 0, 2));
    d.wmt = static_cast<uint32_t>(field(e.clamp, 2, 2));
    d.minu = static_cast<int>(field(e.clamp, 4, 10));
    d.maxu = static_cast<int>(field(e.clamp, 14, 10));
    d.minv = static_cast<int>(field(e.clamp, 24, 10));
    d.maxv = static_cast<int>(field(e.clamp, 34, 10));
    const uint64_t texa = g_regs[TEXA];
    d.ta0 = static_cast<uint32_t>(field(texa, 0, 8));
    d.ta1 = static_cast<uint32_t>(field(texa, 32, 8));
    d.aem = field(texa, 15, 1) != 0;
    d.cbp = static_cast<uint32_t>(field(e.tex0, 37, 14));
    d.cpsm = static_cast<uint32_t>(field(e.tex0, 51, 4));
    d.csa = static_cast<uint32_t>(field(e.tex0, 56, 5));
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
    d.fix = static_cast<uint32_t>(field(e.alpha, 32, 8));
    d.fb16 = e.fpsm == PSMCT16 || e.fpsm == PSMCT16S;
    d.fbmsk16 = pack16(e.fbmsk);
    return d;
}

// Shades and writes one pixel. Colour components are 0..255 floats.
void plot(const Draw& dr, int x, int y, uint32_t z, float r, float g, float b, float a, float u, float v, float fog, bool textured) {
    const Env& e = dr.e;
    if (x < e.scx0 || x > e.scx1 || y < e.scy0 || y > e.scy1) return;
    uint32_t cr = static_cast<uint32_t>(std::clamp(r, 0.f, 255.f));
    uint32_t cg = static_cast<uint32_t>(std::clamp(g, 0.f, 255.f));
    uint32_t cb = static_cast<uint32_t>(std::clamp(b, 0.f, 255.f));
    uint32_t ca = static_cast<uint32_t>(std::clamp(a, 0.f, 255.f));

    if (textured) {
        const uint32_t t = sampleTexture(dr, u, v);
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
    if (dr.zte) {
        const uint32_t zc = std::min(z, dr.zmax);
        const uint32_t zd = readRaw(e.zbp, e.fbw, e.zpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        switch (dr.ztst) {
        case 0: return;
        case 1: break;
        case 2: if (zc < zd) return; break;
        default: if (zc <= zd) return; break;
        }
        if (writeZ) writeRaw(e.zbp, e.fbw, e.zpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y), zc);
    }
    if (!writeFb) return;

    const uint32_t dst = readRaw(e.fbp, e.fbw, e.fpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    const uint32_t d = dr.fb16 ? expand16(dst) : dst;
    if (dr.abe && !(dr.pabe && !(ca & 0x80))) {   // alpha blending
        const uint32_t dR = d & 0xFF, dG = (d >> 8) & 0xFF, dB = (d >> 16) & 0xFF, da = d >> 24;
        auto pick = [&](uint64_t sel, uint32_t s, uint32_t dd) -> int { return sel == 0 ? int(s) : sel == 1 ? int(dd) : 0; };
        const int c = dr.bc == 0 ? int(ca) : dr.bc == 1 ? int(da) : int(dr.fix);
        auto blend = [&](uint32_t s, uint32_t dd) {
            int v = ((pick(dr.ba, s, dd) - pick(dr.bb, s, dd)) * c >> 7) + pick(dr.bd, s, dd);
            return dr.colclamp ? static_cast<uint32_t>(std::clamp(v, 0, 255)) : static_cast<uint32_t>(v & 0xFF);
        };
        cr = blend(cr, dR);
        cg = blend(cg, dG);
        cb = blend(cb, dB);
    }
    if (e.fba) ca |= 0x80;
    uint32_t out = cr | (cg << 8) | (cb << 16) | (ca << 24);
    if (!writeA) out = (out & 0x00FFFFFF) | (d & 0xFF000000);
    if (dr.fb16) {
        const uint32_t m = dr.fbmsk16;
        writeRaw(e.fbp, e.fbw, e.fpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y), (pack16(out) & ~m) | (dst & m));
    } else {
        writeRaw(e.fbp, e.fbw, e.fpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y), (out & ~e.fbmsk) | (dst & e.fbmsk));
    }
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

// Several threads can draw one primitive, each taking every parts-th band
// of four rows (see drawOnThreads).
bool myRow(int y, int part, int parts) { return parts == 1 || ((y >> 2) % parts) == part; }

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
    const int ix0 = box.x0, ix1 = box.x1, iy0 = box.y0, iy1 = box.y1;
    const bool tex = d.tex;
    float ua, va, ub, vb;
    texCoord(d, a.s, a.t, a.q, a.u, a.v, ua, va);
    texCoord(d, b.s, b.t, b.q, b.u, b.v, ub, vb);
    const float dx = b.x - a.x, dy = b.y - a.y;
    for (int y = iy0; y <= iy1; ++y) {
        if (!myRow(y, part, parts)) continue;
        const float fy = dy != 0 ? (static_cast<float>(y) - a.y) / dy : 0;
        for (int x = ix0; x <= ix1; ++x) {
            const float fx = dx != 0 ? (static_cast<float>(x) - a.x) / dx : 0;
            plot(d, x, y, b.z, b.r, b.g, b.b, b.a, ua + (ub - ua) * fx, va + (vb - va) * fy, b.fog, tex);
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
    const int minX = box.x0, maxX = box.x1, minY = box.y0, maxY = box.y1;
    const bool gouraud = d.gouraud;
    const bool tex = d.tex;
    const bool persp = tex && !d.fst;
    // Each row tests only the span its exact edge functions allow, widened by
    // a margin of area units that the float test's rounding cannot cross:
    // pixels outside it fail that test anyway (some weight is negative).
    constexpr double kMargin = 64.0;
    const double sign = area > 0 ? 1.0 : -1.0;
    const Edge edges[3] = {edgeFunction(v1, v2, sign), edgeFunction(v2, v0, sign), edgeFunction(v0, v1, sign)};
    for (int y = minY; y <= maxY; ++y) {
        if (!myRow(y, part, parts)) continue;
        double lo = minX, hi = maxX;
        bool empty = false;
        for (const Edge& g : edges) {   // inside (or nearly): a * px + k >= 0
            const double k = g.b * y + g.c + kMargin;
            if (g.a > 0) lo = std::max(lo, std::ceil(-k / g.a) - 1.0);
            else if (g.a < 0) hi = std::min(hi, std::floor(-k / g.a) + 1.0);
            else if (k < 0) empty = true;
        }
        if (empty || lo > hi) continue;
        for (int x = static_cast<int>(lo); x <= static_cast<int>(hi); ++x) {
            const float px = static_cast<float>(x), py = static_cast<float>(y);
            const float w0 = ((v1.x - px) * (v2.y - py) - (v2.x - px) * (v1.y - py)) / area;
            const float w1 = ((v2.x - px) * (v0.y - py) - (v0.x - px) * (v2.y - py)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            // Exclude the bottom/right edges so shared edges are drawn once.
            if ((w0 == 0 && w1 + w2 == 1 && py == std::max(v1.y, v2.y)) || (px == maxX && (w0 == 0 || w1 == 0 || w2 == 0) && maxX != minX))
                continue;
            auto lerp = [&](float a, float b, float c) { return a * w0 + b * w1 + c * w2; };
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
            float u = 0, v = 0;
            if (tex) {
                if (persp) {
                    const float s = lerp(v0.s, v1.s, v2.s), t = lerp(v0.t, v1.t, v2.t), q = lerp(v0.q, v1.q, v2.q);
                    texCoord(d, s, t, q, 0, 0, u, v);
                } else {
                    u = lerp(v0.u, v1.u, v2.u);
                    v = lerp(v0.v, v1.v, v2.v);
                }
            }
            plot(d, x, y, z, r, g, b, a, u, v, d.fge ? lerp(v0.fog, v1.fog, v2.fog) : 0.0f, tex);
        }
    }
}

void drawLine(const Draw& d, const Vertex& a, const Vertex& b) {
    const int steps = static_cast<int>(std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y)));
    for (int i = 0; i <= steps; ++i) {
        const float f = steps ? static_cast<float>(i) / static_cast<float>(steps) : 0;
        const Vertex& c = d.gouraud ? (f < 0.5f ? a : b) : b;
        plot(d, static_cast<int>(a.x + (b.x - a.x) * f), static_cast<int>(a.y + (b.y - a.y) * f), b.z, c.r, c.g, c.b, c.a, 0, 0, b.fog, false);
    }
}

// ---- drawing on several threads ----------------------------------------------------
// A large primitive is drawn by several threads at once, each taking every
// Nth band of four rows. Each pixel is still shaded by one thread, and the
// primitive is finished before the next one starts, so the picture is the
// one a single thread draws. A primitive whose pixels could see another
// thread's is drawn by one thread: a texture or CLUT in memory it draws
// into, or frame and depth buffers that overlap.

using RowJob = void (*)(const void* job, int part, int parts);

class DrawThreads {
public:
    ~DrawThreads() { stop(); }
    void setCount(int n) {
        stop();
        want_ = n;
    }
    int count() {
        if (want_ <= 0) want_ = static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u));
        return want_;
    }
    // job(part, count()) for every part: part 0 on this thread, the others on
    // helpers, which round floats as this thread does (the EE's rounding,
    // which the guest thread uses and the drawing inherits). Returns when all are done.
    void run(RowJob job, const void* ctx) {
        const int parts = count();
        start(parts - 1);
        job_ = job;
        ctx_ = ctx;
        parts_ = parts;
        rounding_ = std::fegetround();
        pending_.store(parts - 1);
        epoch_.fetch_add(1);
        epoch_.notify_all();
        job(ctx, 0, parts);
        for (int p = pending_.load(); p != 0; p = pending_.load()) pending_.wait(p);
    }

private:
    void start(int helpers) {
        if (static_cast<int>(threads_.size()) == helpers) return;
        stop();
        const uint32_t now = epoch_.load();
        for (int part = 1; part <= helpers; ++part) threads_.emplace_back([this, part, now] { work(part, now); });
    }
    void work(int part, uint32_t seen) {
        for (;;) {
            epoch_.wait(seen);
            seen = epoch_.load();
            if (stopping_) return;
            if (std::fegetround() != rounding_) std::fesetround(rounding_);
            job_(ctx_, part, parts_);
            if (pending_.fetch_sub(1) == 1) pending_.notify_one();
        }
    }
    void stop() {
        if (threads_.empty()) return;
        stopping_ = true;
        epoch_.fetch_add(1);
        epoch_.notify_all();
        for (std::thread& t : threads_) t.join();
        threads_.clear();
        stopping_ = false;
    }

    std::vector<std::thread> threads_;
    std::atomic<uint32_t> epoch_{0};
    std::atomic<int> pending_{0};
    std::atomic<bool> stopping_{false};
    RowJob job_ = nullptr;
    const void* ctx_ = nullptr;
    int parts_ = 1;
    int rounding_ = FE_TONEAREST;
    int want_ = 0;   // 0: half the host's threads, at most 8
};
DrawThreads g_drawThreads;

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
bool meets(Span a, Span b) {
    constexpr uint64_t kWords = uint64_t(1) << 20;
    if (a.hi > kWords || b.hi > kWords) return true;   // wraps round the end of memory: assume the worst
    return a.lo < b.hi && b.lo < a.hi;
}

// How many threads draw a primitive covering `box`: one, or all of them.
int drawParts(const Draw& d, const Box& box) {
    constexpr int64_t kMinPixels = 8192;   // smaller ones cost less than waking the helpers
    if (box.x1 < box.x0 || box.y1 < box.y0) return 1;
    if (static_cast<int64_t>(box.x1 - box.x0 + 1) * (box.y1 - box.y0 + 1) < kMinPixels || box.y1 - box.y0 < 16) return 1;
    const int n = g_drawThreads.count();
    if (n <= 1) return 1;
    const Env& e = d.e;
    const auto w = static_cast<uint32_t>(box.x1 + 1), h = static_cast<uint32_t>(box.y1 + 1);
    const Span frame = footprint(e.fbp, e.fbw, e.fpsm, w, h);
    const Span depth = footprint(e.zbp, e.fbw, e.zpsm, w, h);
    if (d.zte && meets(frame, depth)) return 1;
    if (d.tex) {
        switch (d.tpsm) {
        case PSMCT32: case PSMCT24: case PSMCT16: case PSMCT16S: break;
        case PSMT8: case PSMT4: case PSMT8H: case PSMT4HL: case PSMT4HH: {
            const Span clut{static_cast<uint64_t>(d.cbp) * 64, static_cast<uint64_t>(d.cbp) * 64 + 2048};
            if (meets(clut, frame) || (d.zte && meets(clut, depth))) return 1;
            break;
        }
        default: return 1;   // reported as unsupported, from one thread
        }
        // Region clamp and repeat can reach past the texture's size, up to 1024.
        const auto tw = static_cast<uint32_t>(d.wms >= 2 ? 1024 : d.tw), th = static_cast<uint32_t>(d.wmt >= 2 ? 1024 : d.th);
        const Span texels = footprint(d.tbp, d.tbw, d.tpsm, tw, th);
        if (meets(texels, frame) || (d.zte && meets(texels, depth))) return 1;
    }
    return n;
}

void triangle(const Draw& d, const Vertex& v0, const Vertex& v1, const Vertex& v2) {
    if (drawParts(d, triangleBox(d, v0, v1, v2)) == 1) {
        drawTriangle(d, v0, v1, v2, 0, 1);
        return;
    }
    struct Job {
        const Draw* d;
        const Vertex *v0, *v1, *v2;
    } job{&d, &v0, &v1, &v2};
    g_drawThreads.run([](const void* p, int part, int parts) {
        const auto* j = static_cast<const Job*>(p);
        drawTriangle(*j->d, *j->v0, *j->v1, *j->v2, part, parts);
    }, &job);
}

void sprite(const Draw& d, const Vertex& a, const Vertex& b) {
    if (drawParts(d, spriteBox(d, a, b)) == 1) {
        drawSprite(d, a, b, 0, 1);
        return;
    }
    struct Job {
        const Draw* d;
        const Vertex *a, *b;
    } job{&d, &a, &b};
    g_drawThreads.run([](const void* p, int part, int parts) {
        const auto* j = static_cast<const Job*>(p);
        drawSprite(*j->d, *j->a, *j->b, part, parts);
    }, &job);
}

void kick(bool draw) {
    const Draw& d = currentDraw();
    const Env& e = d.e;
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
        if (draw) plot(d, static_cast<int>(v.x), static_cast<int>(v.y), v.z, v.r, v.g, v.b, v.a, 0, 0, v.fog, false);
        g_queueCount = 0;
        break;
    case 1:   // line
        if (g_queueCount == 2) {
            if (draw) drawLine(d, g_queue[0], g_queue[1]);
            g_queueCount = 0;
        }
        break;
    case 2:   // line strip
        if (g_queueCount == 2) {
            if (draw) drawLine(d, g_queue[0], g_queue[1]);
            g_queue[0] = g_queue[1];
            g_queueCount = 1;
        }
        break;
    case 3:   // triangle
        if (g_queueCount == 3) {
            if (draw) triangle(d, g_queue[0], g_queue[1], g_queue[2]);
            g_queueCount = 0;
        }
        break;
    case 4:   // triangle strip
        if (g_queueCount == 3) {
            if (draw) triangle(d, g_queue[0], g_queue[1], g_queue[2]);
            g_queue[0] = g_queue[1];
            g_queue[1] = g_queue[2];
            g_queueCount = 2;
        }
        break;
    case 5:   // triangle fan
        if (g_queueCount == 3) {
            if (draw) triangle(d, g_queue[0], g_queue[1], g_queue[2]);
            g_queue[1] = g_queue[2];
            g_queueCount = 2;
        }
        break;
    case 6:   // sprite
        if (g_queueCount == 2) {
            if (draw) sprite(d, g_queue[0], g_queue[1]);
            g_queueCount = 0;
        }
        break;
    default:
        g_queueCount = 0;
        break;
    }
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
    case TEX2_1:
    case TEX2_2: {   // replaces the CLUT/PSM fields of TEX0
        const uint64_t keep = 0x3FFFFull | (0xFull << 26) | (0xFull << 30) | (0x7ull << 34);   // TBP0 TBW TW TH TCC TFX
        uint64_t& t0 = g_regs[TEX0_1 + (reg - TEX2_1)];
        t0 = (t0 & keep) | (v & ~keep);
        break;
    }
    case TRXDIR:
        g_regs[TRXDIR] = v;
        startTransfer(static_cast<uint32_t>(v & 3));
        return;
    case HWREG: transferData(v); return;
    case SIGNAL: {
        const auto id = static_cast<uint32_t>(v), mask = static_cast<uint32_t>(v >> 32);
        g_siglblid = (g_siglblid & ~static_cast<uint64_t>(mask)) | (id & mask);
        g_csr |= 1;
        updateGsInterrupt();
        break;
    }
    case FINISH:
        g_csr |= 2;
        updateGsInterrupt();
        break;
    case LABEL: {
        const auto id = static_cast<uint32_t>(v), mask = static_cast<uint32_t>(v >> 32);
        g_siglblid = (g_siglblid & ~(static_cast<uint64_t>(mask) << 32)) | (static_cast<uint64_t>(id & mask) << 32);
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

} // namespace

// ===========================================================================
// Public interface
// ===========================================================================
void init() {
    std::memset(g_vram, 0, sizeof g_vram);
    std::memset(g_regs, 0, sizeof g_regs);
    std::memset(g_priv, 0, sizeof g_priv);
    g_csr = 0x551B4000ull;
    g_imr = 0x7F00;
    g_history = FieldHistory{};
    g_drawDirty = true;
    g_queueCount = 0;
    for (auto& p : g_paths) p = GifPath{};
    g_regs[PRMODECONT] = 1;
}

uint64_t csr() { return g_csr; }
uint64_t imr() { return g_imr; }
uint64_t putImr(uint64_t v) {
    const uint64_t old = g_imr;
    g_imr = v;
    updateGsInterrupt();
    return old;
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
                g_queueCount = 0;
                for (auto& p : g_paths) p = GifPath{};
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
    video::onVsync();
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
        for (auto& p : g_paths) p = GifPath{};
    }
    if (addr == 0x10003010u) g_path3Masked = v & 1;
}

void gifFifoWrite(const uint64_t q[2]) { gifQword(g_paths[3], q); }

void gifTransfer(const uint64_t* qwords, size_t count, int path) {
    GifPath& p = g_paths[path & 3];
    for (size_t i = 0; i < count; ++i) gifQword(p, qwords + 2 * i);
}

void setPath3Masked(bool masked) { g_path3Masked = masked; }

uint32_t readPixel32(uint32_t fbp, uint32_t fbw, uint32_t psm, int x, int y) {
    const uint32_t raw = readRaw(fbp * 32, fbw, psm, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    if (psm == PSMCT16 || psm == PSMCT16S) return expand16(raw);
    if (psm == PSMCT24) return raw | 0xFF000000u;
    return raw;
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

Circuit readCircuit(int i) {
    const uint64_t dispfb = g_priv[i == 0 ? 0x7 : 0x9], display = g_priv[i == 0 ? 0x8 : 0xA];
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

// The circuit's pixel at a screen position, if it covers it. Interlaced FRAME
// mode (FFMD) shows one field per vblank: half the lines, read one after the
// other, each covering two display lines, the odd field one line lower.
// ---- Deinterlacing -----------------------------------------------------------

// The same colour, give or take what rounding and dithering change.
bool still(uint32_t a, uint32_t b) {
    for (int s = 0; s < 24; s += 8) {
        const int d = static_cast<int>((a >> s) & 0xFF) - static_cast<int>((b >> s) & 0xFF);
        if (d > 16 || d < -16) return false;
    }
    return true;
}

uint32_t average(uint32_t a, uint32_t b) {
    uint32_t px = 0xFF000000u;
    for (int s = 0; s < 24; s += 8) px |= ((((a >> s) & 0xFF) + ((b >> s) & 0xFF) + 1) / 2) << s;
    return px;
}

// Fills `out` (w x h) from the current field `cur` (`lines` lines, on the
// display lines of parity `odd`) and the fields before it.
void deinterlaceField(Frame& out, std::vector<uint32_t>&& cur, int w, int h, int lines, int odd) {
    FieldHistory& hist = g_history;
    if (hist.width != w || hist.lines != lines) hist = FieldHistory{w, lines, {}};
    const ShownField& prev = hist.fields[0];
    const ShownField& prev2 = hist.fields[1];
    const bool havePrev = prev.valid && prev.odd != odd && prev.when + 1 == g_fieldCount;
    const bool havePrev2 = havePrev && prev2.valid && prev2.odd == odd && prev2.when + 2 == g_fieldCount;
    const Deinterlace mode = g_deinterlace;
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
        if (mode == Deinterlace::Bob || !havePrev) {
            std::copy_n(a, w, o);
            continue;
        }
        const uint32_t* p = row(prev.pixels, (y - (1 - odd)) / 2);
        if (mode == Deinterlace::Weave) {
            std::copy_n(p, w, o);
            continue;
        }
        // Adaptive: still where this field matches the last one of its parity.
        const uint32_t* pa = havePrev2 ? row(prev2.pixels, above) : nullptr;
        const uint32_t* pb = havePrev2 ? row(prev2.pixels, below) : nullptr;
        for (int x = 0; x < w; ++x)
            o[x] = pa && still(a[x], pa[x]) && still(b[x], pb[x]) ? p[x] : average(a[x], b[x]);
    }
    hist.fields[1] = std::move(hist.fields[0]);
    hist.fields[0] = ShownField{std::move(cur), odd, g_fieldCount, true};
}

bool circuitPixel(const Circuit& c, int clock, int line, bool perField, int odd, uint32_t& px) {
    if (clock < c.dx || line < c.dy || clock >= c.dx + c.width || line >= c.dy + c.height) return false;
    const int x = (clock - c.dx) / c.magh;
    int y = (line - c.dy) / c.magv;
    if (perField) y = std::clamp((y - odd) / 2, 0, std::max(c.height / c.magv / 2, 1) - 1);
    px = readPixel32(c.fbp, c.fbw, c.psm, c.dbx + x, c.dby + y);
    return true;
}

} // namespace

bool currentFrame(Frame& out) {
    const uint64_t pmode = g_priv[0x0], smode2 = g_priv[0x2];
    const Circuit c1 = readCircuit(0), c2 = readCircuit(1);
    const bool en1 = (pmode & 1) && c1.valid, en2 = (pmode & 2) && c2.valid;
    if (!en1 && !en2) return false;
    const bool perField = (smode2 & 1) && (smode2 & 2);
    const int odd = perField ? static_cast<int>((g_csr >> 13) & 1) : 0;
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
    const uint32_t bgcolor = static_cast<uint32_t>(g_priv[0xE]) & 0xFFFFFFu;
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
    deinterlaceField(out, std::move(cur), w, h, lines, odd);
    return true;
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

void setDrawThreads(int n) { g_drawThreads.setCount(n); }

} // namespace ps2::gs
