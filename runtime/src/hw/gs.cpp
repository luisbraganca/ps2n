#include "hw/gs.h"

#include "hw/hw.h"
#include "kernel/kernel.h"
#include "log.h"
#include "video/video.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

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

// Word index of a 32-bit pixel (CT32/CT24/Z32/Z24 and the T8H/T4HL/T4HH host words).
uint32_t addr32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, bool z) {
    const uint32_t page = (y >> 5) * std::max(bw, 1u) + (x >> 6);
    uint32_t block = bt32((x >> 3) & 7, (y >> 3) & 3);
    if (z) block ^= 0x18;
    return (bp * 64 + page * 2048 + block * 64 + ct32(x & 7, y & 7)) & 0xFFFFF;
}
// Halfword index of a 16-bit pixel.
uint32_t addr16(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, bool s, bool z) {
    const uint32_t page = (y >> 6) * std::max(bw, 1u) + (x >> 6);
    uint32_t block = s ? bt16s((x >> 4) & 3, (y >> 3) & 7) : bt16((x >> 4) & 3, (y >> 3) & 7);
    if (z) block ^= 0x18;
    return (bp * 128 + page * 4096 + block * 128 + ct16(x & 15, y & 7)) & 0x1FFFFF;
}
uint32_t addr8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (y >> 6) * std::max(bw >> 1, 1u) + (x >> 7);
    return (bp * 256 + page * 8192 + bt32((x >> 4) & 7, (y >> 4) & 3) * 256 + ct8(x & 15, y & 15)) & 0x3FFFFF;
}
uint32_t addr4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (y >> 7) * std::max(bw >> 1, 1u) + (x >> 7);
    return (bp * 512 + page * 16384 + bt16((x >> 5) & 3, (y >> 4) & 7) * 512 + ct4(x & 31, y & 15)) & 0x7FFFFF;
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
uint64_t g_csr = 0x551B4000ull;   // FIFO empty, REV 0x1B, ID 0x55
uint64_t g_imr = 0x7F00;
uint64_t g_siglblid = 0;
bool g_path3Masked = false;

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

