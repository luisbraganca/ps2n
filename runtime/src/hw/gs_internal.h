// The GS's state and memory as the software rasterizer (gs.cpp) and the
// Vulkan renderer (gpu/renderer.cpp) both see them: local memory and its
// addressing, the decoded drawing state, primitives' vertices, the pages a
// drawing state touches, the display circuits. Moved here unchanged from
// gs.cpp, which defines the globals.
#pragma once

#include "ps2/compiler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ps2::gs::detail {

// ===========================================================================
// Local memory (4 MB) and pixel addressing
// ===========================================================================
// The swizzle formulas below reproduce the block/column tables of the GS
// User's Manual (verified entry-for-entry against PCSX2's tables).
extern uint32_t g_vram[1024 * 1024];   // defined in gs.cpp

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

inline bool isZ(uint32_t psm) { return (psm & 0x30) == 0x30; }

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
extern const Swizzle g_swizzle;

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
extern const ClutOffsets g_clutOffsets;

// Word index of a 32-bit pixel (CT32/CT24/Z32/Z24 and the T8H/T4HL/T4HH host words).
inline uint32_t addr32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, bool z) {
    const uint32_t page = (y >> 5) * std::max(bw, 1u) + (x >> 6);
    return (bp * 64 + page * 2048 + g_swizzle.c32[z][y & 31][x & 63]) & 0xFFFFF;
}
// Halfword index of a 16-bit pixel.
inline uint32_t addr16(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, bool s, bool z) {
    const uint32_t page = (y >> 6) * std::max(bw, 1u) + (x >> 6);
    return (bp * 128 + page * 4096 + g_swizzle.c16[(s ? 1 : 0) | (z ? 2 : 0)][y & 63][x & 63]) & 0x1FFFFF;
}
inline uint32_t addr8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (y >> 6) * std::max(bw >> 1, 1u) + (x >> 7);
    return (bp * 256 + page * 8192 + g_swizzle.c8[y & 63][x & 127]) & 0x3FFFFF;
}
inline uint32_t addr4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    const uint32_t page = (y >> 7) * std::max(bw >> 1, 1u) + (x >> 7);
    return (bp * 512 + page * 16384 + g_swizzle.c4[y & 127][x & 127]) & 0x7FFFFF;
}

inline uint8_t* vramBytes() { return reinterpret_cast<uint8_t*>(g_vram); }

