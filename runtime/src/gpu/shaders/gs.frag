#version 460
// One pixel of a GS primitive, as the software rasterizer draws it (gs.cpp:
// drawTriangle's and drawSprite's arithmetic for the pixel's values, then
// plot()), on targets that hold each pixel's raw unit. The pixel's position
// is the GS's position k / N of image pixel k, so pixel N * px of the image
// gets exactly what the software draws at px: the same float operations,
// rounded the same way (the guest thread's round-toward-zero in the
// ROUND_TOWARD_ZERO variant), divisions exact (divide()), and doubles where
// the software uses them (FLOAT64). The depth test, the blend and the writes
// run in pixel interlock, so primitives reach a pixel in their order.
#extension GL_ARB_fragment_shader_interlock : require
#extension GL_EXT_spirv_intrinsics : require
#extension GL_EXT_terminate_invocation : require

#ifdef ROUND_TOWARD_ZERO
spirv_execution_mode(capabilities = [4468], 4463, 32);   // RoundingModeRTZ, floats
spirv_execution_mode(capabilities = [4466], 4461, 32);   // SignedZeroInfNanPreserve, floats
#ifdef FLOAT64
spirv_execution_mode(capabilities = [4467], 4462, 64);   // RoundingModeRTE, doubles: corrected toward zero below
#endif
#else
spirv_execution_mode(capabilities = [4467], 4462, 32);   // RoundingModeRTE, floats
spirv_execution_mode(capabilities = [4466], 4461, 32);
#endif

#include "common.glsl"

layout(pixel_interlock_ordered) in;
layout(location = 0) flat in uint primIndex;

uint P;   // the primitive's first word
uint S;   // its state's first word

uint sw(uint i) { return states[S + i]; }
float sf(uint i) { return uintBitsToFloat(states[S + i]); }
bool flag(uint f) { return (states[S + S_FLAGS] & f) != 0u; }
uint vu(uint v, uint field) { return prims[P + P_V0 + v * V_WORDS + field]; }
float vf(uint v, uint field) { return uintBitsToFloat(prims[P + P_V0 + v * V_WORDS + field]); }

#if defined(FLOAT64) && defined(ROUND_TOWARD_ZERO)
// Doubles round to nearest here; the software's round toward zero. One step
// toward zero, where the exact result was smaller than the rounded one.
double towardZero(double v) {
    uvec2 w = unpackDouble2x32(v);
    if (w.x == 0u) {
        w.x = 0xFFFFFFFFu;
        w.y -= 1u;
    } else {
        w.x -= 1u;
    }
    return packDouble2x32(w);
}
// a * b toward zero: Dekker's exact product says which side the rounded one fell.
double mulD(double a, double b) {
    precise const double p = a * b;
    precise const double ca = 134217729.0lf * a, cb = 134217729.0lf * b;
    precise const double ah = ca - (ca - a), al = a - ah;
    precise const double bh = cb - (cb - b), bl = b - bh;
    precise const double err = (((ah * bh - p) + ah * bl) + al * bh) + al * bl;
    return err != 0.0lf && (err < 0.0lf) != (p < 0.0lf) ? towardZero(p) : p;
}
// a + b toward zero: Knuth's two-sum gives the error.
double addD(double a, double b) {
    precise const double s = a + b;
    precise const double bb = s - a;
    precise const double err = (a - (s - bb)) + (b - bb);
    return err != 0.0lf && (err < 0.0lf) != (s < 0.0lf) ? towardZero(s) : s;
}
#elif defined(FLOAT64)
double mulD(double a, double b) { precise const double p = a * b; return p; }
double addD(double a, double b) { precise const double s = a + b; return s; }
#endif

// The GS's position of image pixel k: k itself at N = 1, and k / N above,
// whole for the pixels the GS samples.
float gsPos(int k, int n) {
    if (n == 1) return float(k);
    const int whole = k / n;
    return float(whole) + float(k - whole * n) / float(n);
}

// ---- Textures (gs.cpp: wrapCoord, texel, sampleLevel, levelOfDetail, sampleTexture) ----------------

uint wrapCoord(int c, int size, uint mode, int minv, int maxv) {
    if (mode == 0u) return uint(c & (size - 1));
    if (mode == 1u) return uint(c < 0 ? 0 : (size - 1 < c ? size - 1 : c));
    if (mode == 2u) return uint(c < minv ? minv : (maxv < c ? maxv : c));
    return uint((c & minv) | maxv);
}