uint32_t clutLookup(uint32_t index, uint64_t tex0, uint32_t psm) {
    const auto cbp = static_cast<uint32_t>(field(tex0, 37, 14));
    const auto cpsm = static_cast<uint32_t>(field(tex0, 51, 4));
    const auto csa = static_cast<uint32_t>(field(tex0, 56, 5));
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

uint32_t sampleTexture(const Env& e, float u, float v) {
    const uint64_t tex0 = e.tex0;
    const auto tbp = static_cast<uint32_t>(field(tex0, 0, 14));
    const auto tbw = static_cast<uint32_t>(field(tex0, 14, 6));
    const auto psm = static_cast<uint32_t>(field(tex0, 20, 6));
    const int tw = 1 << std::min<int>(10, static_cast<int>(field(tex0, 26, 4)));
    const int th = 1 << std::min<int>(10, static_cast<int>(field(tex0, 30, 4)));
    const uint64_t cl = e.clamp;
    const uint32_t x = wrapCoord(static_cast<int>(std::floor(u)), tw, static_cast<uint32_t>(field(cl, 0, 2)),
                                 static_cast<int>(field(cl, 4, 10)), static_cast<int>(field(cl, 14, 10)));
    const uint32_t y = wrapCoord(static_cast<int>(std::floor(v)), th, static_cast<uint32_t>(field(cl, 2, 2)),
                                 static_cast<int>(field(cl, 24, 10)), static_cast<int>(field(cl, 34, 10)));
    const uint64_t texa = g_regs[TEXA];
    const auto ta0 = static_cast<uint32_t>(field(texa, 0, 8)), ta1 = static_cast<uint32_t>(field(texa, 32, 8));
    const bool aem = field(texa, 15, 1) != 0;
    switch (psm) {
    case PSMCT32: return readRaw(tbp, tbw, psm, x, y);
    case PSMCT24: {
        const uint32_t c = readRaw(tbp, tbw, psm, x, y);
        return c | ((aem && c == 0) ? 0 : ta0 << 24);
    }
    case PSMCT16: case PSMCT16S: return expand16(readRaw(tbp, tbw, psm, x, y), ta0, ta1, aem);
    case PSMT8: case PSMT4: case PSMT8H: case PSMT4HL: case PSMT4HH:
        return clutLookup(readRaw(tbp, tbw, psm, x, y), tex0, psm);
    default:
        PS2_UNIMPL("gs", "texture format", "texture format 0x%x is not supported", psm);
        return 0xFFFFFFFFu;
    }
}

bool alphaTestPass(uint64_t test, uint32_t a) {
    if (!field(test, 0, 1)) return true;
    const auto aref = static_cast<uint32_t>(field(test, 4, 8));
    switch (field(test, 1, 3)) {
    case 0: return false;
    case 1: return true;
    case 2: return a < aref;
    case 3: return a <= aref;
    case 4: return a == aref;
    case 5: return a >= aref;
    case 6: return a > aref;
    default: return a != aref;
    }
}

uint32_t zMax(uint32_t zpsm) {
    switch (zpsm) {
    case PSMZ24: return 0xFFFFFF;
    case PSMZ16: case PSMZ16S: return 0xFFFF;
    default: return 0xFFFFFFFF;
    }
}

// Shades and writes one pixel. Colour components are 0..255 floats.
void plot(const Env& e, int x, int y, uint32_t z, float r, float g, float b, float a, float u, float v, float fog, bool textured) {
    if (x < e.scx0 || x > e.scx1 || y < e.scy0 || y > e.scy1) return;
    const uint64_t prim = e.prim;
    uint32_t cr = static_cast<uint32_t>(std::clamp(r, 0.f, 255.f));
    uint32_t cg = static_cast<uint32_t>(std::clamp(g, 0.f, 255.f));
    uint32_t cb = static_cast<uint32_t>(std::clamp(b, 0.f, 255.f));
    uint32_t ca = static_cast<uint32_t>(std::clamp(a, 0.f, 255.f));

    if (textured) {
        const uint32_t t = sampleTexture(e, u, v);
        const uint32_t tr = t & 0xFF, tg = (t >> 8) & 0xFF, tb = (t >> 16) & 0xFF, ta = t >> 24;
        const bool tcc = field(e.tex0, 34, 1) != 0;
        switch (field(e.tex0, 35, 2)) {
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
    if (field(prim, 5, 1)) {   // fog
        const uint64_t fc = g_regs[FOGCOL];
        const auto f = static_cast<uint32_t>(std::clamp(fog, 0.f, 255.f));
        cr = (f * cr + (255 - f) * static_cast<uint32_t>(field(fc, 0, 8))) >> 8;
        cg = (f * cg + (255 - f) * static_cast<uint32_t>(field(fc, 8, 8))) >> 8;
        cb = (f * cb + (255 - f) * static_cast<uint32_t>(field(fc, 16, 8))) >> 8;
    }

    bool writeFb = true, writeZ = !e.zmsk, writeA = true;
    if (!alphaTestPass(e.test, ca)) {
        switch (field(e.test, 12, 2)) {   // AFAIL
        case 0: return;
        case 1: writeZ = false; break;
        case 2: writeFb = false; break;
        default: writeZ = false; writeA = false; break;
        }
    }
    const bool zte = field(e.test, 16, 1) != 0;
    if (zte) {
        const uint32_t zm = zMax(e.zpsm);
        const uint32_t zc = std::min(z, zm);
        const uint32_t zd = readRaw(e.zbp, e.fbw, e.zpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        switch (field(e.test, 17, 2)) {
        case 0: return;
        case 1: break;
        case 2: if (zc < zd) return; break;
        default: if (zc <= zd) return; break;
        }
        if (writeZ) writeRaw(e.zbp, e.fbw, e.zpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y), zc);
    }
    if (!writeFb) return;

    const uint32_t dst = readRaw(e.fbp, e.fbw, e.fpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    const uint32_t d = (e.fpsm == PSMCT16 || e.fpsm == PSMCT16S) ? expand16(dst) : dst;
    const bool pabe = field(g_regs[PABE], 0, 1) != 0;
    if (field(prim, 6, 1) && !(pabe && !(ca & 0x80))) {   // alpha blending
        const uint64_t al = e.alpha;
        const uint32_t dr = d & 0xFF, dg = (d >> 8) & 0xFF, db = (d >> 16) & 0xFF, da = d >> 24;
        auto pick = [&](uint64_t sel, uint32_t s, uint32_t dd) -> int { return sel == 0 ? int(s) : sel == 1 ? int(dd) : 0; };
        const uint64_t A = field(al, 0, 2), B = field(al, 2, 2), C = field(al, 4, 2), D = field(al, 6, 2);
        const int c = C == 0 ? int(ca) : C == 1 ? int(da) : int(field(al, 32, 8));
        auto blend = [&](uint32_t s, uint32_t dd) {
            int v = ((pick(A, s, dd) - pick(B, s, dd)) * c >> 7) + pick(D, s, dd);
            return field(g_regs[COLCLAMP], 0, 1) ? static_cast<uint32_t>(std::clamp(v, 0, 255)) : static_cast<uint32_t>(v & 0xFF);
        };
        cr = blend(cr, dr);
        cg = blend(cg, dg);
        cb = blend(cb, db);
    }
    if (e.fba) ca |= 0x80;
    uint32_t out = cr | (cg << 8) | (cb << 16) | (ca << 24);
    if (!writeA) out = (out & 0x00FFFFFF) | (d & 0xFF000000);
    if (e.fpsm == PSMCT16 || e.fpsm == PSMCT16S) {
        const uint32_t m = pack16(e.fbmsk);
        writeRaw(e.fbp, e.fbw, e.fpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y), (pack16(out) & ~m) | (dst & m));
    } else {
        writeRaw(e.fbp, e.fbw, e.fpsm, static_cast<uint32_t>(x), static_cast<uint32_t>(y), (out & ~e.fbmsk) | (dst & e.fbmsk));
    }
}

bool textured(const Env& e) { return field(e.prim, 4, 1) != 0; }
bool fst(const Env& e) { return field(e.prim, 8, 1) != 0; }

void texCoord(const Env& e, float s, float t, float q, float u, float v, float& outU, float& outV) {
    if (fst(e)) {
        outU = u;
        outV = v;
        return;
    }
    const float tw = static_cast<float>(1 << std::min<int>(10, static_cast<int>(field(e.tex0, 26, 4))));
    const float th = static_cast<float>(1 << std::min<int>(10, static_cast<int>(field(e.tex0, 30, 4))));
    const float iq = q != 0 ? 1.0f / q : 1.0f;
    outU = s * iq * tw;
    outV = t * iq * th;
}

void drawSprite(const Env& e, const Vertex& a, const Vertex& b) {
    const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
    const float y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    const int ix0 = std::max(static_cast<int>(std::ceil(x0)), e.scx0), ix1 = std::min(static_cast<int>(std::ceil(x1)) - 1, e.scx1);
    const int iy0 = std::max(static_cast<int>(std::ceil(y0)), e.scy0), iy1 = std::min(static_cast<int>(std::ceil(y1)) - 1, e.scy1);
    const bool tex = textured(e);
    float ua, va, ub, vb;
    texCoord(e, a.s, a.t, a.q, a.u, a.v, ua, va);
    texCoord(e, b.s, b.t, b.q, b.u, b.v, ub, vb);
    const float dx = b.x - a.x, dy = b.y - a.y;
    for (int y = iy0; y <= iy1; ++y) {
        const float fy = dy != 0 ? (static_cast<float>(y) - a.y) / dy : 0;
        for (int x = ix0; x <= ix1; ++x) {
            const float fx = dx != 0 ? (static_cast<float>(x) - a.x) / dx : 0;
            plot(e, x, y, b.z, b.r, b.g, b.b, b.a, ua + (ub - ua) * fx, va + (vb - va) * fy, b.fog, tex);
        }
    }
}

void drawTriangle(const Env& e, const Vertex& v0, const Vertex& v1, const Vertex& v2) {
    const float area = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
    if (area == 0) return;
    const int minX = std::max(static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))), e.scx0);
    const int maxX = std::min(static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))), e.scx1);
    const int minY = std::max(static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))), e.scy0);
    const int maxY = std::min(static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))), e.scy1);
    const bool gouraud = field(e.prim, 3, 1) != 0;
    const bool tex = textured(e);
    const bool persp = tex && !fst(e);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x), py = static_cast<float>(y);
            const float w0 = ((v1.x - px) * (v2.y - py) - (v2.x - px) * (v1.y - py)) / area;
            const float w1 = ((v2.x - px) * (v0.y - py) - (v0.x - px) * (v2.y - py)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            // Exclude the bottom/right edges so shared edges are drawn once.
            if ((w0 == 0 && w1 + w2 == 1 && py == std::max(v1.y, v2.y)) || (px == maxX && (w0 == 0 || w1 == 0 || w2 == 0) && maxX != minX))
                continue;
            auto lerp = [&](float a, float b, float c) { return a * w0 + b * w1 + c * w2; };
            const auto z = static_cast<uint32_t>(std::clamp(lerp(static_cast<float>(v0.z), static_cast<float>(v1.z), static_cast<float>(v2.z)), 0.f, 4294967295.f));
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
                    texCoord(e, s, t, q, 0, 0, u, v);
                } else {
                    u = lerp(v0.u, v1.u, v2.u);
                    v = lerp(v0.v, v1.v, v2.v);
                }
            }
            plot(e, x, y, z, r, g, b, a, u, v, lerp(v0.fog, v1.fog, v2.fog), tex);
        }
    }
}

