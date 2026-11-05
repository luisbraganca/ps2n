// What the Vulkan renderer's shaders share: the descriptor set, GS memory's
// addressing (the swizzle tables of gs_internal.h), and arithmetic that
// rounds as the EE's thread does. A shader that includes it may define
// ROUND_TOWARD_ZERO, as the guest thread rounds by default (gs.frag's RTZ
// variant does, with the matching execution modes).
#extension GL_EXT_nonuniform_qualifier : require

#include "layout.h"

layout(set = 0, binding = 0, r32ui) uniform coherent uimage2D images[];   // targets: raw units, at scale
layout(set = 0, binding = 1, std430) buffer Mirror { uint vram[]; };      // GS memory as the card holds it
layout(set = 0, binding = 2, std430) readonly buffer Tables { uint tables[]; };
layout(set = 0, binding = 3, std430) readonly buffer States { uint states[]; };
layout(set = 0, binding = 4, std430) readonly buffer Prims { uint prims[]; };
layout(set = 0, binding = 5, std430) readonly buffer Upload { uint upload[]; };
layout(set = 0, binding = 6, rgba8) uniform coherent image2D pictures[];  // the display's pictures and fields

layout(push_constant) uniform Push { uint p[32]; } pc;

float wordFloat(uint w) { return uintBitsToFloat(w); }

// ---- Arithmetic ---------------------------------------------------------------------------------

// a / b as IEEE single precision rounds it, toward zero with
// ROUND_TOWARD_ZERO, else to nearest even, in integers: the card's own
// division is only within 2.5 units of the last place.
float divide(float a, float b) {
    const uint ua = floatBitsToUint(a), ub = floatBitsToUint(b);
    const uint sign = (ua ^ ub) & 0x80000000u;
    const uint ea = (ua >> 23) & 0xFFu, eb = (ub >> 23) & 0xFFu;
    uint ma = ua & 0x7FFFFFu, mb = ub & 0x7FFFFFu;
    const uint nan = 0x7FC00000u, inf = 0x7F800000u;
    if (ea == 0xFFu || eb == 0xFFu) {
        if ((ea == 0xFFu && ma != 0u) || (eb == 0xFFu && mb != 0u)) return uintBitsToFloat(nan);
        if (ea == 0xFFu) return uintBitsToFloat(eb == 0xFFu ? nan : sign | inf);
        return uintBitsToFloat(sign);   // finite / infinity
    }
    if (eb == 0u && mb == 0u) return uintBitsToFloat(ea == 0u && ma == 0u ? nan : sign | inf);
    if (ea == 0u && ma == 0u) return uintBitsToFloat(sign);
    // Significands with their leading bit, denormals normalised.
    int expA = int(ea), expB = int(eb);
    if (ea == 0u) {
        const int top = findMSB(ma);
        ma <<= uint(23 - top);
        expA = 1 - (23 - top);
    } else {
        ma |= 0x800000u;
    }
    if (eb == 0u) {
        const int top = findMSB(mb);
        mb <<= uint(23 - top);
        expB = 1 - (23 - top);
    } else {
        mb |= 0x800000u;
    }
    // 26 bits of ma / mb (both in [2^23, 2^24)), long-hand, and whether a remainder is left.
    uint q = 0u, r = ma;
    for (int i = 0; i < 26; ++i) {
        q <<= 1;
        if (r >= mb) {
            r -= mb;
            q |= 1u;
        }
        r <<= 1;
    }
    // 24 bits of significand and 2 below them; the exponent's biased value.
    uint ext;
    int e;
    if (q >= (1u << 25)) {
        ext = q;
        e = expA - expB + 127;
    } else {
        ext = q << 1;
        e = expA - expB + 126;
    }
    bool sticky = r != 0u;
    if (e <= 0) {   // a denormal: shifted down, what falls off kept as sticky
        const int shift = 1 - e;
        if (shift >= 26) {
            sticky = sticky || ext != 0u;
            ext = 0u;
        } else {
            sticky = sticky || (ext & ((1u << shift) - 1u)) != 0u;
            ext >>= uint(shift);
        }
        e = 0;
    }
    uint mant = ext >> 2;
#ifndef ROUND_TOWARD_ZERO
    if (((ext >> 1) & 1u) != 0u && ((ext & 1u) != 0u || sticky || (mant & 1u) != 0u)) mant += 1u;
#endif
    if (e == 0) return uintBitsToFloat(sign | mant);   // a carry makes it the least normal number, as it should
    if (mant >= (1u << 24)) {
        mant >>= 1;
        e += 1;
    }
    if (e >= 255) {
#ifdef ROUND_TOWARD_ZERO
        return uintBitsToFloat(sign | 0x7F7FFFFFu);   // the largest finite number
#else
        return uintBitsToFloat(sign | inf);
#endif
    }
    return uintBitsToFloat(sign | (uint(e) << 23) | (mant & 0x7FFFFFu));
}