// A coordinate in a target `sc` times the texture's resolution: repeats wrap
// whole texels and keep the sub-texel, clamps clamp the scaled coordinate.
uint wrapScaled(int c, int size, uint mode, int minv, int maxv, int sc) {
    const int texel = c >= 0 ? c / sc : -((-c + sc - 1) / sc);
    const int sub = c - texel * sc;
    if (mode == 0u) return uint((texel & (size - 1)) * sc + sub);
    if (mode == 1u) return uint(c < 0 ? 0 : (size * sc - 1 < c ? size * sc - 1 : c));
    if (mode == 2u) return uint(c < minv * sc ? minv * sc : (maxv * sc + sc - 1 < c ? maxv * sc + sc - 1 : c));
    return uint(((texel & minv) | maxv) * sc + sub);
}

uint clutColour(uint index) {
    const uint at = sw(S_CLUTBASE) + tables[sw(S_CLUTTABLE) + index];
    if (flag(F_CLUTWIDE)) return vram[at & 0xFFFFFu];
    return expand16(mirrorHalfword(at & 0x1FFFFFu), sw(S_TA0), sw(S_TA1), flag(F_AEM));
}

// The texel at (x, y) of level `lvl`, wrapped already (scaled for a target), as RGBA.
uint texelAt(uint lvl, uint x, uint y) {
    if (flag(F_ANYFORMAT)) return 0xFFFFFFFFu;
    const uint l = S + S_LEVELS + lvl * L_WORDS;
    const uint source = states[l + L_SOURCE], psm = sw(S_TPSM);
    uint c;
    if ((source & L_FROM_TARGET) != 0u) {
        // The page GS memory keeps the texel in (one past the buffer's width
        // is on the next row of pages, as a texture wider than its buffer
        // reads): where the target holds it, the texel's pixels in it at the
        // target's scale; elsewhere (a texture declared larger than the
        // picture, reaching into another buffer) the texel in GS memory.
        const uint sc = states[l + L_SCALE], fbw = sw(S_SOURCEFBW);
        const uint pageH = psm == PSMCT16 || psm == PSMCT16S ? 64u : 32u;
        const uint tx = x / sc, ty = y / sc;
        const uint rel = (states[l + L_ROW] / pageH + ty / pageH) * fbw + states[l + L_BASE] / 64u + tx / 64u;
        const uint page = (sw(S_SOURCEBASE) + rel) & 511u;
        if ((states[S + S_SOURCEPAGES + (page >> 5u)] & (1u << (page & 31u))) != 0u) {
            const uvec2 at = uvec2((rel % fbw) * 64u + (tx & 63u), (rel / fbw) * pageH + ty % pageH) * sc + uvec2(x % sc, y % sc);
            c = rawTexelInTarget(psm, source & 0xFFFFu, ivec2(at));
        } else {
            c = rawTexelInMemory(psm, sw(S_SOURCEUNIT), sw(S_SOURCEROW), tx, ty);
        }
    } else {
        c = rawTexelInMemory(psm, states[l + L_BASE], states[l + L_ROW], x, y);
    }
    if (psm == PSMCT32) return c;
    if (psm == PSMCT24) return c | ((flag(F_AEM) && c == 0u) ? 0u : sw(S_TA0) << 24);
    if (psm == PSMCT16 || psm == PSMCT16S) return expand16(c, sw(S_TA0), sw(S_TA1), flag(F_AEM));
    return clutColour(c);
}

const uint kLanes = 0x00FF00FFu;