// The memory unit holding pixel (x, y) in format psm: a word, halfword, byte
// or nibble, as the format addresses memory.
inline uint32_t pixelUnit(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y) {
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
inline Buffer bufferOf(uint32_t bp, uint32_t bw, uint32_t psm) {
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
inline uint32_t readRaw(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y) { return readUnit(psm, pixelUnit(bp, bw, psm, x, y)); }

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
inline uint32_t bitsPerPixel(uint32_t psm) {
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
inline uint32_t expand16(uint32_t c, uint32_t ta0 = 0, uint32_t ta1 = 0x80, bool aem = false) {
    const uint32_t r = (c & 0x1F) << 3, g = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
    uint32_t a = (c & 0x8000) ? ta1 : ta0;
    if (aem && (c & 0xFFFF) == 0) a = 0;
    return r | (g << 8) | (b << 16) | (a << 24);
}
inline uint32_t pack16(uint32_t c) {
    return ((c >> 3) & 0x1F) | (((c >> 11) & 0x1F) << 5) | (((c >> 19) & 0x1F) << 10) | ((c >> 31) << 15);
}

// A frame buffer pixel as 32-bit RGBA; fbp in pages.
inline uint32_t pixel32(uint32_t fbp, uint32_t fbw, uint32_t psm, int x, int y) {
    const uint32_t raw = readRaw(fbp * 32, fbw, psm, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    if (psm == PSMCT16 || psm == PSMCT16S) return expand16(raw);
    if (psm == PSMCT24) return raw | 0xFF000000u;
    return raw;
}

constexpr uint64_t field(uint64_t v, int lo, int bits) { return (v >> lo) & ((1ull << bits) - 1); }

// ---- vertices -----------------------------------------------------------------
struct Vertex {
    float x = 0, y = 0;          // window coordinates (pixels)
    uint32_t z = 0;
    float r = 0, g = 0, b = 0, a = 0;
    float s = 0, t = 0, q = 1;
    float u = 0, v = 0;          // texels (FST=1)
    float fog = 0;
};

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
extern const Log2Steps g_log2Steps;

constexpr int kFarthest = 1 << 20;   // a level of detail past every level

// The level of detail at |Q|, given as its float's bits, in sixteenths (K's
// precision): log2(1 / |Q|) * 2^L + K, rounded down. That is K less
// ceil(log2|Q| * 2^(4 + L)): the exponent's octaves and the mantissa's
// steps, plus one unless the mantissa is 0 (any other has an irrational
// log2). Q = 0 is the farthest. It falls as |Q| grows.
inline int levelOfDetail(const Draw& d, uint32_t bits) {
    const int e = static_cast<int>(bits >> 23);
    if (e == 0) return kFarthest;   // 0 (the PS2 has no denormals)
    const uint32_t m = bits & 0x7FFFFFu;
    return d.lodK - (e - 127) * (16 << d.lodL) - (m ? (g_log2Steps(m) >> (3 - d.lodL)) + 1 : 0);
}

inline bool alphaTestPass(const Draw& d, uint32_t a) {
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

inline uint32_t zMax(uint32_t zpsm) {
    switch (zpsm) {
    case PSMZ24: return 0xFFFFFF;
    case PSMZ16: case PSMZ16S: return 0xFFFF;
    default: return 0xFFFFFFFF;
    }
}

enum class Shape : uint8_t { Point, Line, Triangle, Sprite };

// ---- drawing on several threads ----------------------------------------------------
// The words of local memory that pixels (0..w-1, 0..h-1) of a buffer can
// address: from its first block to the end of the last page they reach.
struct Span {
    uint64_t lo, hi;
};
inline Span footprint(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t w, uint32_t h) {
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
inline bool meets(Span a, Span b) {
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
inline uint32_t layout(uint32_t bp, uint32_t bw, uint32_t psm) {
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
inline Span clutSpan(const Draw& d) {
    const uint32_t entries = d.tpsm == PSMT8 || d.tpsm == PSMT8H ? 256 : 16;
    uint32_t lo = ~0u, hi = 0;
    for (uint32_t i = 0; i < entries; ++i) {
        lo = std::min<uint32_t>(lo, d.clutOffsets[i]);
        hi = std::max<uint32_t>(hi, d.clutOffsets[i]);
    }
    const uint64_t first = static_cast<uint64_t>(d.clutBase) + lo, last = static_cast<uint64_t>(d.clutBase) + hi;
    return d.clutWide ? Span{first, last + 1} : Span{first / 2, last / 2 + 1};   // 16-bit entries: halfwords
}

inline Access accessOf(const Draw& d) {
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

// What the display circuits show at a vsync: the privileged registers, CSR
// (its FIELD bit) and the vsync count, which tells consecutive fields apart.
struct Display {
    uint64_t priv[0x10];
    uint64_t csr;
    uint64_t fieldCount;
};

// A CRTC read-out circuit: where its picture sits on screen (DISPLAY, in video
// clocks and lines) and where it is read from (DISPFB).
struct Circuit {
    uint32_t fbp = 0, fbw = 0, psm = 0;
    int dbx = 0, dby = 0;
    int dx = 0, dy = 0, magh = 1, magv = 1;
    int width = 0, height = 0;   // video clocks and lines
    bool valid = false;
};

inline Circuit readCircuit(const Display& d, int i) {
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

// A host-to-local transfer under way: its destination and where the next
// pixel goes (x, y within the rectangle). gs.cpp writes it; the Vulkan
// renderer reads how far it got.
struct Transfer {
    bool active = false;
    uint32_t dbp = 0, dbw = 0, dpsm = 0;
    uint32_t dsax = 0, dsay = 0, rrw = 0, rrh = 0;
    uint32_t x = 0, y = 0;
    uint64_t bitBuf = 0;
    uint32_t bitCount = 0;
};
extern Transfer g_xfer;

} // namespace ps2::gs::detail