// static_cast<int>(c) on x86: toward zero, INT_MIN past the range and for NaN.
int truncInt(float c) {
    if (!(c >= -2147483648.0 && c < 2147483648.0)) return int(0x80000000u);
    return int(c);
}

// gs.cpp's floorInt: the floor, INT_MIN past the range.
int floorInt(float c) {
    const int i = truncInt(c);
    return float(i) > c && i != int(0x80000000u) ? i - 1 : i;
}

// gs.cpp's sixteenths: a texture coordinate in sixteenths of a texel, rounded down, saturating.
int sixteenths(float c) {
    precise const float f = c * 16.0;
    if (f >= 1073741824.0) return 1 << 30;
    if (!(f >= -1073741824.0)) return -(1 << 30);   // NaN too
    const int i = int(f);   // toward zero: one more than the floor below a negative fraction
    return float(i) > f ? i - 1 : i;
}

// ---- GS memory (the mirror) ----------------------------------------------------------------------

uint c32(uint z, uint y, uint x) { return tables[T_C32 + (z * 32u + y) * 64u + x]; }
uint c16(uint v, uint y, uint x) { return tables[T_C16 + (v * 64u + y) * 64u + x]; }
uint c8(uint y, uint x) { return tables[T_C8 + y * 128u + x]; }
uint c4(uint y, uint x) { return tables[T_C4 + y * 128u + x]; }

uint mirrorHalfword(uint n) { return (vram[(n >> 1) & 0xFFFFFu] >> ((n & 1u) * 16u)) & 0xFFFFu; }
uint mirrorByte(uint n) { return (vram[(n >> 2) & 0xFFFFFu] >> ((n & 3u) * 8u)) & 0xFFu; }
uint mirrorNibble(uint n) { return (vram[(n >> 3) & 0xFFFFFu] >> ((n & 7u) * 4u)) & 0xFu; }

// The 1:5:5:5 colour c as RGBA, its alpha from TEXA (gs.cpp's expand16).
uint expand16(uint c, uint ta0, uint ta1, bool aem) {
    const uint r = (c & 0x1Fu) << 3, g = ((c >> 5) & 0x1Fu) << 3, b = ((c >> 10) & 0x1Fu) << 3;
    uint a = (c & 0x8000u) != 0u ? ta1 : ta0;
    if (aem && (c & 0xFFFFu) == 0u) a = 0u;
    return r | (g << 8) | (b << 16) | (a << 24);
}

uint pack16(uint c) {
    return ((c >> 3) & 0x1Fu) | (((c >> 11) & 0x1Fu) << 5) | (((c >> 19) & 0x1Fu) << 10) | ((c >> 31) << 15);
}

// A texel's bits (gs.cpp's rawTexel) in GS memory, at (x, y) of a level whose
// first unit and row of pages are `base` and `row`, wrapped already.
uint rawTexelInMemory(uint psm, uint base, uint row, uint x, uint y) {
    if (psm == PSMCT16 || psm == PSMCT16S)
        return mirrorHalfword(base + (y >> 6) * row + (x >> 6) * 4096u + c16(psm == PSMCT16S ? 1u : 0u, y & 63u, x & 63u));
    if (psm == PSMT8) return mirrorByte(base + (y >> 6) * row + (x >> 7) * 8192u + c8(y & 63u, x & 127u));
    if (psm == PSMT4) return mirrorNibble((base + (y >> 7) * row + (x >> 7) * 16384u + c4(y & 127u, x & 127u)) & 0x7FFFFFu);
    const uint w = vram[(base + (y >> 5) * row + (x >> 6) * 2048u + c32(0u, y & 31u, x & 63u)) & 0xFFFFFu];
    if (psm == PSMCT24) return w & 0xFFFFFFu;
    if (psm == PSMT8H) return w >> 24;
    if (psm == PSMT4HL) return (w >> 24) & 0xFu;
    if (psm == PSMT4HH) return w >> 28;
    return w;
}

// The same bits from a target, which holds each pixel's unit in a word.
uint rawTexelInTarget(uint psm, uint image, ivec2 at) {
    const uint w = imageLoad(images[nonuniformEXT(image)], at).r;
    if (psm == PSMCT16 || psm == PSMCT16S) return w & 0xFFFFu;
    if (psm == PSMCT24) return w & 0xFFFFFFu;
    if (psm == PSMT8H) return w >> 24;
    if (psm == PSMT4HL) return (w >> 24) & 0xFu;
    if (psm == PSMT4HH) return w >> 28;
    return w;
}