uint sampleLevel(uint n, bool linear, float u, float v) {
    const uint l = S + S_LEVELS + n * L_WORDS;
    const int tw = int(states[l + L_TW]), th = int(states[l + L_TH]);
    const int minu = int(states[l + L_MINU]), maxu = int(states[l + L_MAXU]);
    const int minv = int(states[l + L_MINV]), maxv = int(states[l + L_MAXV]);
    const int sc = int(states[l + L_SCALE]);
    const uint wms = sw(S_WMS), wmt = sw(S_WMT);
    int su, sv;
    if (sc == 1) {   // gs.cpp's, exactly
        if (!linear)
            return texelAt(n, wrapCoord(floorInt(u) >> n, tw, wms, minu, maxu), wrapCoord(floorInt(v) >> n, th, wmt, minv, maxv));
        su = (sixteenths(u) >> n) - 8;
        sv = (sixteenths(v) >> n) - 8;
    } else {
        // A target, sampled at its own resolution. Its pixel m holds what the
        // GS draws at position m / sc, and texture coordinate u is position
        // u - 1/2 (a texel's centre is half a texel in): nearest takes the
        // pixel nearest to it, bilinear the two around it, as the GS does at
        // sc = 1. So a copy through texel centres copies every pixel.
        precise const float us = u * float(sc), vs = v * float(sc);
        if (!linear)
            return texelAt(n, wrapScaled((sixteenths(us) - 8 * (sc - 1)) >> 4, tw, wms, minu, maxu, sc),
                           wrapScaled((sixteenths(vs) - 8 * (sc - 1)) >> 4, th, wmt, minv, maxv, sc));
        su = sixteenths(us) - 8 * sc;
        sv = sixteenths(vs) - 8 * sc;
    }
    const int x = su >> 4, y = sv >> 4;
    const uint fx = uint(su & 15), fy = uint(sv & 15);
    const uint x0 = sc == 1 ? wrapCoord(x, tw, wms, minu, maxu) : wrapScaled(x, tw, wms, minu, maxu, sc);
    const uint y0 = sc == 1 ? wrapCoord(y, th, wmt, minv, maxv) : wrapScaled(y, th, wmt, minv, maxv, sc);
    const uint c00 = texelAt(n, x0, y0);
    if ((fx | fy) == 0u) return c00;
    // A texel whose weight is 0 is not read.
    const uint x1 = fx != 0u ? (sc == 1 ? wrapCoord(x + 1, tw, wms, minu, maxu) : wrapScaled(x + 1, tw, wms, minu, maxu, sc)) : 0u;
    const uint y1 = fy != 0u ? (sc == 1 ? wrapCoord(y + 1, th, wmt, minv, maxv) : wrapScaled(y + 1, th, wmt, minv, maxv, sc)) : 0u;
    const uint c10 = fx != 0u ? texelAt(n, x1, y0) : 0u, c01 = fy != 0u ? texelAt(n, x0, y1) : 0u;
    const uint c11 = fx != 0u && fy != 0u ? texelAt(n, x1, y1) : 0u;
    const uint w00 = (16u - fx) * (16u - fy), w10 = fx * (16u - fy), w01 = (16u - fx) * fy, w11 = fx * fy;
    const uint rb = ((c00 & kLanes) * w00 + (c10 & kLanes) * w10 + (c01 & kLanes) * w01 + (c11 & kLanes) * w11) >> 8;
    const uint ga = (((c00 >> 8) & kLanes) * w00 + ((c10 >> 8) & kLanes) * w10 + ((c01 >> 8) & kLanes) * w01 +
                     ((c11 >> 8) & kLanes) * w11) >> 8;
    return (rb & kLanes) | (ga & kLanes) << 8;
}

int levelOfDetail(uint bits) {
    const int e = int(bits >> 23);
    if (e == 0) return 1 << 20;   // kFarthest
    const uint m = bits & 0x7FFFFFu;
    const int lodK = int(sw(S_LODK)), lodL = int(sw(S_LODL));
    int steps = 0;
    if (m != 0u) {
        const uint b = m >> 15;
        steps = int(tables[T_LOG2BEFORE + b]) + (m >= tables[T_LOG2START + b] ? 1 : 0);
        steps = (steps >> (3 - lodL)) + 1;
    }
    return lodK - (e - 127) * (16 << lodL) - steps;
}

uint sampleTexture(float u, float v, float q) {
    const int mipLevels = int(sw(S_MIPLEVELS));
    if (!flag(F_LODBYQ) && mipLevels == 0) return sampleLevel(0u, flag(F_LINEAR), u, v);
    int lod = int(sw(S_LODK));
    if (flag(F_LODBYQ)) {
        const uint bits = floatBitsToUint(q) & 0x7FFFFFFFu;
        lod = bits >= sw(S_MAGFROM) ? -1 : levelOfDetail(bits);
    }
    if (lod < 0) return sampleLevel(0u, flag(F_MAGLINEAR), u, v);
    const bool minLinear = flag(F_MINLINEAR);
    if (mipLevels == 0) return sampleLevel(0u, minLinear, u, v);
    if (!flag(F_MIPLINEAR)) return sampleLevel(uint(min((lod + 8) >> 4, mipLevels)), minLinear, u, v);
    const int n = lod >> 4;
    if (n >= mipLevels) return sampleLevel(uint(mipLevels), minLinear, u, v);
    const uint a = sampleLevel(uint(n), minLinear, u, v);
    const uint f = uint(lod & 15);
    if (f == 0u) return a;
    const uint b = sampleLevel(uint(n + 1), minLinear, u, v);
    const uint rb = ((a & kLanes) * (16u - f) + (b & kLanes) * f) >> 4;
    const uint ga = (((a >> 8) & kLanes) * (16u - f) + ((b >> 8) & kLanes) * f) >> 4;
    return (rb & kLanes) | (ga & kLanes) << 8;
}