void drawLine(const Env& e, const Vertex& a, const Vertex& b) {
    const int steps = static_cast<int>(std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y)));
    for (int i = 0; i <= steps; ++i) {
        const float f = steps ? static_cast<float>(i) / static_cast<float>(steps) : 0;
        const Vertex& c = field(e.prim, 3, 1) ? (f < 0.5f ? a : b) : b;
        plot(e, static_cast<int>(a.x + (b.x - a.x) * f), static_cast<int>(a.y + (b.y - a.y) * f), b.z, c.r, c.g, c.b, c.a, 0, 0, b.fog, false);
    }
}

void kick(bool draw) {
    const Env e = currentEnv();
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
        if (draw) plot(e, static_cast<int>(v.x), static_cast<int>(v.y), v.z, v.r, v.g, v.b, v.a, 0, 0, v.fog, false);
        g_queueCount = 0;
        break;
    case 1:   // line
        if (g_queueCount == 2) {
            if (draw) drawLine(e, g_queue[0], g_queue[1]);
            g_queueCount = 0;
        }
        break;
    case 2:   // line strip
        if (g_queueCount == 2) {
            if (draw) drawLine(e, g_queue[0], g_queue[1]);
            g_queue[0] = g_queue[1];
            g_queueCount = 1;
        }
        break;
    case 3:   // triangle
        if (g_queueCount == 3) {
            if (draw) drawTriangle(e, g_queue[0], g_queue[1], g_queue[2]);
            g_queueCount = 0;
        }
        break;
    case 4:   // triangle strip
        if (g_queueCount == 3) {
            if (draw) drawTriangle(e, g_queue[0], g_queue[1], g_queue[2]);
            g_queue[0] = g_queue[1];
            g_queue[1] = g_queue[2];
            g_queueCount = 2;
        }
        break;
    case 5:   // triangle fan
        if (g_queueCount == 3) {
            if (draw) drawTriangle(e, g_queue[0], g_queue[1], g_queue[2]);
            g_queue[1] = g_queue[2];
            g_queueCount = 2;
        }
        break;
    case 6:   // sprite
        if (g_queueCount == 2) {
            if (draw) drawSprite(e, g_queue[0], g_queue[1]);
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
    g_priv[(addr >> 4) & 0xF] = v;
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

bool currentFrame(Frame& out) {
    const uint64_t pmode = g_priv[0x0];
    const int circuit = (pmode & 1) ? 0 : (pmode & 2) ? 1 : -1;
    if (circuit < 0) return false;
    const uint64_t dispfb = g_priv[circuit == 0 ? 0x7 : 0x9];
    const uint64_t display = g_priv[circuit == 0 ? 0x8 : 0xA];
    const uint64_t smode2 = g_priv[0x2];
    const auto fbp = static_cast<uint32_t>(field(dispfb, 0, 9));
    const auto fbw = static_cast<uint32_t>(field(dispfb, 9, 6));
    const auto psm = static_cast<uint32_t>(field(dispfb, 15, 5));
    const auto dbx = static_cast<int>(field(dispfb, 32, 11)), dby = static_cast<int>(field(dispfb, 43, 11));
    const int magh = static_cast<int>(field(display, 23, 4)) + 1, magv = static_cast<int>(field(display, 27, 2)) + 1;
    int w = (static_cast<int>(field(display, 32, 12)) + 1) / magh;
    int h = (static_cast<int>(field(display, 44, 11)) + 1) / magv;
    if ((smode2 & 1) && (smode2 & 2)) h /= 2;   // interlaced field mode: one field's worth of lines
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return false;
    out.width = w;
    out.height = h;
    out.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                readPixel32(fbp, fbw, psm, dbx + x, dby + y) | 0xFF000000u;
    return true;
}

} // namespace ps2::gs