bool alphaTestPass(uint a) {
    if (!flag(F_ATE)) return true;
    const uint aref = sw(S_AREF);
    switch (sw(S_ATST)) {
    case 0u: return false;
    case 1u: return true;
    case 2u: return a < aref;
    case 3u: return a <= aref;
    case 4u: return a == aref;
    case 5u: return a >= aref;
    case 6u: return a > aref;
    default: return a != aref;
    }
}

int pick(uint sel, uint s, uint d) { return sel == 0u ? int(s) : sel == 1u ? int(d) : 0; }

// Where a target keeps image pixel k of a buffer `fbw` pages across: k
// itself, or for a pixel past the buffer's width, where GS memory keeps it,
// on the next row of pages (a scissor wider than the buffer draws there).
ivec2 inBuffer(ivec2 k, int n, uint fbw, uint pageH) {
    if (k.x < int(fbw) * 64 * n) return k;
    const uint x = uint(k.x / n), y = uint(k.y / n);
    const uint page = (y / pageH) * fbw + x / 64u;
    return ivec2(int(((page % fbw) * 64u + (x & 63u)) * uint(n)) + k.x % n, int(((page / fbw) * pageH + y % pageH) * uint(n)) + k.y % n);
}

uint blendChannel(uint s, uint d, int c, uint blend, bool colclamp) {
    const int v = ((pick(blend & 3u, s, d) - pick((blend >> 2) & 3u, s, d)) * c >> 7) + pick((blend >> 6) & 3u, s, d);
    return colclamp ? uint(clamp(v, 0, 255)) : uint(v & 0xFF);
}

void main() {
    P = primIndex * PRIM_WORDS;
    S = prims[P + P_STATE];
    const int n = int(pc.p[0]);
    const ivec2 k = ivec2(gl_FragCoord.xy);
    if (k.x < int(sw(S_SCX0)) * n || k.x >= (int(sw(S_SCX1)) + 1) * n || k.y < int(sw(S_SCY0)) * n ||
        k.y >= (int(sw(S_SCY1)) + 1) * n)
        terminateInvocation;   // outside the scissor: ends here, before the interlock
    const float lx = gsPos(k.x, n), ly = gsPos(k.y, n);

    // The pixel's values, as the software works them out.
    uint z = 0u;
    float r, g, b, a, u = 0.0, v = 0.0, q, fog = 0.0;
    bool textured = flag(F_TEX);
    const uint kind = prims[P + P_KIND];
    if (kind == KIND_TRIANGLE) {
        const float area = wordFloat(prims[P + P_AREA]);
        precise const float dy0 = vf(0u, V_Y) - ly, dy1 = vf(1u, V_Y) - ly, dy2 = vf(2u, V_Y) - ly;
        precise const float n0 = (vf(1u, V_X) - lx) * dy2 - (vf(2u, V_X) - lx) * dy1;
        precise const float n1 = (vf(2u, V_X) - lx) * dy0 - (vf(0u, V_X) - lx) * dy2;
        const float w0 = divide(n0, area), w1 = divide(n1, area);
        precise const float w2 = 1.0 - w0 - w1;
#define LERP(field) (vf(0u, field) + (vf(1u, field) - vf(0u, field)) * w1 + (vf(2u, field) - vf(0u, field)) * w2)
        if (flag(F_ZTE)) {
            const uint z0 = vu(0u, V_Z), z1 = vu(1u, V_Z), z2 = vu(2u, V_Z);
            if (z0 == z1 && z0 == z2) {
                z = z0;
            } else {
#ifdef FLOAT64
                precise const double zd = addD(addD(double(z0), mulD(double(w1), double(z1) - double(z0))), mulD(double(w2), double(z2) - double(z0)));
                z = uint(clamp(zd, 0.0lf, 4294967295.0lf));
#else
                precise const float zf = float(z0) + w1 * (float(z1) - float(z0)) + w2 * (float(z2) - float(z0));
                z = uint(clamp(zf, 0.0, 4294967040.0));
#endif
            }
        }
        if (flag(F_GOURAUD)) {
            precise const float lr = LERP(V_R), lg = LERP(V_G), lb = LERP(V_B), la = LERP(V_A);
            r = lr;
            g = lg;
            b = lb;
            a = la;
        } else {
            r = vf(2u, V_R);
            g = vf(2u, V_G);
            b = vf(2u, V_B);
            a = vf(2u, V_A);
        }
        q = vf(2u, V_Q);
        if (textured) {
            if (!flag(F_FST)) {
                precise const float s = LERP(V_S), t = LERP(V_T), lq = LERP(V_Q);
                q = lq;
                const float iq = q != 0.0 ? divide(1.0, q) : 1.0;
                precise const float tu = s * iq * sf(S_TWF), tv = t * iq * sf(S_THF);
                u = tu;
                v = tv;
            } else {
                precise const float lu = LERP(V_U), lv = LERP(V_V);
                u = lu;
                v = lv;
            }
        }
        if (flag(F_FGE)) {
            precise const float lf = LERP(V_FOG);
            fog = lf;
        }
#undef LERP
    } else if (kind == KIND_SPRITE) {
        const float ax = vf(0u, V_X), ay = vf(0u, V_Y);
        precise const float dx = vf(1u, V_X) - ax, dy = vf(1u, V_Y) - ay;
        const float ua = wordFloat(prims[P + P_UA]), va = wordFloat(prims[P + P_VA]);
        const float ub = wordFloat(prims[P + P_UB]), vb = wordFloat(prims[P + P_VB]);
        const float fy = dy != 0.0 ? divide(ly - ay, dy) : 0.0;
        precise const float sv = va + (vb - va) * fy;
        precise const float su = ua + (ub - ua) * (dx != 0.0 ? divide(lx - ax, dx) : 0.0);
        u = su;
        v = sv;
        z = vu(1u, V_Z);
        r = vf(1u, V_R);
        g = vf(1u, V_G);
        b = vf(1u, V_B);
        a = vf(1u, V_A);
        q = vf(1u, V_Q);
        fog = vf(1u, V_FOG);
    } else {   // a line's or a point's pixel
        textured = false;
        z = vu(0u, V_Z);
        r = vf(0u, V_R);
        g = vf(0u, V_G);
        b = vf(0u, V_B);
        a = vf(0u, V_A);
        q = vf(0u, V_Q);
        fog = vf(0u, V_FOG);
    }

    // plot(): colour, texture, fog and the alpha test need no other pixel.
    uint cr = uint(clamp(r, 0.0, 255.0)), cg = uint(clamp(g, 0.0, 255.0)), cb = uint(clamp(b, 0.0, 255.0));
    uint ca = uint(clamp(a, 0.0, 255.0));
    if (textured) {
        const uint t = sampleTexture(u, v, q);
        const uint tr = t & 0xFFu, tg = (t >> 8) & 0xFFu, tb = (t >> 16) & 0xFFu, ta = t >> 24;
        const bool tcc = flag(F_TCC);
        switch (sw(S_TFX)) {
        case 0u:   // MODULATE
            cr = min(255u, (tr * cr) >> 7);
            cg = min(255u, (tg * cg) >> 7);
            cb = min(255u, (tb * cb) >> 7);
            if (tcc) ca = min(255u, (ta * ca) >> 7);
            break;
        case 1u:   // DECAL
            cr = tr;
            cg = tg;
            cb = tb;
            if (tcc) ca = ta;
            break;
        case 2u:   // HIGHLIGHT
            cr = min(255u, ((tr * cr) >> 7) + ca);
            cg = min(255u, ((tg * cg) >> 7) + ca);
            cb = min(255u, ((tb * cb) >> 7) + ca);
            if (tcc) ca = min(255u, ta + ca);
            break;
        default:   // HIGHLIGHT2
            cr = min(255u, ((tr * cr) >> 7) + ca);
            cg = min(255u, ((tg * cg) >> 7) + ca);
            cb = min(255u, ((tb * cb) >> 7) + ca);
            if (tcc) ca = ta;
            break;
        }
    }
    if (flag(F_FGE)) {
        const uint f = uint(clamp(fog, 0.0, 255.0));
        cr = (f * cr + (255u - f) * sw(S_FOGR)) >> 8;
        cg = (f * cg + (255u - f) * sw(S_FOGG)) >> 8;
        cb = (f * cb + (255u - f) * sw(S_FOGB)) >> 8;
    }
    bool drawn = true, writeFb = true, writeZ = !flag(F_ZMSK), writeA = true;
    if (!alphaTestPass(ca)) {
        switch (sw(S_AFAIL)) {
        case 0u: drawn = false; break;
        case 1u: writeZ = false; break;
        case 2u: writeFb = false; break;
        default: writeZ = false; writeA = false; break;
        }
    }
    const bool depth = flag(F_ZTE) && !flag(F_NODEPTH);
    const uint fpsm = sw(S_FPSM), zpsm = sw(S_ZPSM);
    const uint frame = sw(S_FRAME), depthImage = sw(S_DEPTH);
    const bool fb16 = flag(F_FB16);
    // The pixel in the frame's and the depth buffer's targets.
    const ivec2 fk = inBuffer(k, n, sw(S_FBW), fpsm == PSMCT16 || fpsm == PSMCT16S ? 64u : 32u);
    const ivec2 dk = inBuffer(k, n, sw(S_FBW), zpsm == PSMZ16 || zpsm == PSMZ16S ? 64u : 32u);
    const bool inFrame = fk.x < int(sw(S_FRAMEW)) && fk.y < int(sw(S_FRAMEH));
    const bool inDepth = dk.x < int(sw(S_DEPTHW)) && dk.y < int(sw(S_DEPTHH));

    beginInvocationInterlockARB();
    // The depth test came first in the software: a pixel that fails it writes nothing.
    uint zc = 0u;
    if (flag(F_ZTE)) {
        if (sw(S_ZTST) == 0u) {
            drawn = false;   // NEVER
        } else if (depth) {
            zc = min(z, sw(S_ZMAX));
            if (sw(S_ZTST) >= 2u && inDepth) {
                uint zd = imageLoad(images[nonuniformEXT(depthImage)], dk).r;
                if (zpsm == PSMZ24) zd &= 0xFFFFFFu;
                else if (zpsm == PSMZ16 || zpsm == PSMZ16S) zd &= 0xFFFFu;
                if (sw(S_ZTST) == 2u ? zc < zd : zc <= zd) drawn = false;
            }
        }
    }
    if (drawn && depth && writeZ && inDepth) {
        uint word = zc;
        if (zpsm == PSMZ24) word = (imageLoad(images[nonuniformEXT(depthImage)], dk).r & 0xFF000000u) | (zc & 0xFFFFFFu);
        else if (zpsm == PSMZ16 || zpsm == PSMZ16S) word = zc & 0xFFFFu;
        imageStore(images[nonuniformEXT(depthImage)], dk, uvec4(word));
    }
    if (drawn && writeFb && inFrame) {
        const uint old = imageLoad(images[nonuniformEXT(frame)], fk).r;
        const uint dst = fpsm == PSMCT24 ? old & 0xFFFFFFu : fb16 ? old & 0xFFFFu : old;
        const uint d = fb16 ? expand16(dst, 0u, 0x80u, false) : dst;
        if (flag(F_ABE) && !(flag(F_PABE) && (ca & 0x80u) == 0u)) {
            const uint blend = sw(S_BLEND), bc = (blend >> 4) & 3u;
            const int c = bc == 0u ? int(ca) : bc == 1u ? int(d >> 24) : int(sw(S_FIX));
            const bool colclamp = flag(F_COLCLAMP);
            cr = blendChannel(cr, d & 0xFFu, c, blend, colclamp);
            cg = blendChannel(cg, (d >> 8) & 0xFFu, c, blend, colclamp);
            cb = blendChannel(cb, (d >> 16) & 0xFFu, c, blend, colclamp);
        }
        if (flag(F_FBA)) ca |= 0x80u;
        uint colour = cr | (cg << 8) | (cb << 16) | (ca << 24);
        if (!writeA) colour = (colour & 0x00FFFFFFu) | (d & 0xFF000000u);
        const uint mask = fb16 ? sw(S_FBMSK16) : sw(S_FBMSK);
        const uint value = ((fb16 ? pack16(colour) : colour) & ~mask) | (dst & mask);
        const uint word = fpsm == PSMCT24 ? (old & 0xFF000000u) | (value & 0xFFFFFFu) : fb16 ? value & 0xFFFFu : value;
        imageStore(images[nonuniformEXT(frame)], fk, uvec4(word));
    }
    endInvocationInterlockARB();
}
