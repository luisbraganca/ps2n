// COP2: VU0 in macro mode (vector instructions issued inline by the EE).
//
// Float handling matches the FPU: operands with exponent 255 read as
// +/-FLT_MAX, denormals as 0, and results are clamped while the MAC and
// status flags are updated per component. vf0 is the constant (0, 0, 0, 1)
// and vi0 the constant 0; writes to either are discarded.
//
// Pipeline timing (Q/P latency, flag delays) is not modelled: every result is
// visible immediately, as if each instruction waited for the previous one.
#pragma once

#include "ps2/cpu.h"
#include "ps2/memory.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ps2 {

extern uint8_t vu0Mem[4096];      // VU0 data memory (EE address 0x11004000)
extern uint8_t vu0Micro[4096];    // VU0 micro memory (0x11000000)
extern uint8_t vu1Mem[16384];     // VU1 data memory (0x1100C000)
extern uint8_t vu1Micro[16384];   // VU1 micro memory (0x11008000)

// Runtime hooks (vu.cpp).
void vu0CallMicro(uint32_t addr);   // VCALLMS / VCALLMSR
void vu0Reset();
void vu1Reset();
void vu1Start(uint32_t addr);       // CTC2 to CMSAR1

namespace ops {
namespace vu {

inline float in(uint32_t f) {
    switch (f & 0x7F800000) {
    case 0: f &= 0x80000000; break;
    case 0x7F800000: f = (f & 0x80000000) | 0x7F7FFFFF; break;
    default: break;
    }
    return std::bit_cast<float>(f);
}

// Updates the MAC flags of one component (shift 3 = x ... 0 = w) and returns
// the clamped result bits.
inline uint32_t mac(int shift, float f) {
    const uint32_t v = std::bit_cast<uint32_t>(f);
    const uint32_t s = v & 0x80000000;
    uint32_t& m = cpu.vi[VU_MAC];
    if (s) m |= 0x0010u << shift;
    else m &= ~(0x0010u << shift);
    if (f == 0) {
        m = (m & ~(0x1100u << shift)) | (0x0001u << shift);
        return v;
    }
    switch ((v >> 23) & 0xFF) {
    case 0:
        m = (m & ~(0x1000u << shift)) | (0x0101u << shift);
        return s;
    case 255:
        m = (m & ~(0x0101u << shift)) | (0x1000u << shift);
        return s | 0x7F7FFFFF;
    default:
        m &= ~(0x1101u << shift);
        return v;
    }
}
inline void macClear(int shift) { cpu.vi[VU_MAC] &= ~(0x1111u << shift); }
inline void statusUpdate() {
    const uint32_t m = cpu.vi[VU_MAC];
    uint32_t f = 0;
    if (m & 0x000F) f |= 1;
    if (m & 0x00F0) f |= 2;
    if (m & 0x0F00) f |= 4;
    if (m & 0xF000) f |= 8;
    uint32_t& st = cpu.vi[VU_STATUS];
    st = (st & 0xFF0) | f | (f << 6);
}

inline VuReg* fd(uint32_t r) {
    static VuReg discard;
    return r ? &cpu.vf[r] : &discard;
}
inline bool on(uint32_t dest, int i) { return (dest & (8u >> i)) != 0; }

// Generic FMAC operation: dst[i] = f(acc[i], fs[i], b[i]) for enabled components.
template <typename F>
inline void fmac(uint32_t dest, VuReg* dst, uint32_t fs, const uint32_t (&b)[4], F f) {
    const VuReg a = cpu.vf[fs];
    const VuReg acc = cpu.vacc;
    for (int i = 0; i < 4; ++i) {
        if (on(dest, i)) dst->c[i] = mac(3 - i, f(acc.c[i], a.c[i], b[i]));
        else macClear(3 - i);
    }
    statusUpdate();
}
inline void vec(uint32_t ft, uint32_t (&b)[4]) { std::memcpy(b, cpu.vf[ft].c, 16); }
inline void bcast(uint32_t v, uint32_t (&b)[4]) { b[0] = b[1] = b[2] = b[3] = v; }

struct Add  { float operator()(uint32_t, uint32_t a, uint32_t b) const { return in(a) + in(b); } };
struct Sub  { float operator()(uint32_t, uint32_t a, uint32_t b) const { return in(a) - in(b); } };
struct Mul  { float operator()(uint32_t, uint32_t a, uint32_t b) const { return in(a) * in(b); } };
struct MAdd { float operator()(uint32_t c, uint32_t a, uint32_t b) const { return in(c) + in(a) * in(b); } };
struct MSub { float operator()(uint32_t c, uint32_t a, uint32_t b) const { return in(c) - in(a) * in(b); } };

inline uint32_t maxBits(uint32_t a, uint32_t b) {
    const auto sa = static_cast<int32_t>(a), sb = static_cast<int32_t>(b);
    return static_cast<uint32_t>((sa < 0 && sb < 0) ? (sa < sb ? sa : sb) : (sa > sb ? sa : sb));
}
inline uint32_t minBits(uint32_t a, uint32_t b) {
    const auto sa = static_cast<int32_t>(a), sb = static_cast<int32_t>(b);
    return static_cast<uint32_t>((sa < 0 && sb < 0) ? (sa > sb ? sa : sb) : (sa < sb ? sa : sb));
}
template <typename F>
inline void minmax(uint32_t dest, uint32_t fdr, uint32_t fs, const uint32_t (&b)[4], F f) {
    if (!fdr) return;
    const VuReg a = cpu.vf[fs];
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) cpu.vf[fdr].c[i] = f(a.c[i], b[i]);
}
template <typename F>
inline void unary(uint32_t dest, uint32_t ft, uint32_t fs, F f) {
    if (!ft) return;
    const VuReg a = cpu.vf[fs];
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) cpu.vf[ft].c[i] = f(a.c[i]);
}

inline uint32_t ftoi(uint32_t v, int frac) {
    float f = std::bit_cast<float>(v);
    if (frac) f *= std::bit_cast<float>(0x3F800000u + (static_cast<uint32_t>(frac) << 23));
    const uint32_t u = std::bit_cast<uint32_t>(f);
    if ((u & 0x7F800000) >= 0x4F000000) return (u & 0x80000000) ? 0x80000000u : 0x7FFFFFFFu;
    return static_cast<uint32_t>(static_cast<int32_t>(f));
}
inline uint32_t itof(uint32_t v, int frac) {
    float f = static_cast<float>(static_cast<int32_t>(v));
    if (frac) f *= std::bit_cast<float>(0x3F800000u - (static_cast<uint32_t>(frac) << 23));
    return std::bit_cast<uint32_t>(f);
}

inline uint8_t* mem(uint32_t qwAddr) { return vu0Mem + ((qwAddr * 16) & 0xFF0); }
inline uint16_t vi(uint32_t r) { return static_cast<uint16_t>(cpu.vi[r]); }
inline void setVi(uint32_t r, uint32_t v) {
    if (r) cpu.vi[r] = v & 0xFFFF;
}
inline void advanceR() {
    uint32_t r = cpu.vi[VU_R];
    const uint32_t x = (r >> 4) & 1, y = (r >> 22) & 1;
    r = (r << 1) ^ x ^ y;
    cpu.vi[VU_R] = (r & 0x7FFFFF) | 0x3F800000;
}
} // namespace vu

// ---- FMAC: vector, broadcast, Q and I forms --------------------------------
#define PS2_VU_FMAC_FAMILY(NAME, FN)                                                            \
    inline void NAME(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) {                    \
        uint32_t b[4];                                                                          \
        vu::vec(ft, b);                                                                         \
        vu::fmac(dest, vu::fd(fd), fs, b, vu::FN{});                                            \
    }                                                                                           \
    inline void NAME##bc(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) {   \
        uint32_t b[4];                                                                          \
        vu::bcast(cpu.vf[ft].c[bc], b);                                                         \
        vu::fmac(dest, vu::fd(fd), fs, b, vu::FN{});                                            \
    }                                                                                           \
    inline void NAME##q(uint32_t dest, uint32_t fd, uint32_t fs) {                              \
        uint32_t b[4];                                                                          \
        vu::bcast(cpu.vi[VU_Q], b);                                                             \
        vu::fmac(dest, vu::fd(fd), fs, b, vu::FN{});                                            \
    }                                                                                           \
    inline void NAME##i(uint32_t dest, uint32_t fd, uint32_t fs) {                              \
        uint32_t b[4];                                                                          \
        vu::bcast(cpu.vi[VU_I], b);                                                             \
        vu::fmac(dest, vu::fd(fd), fs, b, vu::FN{});                                            \
    }                                                                                           \
    inline void NAME##A(uint32_t dest, uint32_t fs, uint32_t ft) {                              \
        uint32_t b[4];                                                                          \
        vu::vec(ft, b);                                                                         \
        vu::fmac(dest, &cpu.vacc, fs, b, vu::FN{});                                             \
    }                                                                                           \
    inline void NAME##Abc(uint32_t dest, uint32_t fs, uint32_t ft, uint32_t bc) {               \
        uint32_t b[4];                                                                          \
        vu::bcast(cpu.vf[ft].c[bc], b);                                                         \
        vu::fmac(dest, &cpu.vacc, fs, b, vu::FN{});                                             \
    }                                                                                           \
    inline void NAME##Aq(uint32_t dest, uint32_t fs) {                                          \
        uint32_t b[4];                                                                          \
        vu::bcast(cpu.vi[VU_Q], b);                                                             \
        vu::fmac(dest, &cpu.vacc, fs, b, vu::FN{});                                             \
    }                                                                                           \
    inline void NAME##Ai(uint32_t dest, uint32_t fs) {                                          \
        uint32_t b[4];                                                                          \
        vu::bcast(cpu.vi[VU_I], b);                                                             \
        vu::fmac(dest, &cpu.vacc, fs, b, vu::FN{});                                             \
    }

PS2_VU_FMAC_FAMILY(VADD, Add)
PS2_VU_FMAC_FAMILY(VSUB, Sub)
PS2_VU_FMAC_FAMILY(VMUL, Mul)
PS2_VU_FMAC_FAMILY(VMADD, MAdd)
PS2_VU_FMAC_FAMILY(VMSUB, MSub)
#undef PS2_VU_FMAC_FAMILY

// Outer product (cross product helpers): only x, y, z take part.
inline void VOPMULA(uint32_t, uint32_t fs, uint32_t ft) {
    const VuReg a = cpu.vf[fs], b = cpu.vf[ft];
    cpu.vacc.c[0] = vu::mac(3, vu::in(a.c[1]) * vu::in(b.c[2]));
    cpu.vacc.c[1] = vu::mac(2, vu::in(a.c[2]) * vu::in(b.c[0]));
    cpu.vacc.c[2] = vu::mac(1, vu::in(a.c[0]) * vu::in(b.c[1]));
    vu::statusUpdate();
}
inline void VOPMSUB(uint32_t, uint32_t fd, uint32_t fs, uint32_t ft) {
    const VuReg a = cpu.vf[fs], b = cpu.vf[ft], acc = cpu.vacc;
    VuReg* d = vu::fd(fd);
    d->c[0] = vu::mac(3, vu::in(acc.c[0]) - vu::in(a.c[1]) * vu::in(b.c[2]));
    d->c[1] = vu::mac(2, vu::in(acc.c[1]) - vu::in(a.c[2]) * vu::in(b.c[0]));
    d->c[2] = vu::mac(1, vu::in(acc.c[2]) - vu::in(a.c[0]) * vu::in(b.c[1]));
    vu::statusUpdate();
}

// ---- min / max (no flags) -----------------------------------------------------
inline void VMAX(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) {
    uint32_t b[4];
    vu::vec(ft, b);
    vu::minmax(dest, fd, fs, b, vu::maxBits);
}
inline void VMAXbc(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) {
    uint32_t b[4];
    vu::bcast(cpu.vf[ft].c[bc], b);
    vu::minmax(dest, fd, fs, b, vu::maxBits);
}
inline void VMAXi(uint32_t dest, uint32_t fd, uint32_t fs) {
    uint32_t b[4];
    vu::bcast(cpu.vi[VU_I], b);
    vu::minmax(dest, fd, fs, b, vu::maxBits);
}
inline void VMINI(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft) {
    uint32_t b[4];
    vu::vec(ft, b);
    vu::minmax(dest, fd, fs, b, vu::minBits);
}
inline void VMINIbc(uint32_t dest, uint32_t fd, uint32_t fs, uint32_t ft, uint32_t bc) {
    uint32_t b[4];
    vu::bcast(cpu.vf[ft].c[bc], b);
    vu::minmax(dest, fd, fs, b, vu::minBits);
}
inline void VMINIi(uint32_t dest, uint32_t fd, uint32_t fs) {
    uint32_t b[4];
    vu::bcast(cpu.vi[VU_I], b);
    vu::minmax(dest, fd, fs, b, vu::minBits);
}

// ---- unary moves and conversions ----------------------------------------------
inline void VABS(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return v & 0x7FFFFFFFu; }); }
inline void VMOVE(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return v; }); }
inline void VMR32(uint32_t dest, uint32_t ft, uint32_t fs) {
    if (!ft) return;
    const VuReg a = cpu.vf[fs];
    const uint32_t rot[4] = {a.c[1], a.c[2], a.c[3], a.c[0]};
    for (int i = 0; i < 4; ++i)
        if (vu::on(dest, i)) cpu.vf[ft].c[i] = rot[i];
}
inline void VFTOI0(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return vu::ftoi(v, 0); }); }
inline void VFTOI4(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return vu::ftoi(v, 4); }); }
inline void VFTOI12(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return vu::ftoi(v, 12); }); }
inline void VFTOI15(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return vu::ftoi(v, 15); }); }
inline void VITOF0(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return vu::itof(v, 0); }); }
inline void VITOF4(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return vu::itof(v, 4); }); }
inline void VITOF12(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return vu::itof(v, 12); }); }
inline void VITOF15(uint32_t dest, uint32_t ft, uint32_t fs) { vu::unary(dest, ft, fs, [](uint32_t v) { return vu::itof(v, 15); }); }

// Clip test of fs.xyz against |ft.w|; the clip flag register keeps the last
// four results (6 bits each).
inline void VCLIPw(uint32_t, uint32_t fs, uint32_t ft) {
    auto w = static_cast<int32_t>(cpu.vf[ft].c[3]);
    w = (w & 0x7F800000) ? (w & 0x7FFFFFFF) : 0x007FFFFF;
    const VuReg a = cpu.vf[fs];
    uint32_t clip = cpu.vi[VU_CLIP] << 6;
    for (int i = 0; i < 3; ++i) {
        if (static_cast<int32_t>(a.c[i]) > w) clip |= 1u << (2 * i);
        if (static_cast<int32_t>(a.c[i] ^ 0x80000000u) > w) clip |= 2u << (2 * i);
    }
    cpu.vi[VU_CLIP] = clip & 0xFFFFFF;
}

// ---- divide unit (results land in Q) ------------------------------------------
inline void VDIV(uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
    const uint32_t tb = cpu.vf[ft].c[ftf], sb = cpu.vf[fs].c[fsf];
    const float t = vu::in(tb), s = vu::in(sb);
    cpu.vi[VU_STATUS] &= ~0x30u;
    if (t == 0.0f) {
        cpu.vi[VU_STATUS] |= (s == 0.0f) ? 0x10u : 0x20u;
        cpu.vi[VU_Q] = ((tb ^ sb) & 0x80000000u) ? 0xFF7FFFFFu : 0x7F7FFFFFu;
    } else {
        cpu.vi[VU_Q] = std::bit_cast<uint32_t>(vu::in(std::bit_cast<uint32_t>(s / t)));
    }
}
inline void VSQRT(uint32_t ft, uint32_t ftf) {
    const float t = vu::in(cpu.vf[ft].c[ftf]);
    cpu.vi[VU_STATUS] &= ~0x30u;
    if (t < 0.0f) cpu.vi[VU_STATUS] |= 0x10u;
    cpu.vi[VU_Q] = std::bit_cast<uint32_t>(vu::in(std::bit_cast<uint32_t>(std::sqrt(std::fabs(t)))));
}
inline void VRSQRT(uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
    const uint32_t tb = cpu.vf[ft].c[ftf], sb = cpu.vf[fs].c[fsf];
    const float t = vu::in(tb), s = vu::in(sb);
    cpu.vi[VU_STATUS] &= ~0x30u;
    if (t == 0.0f) {
        cpu.vi[VU_STATUS] |= 0x20u;
        if (s != 0.0f) {
            cpu.vi[VU_Q] = ((tb ^ sb) & 0x80000000u) ? 0xFF7FFFFFu : 0x7F7FFFFFu;
        } else {
            cpu.vi[VU_Q] = ((tb ^ sb) & 0x80000000u) ? 0x80000000u : 0u;
            cpu.vi[VU_STATUS] |= 0x10u;
        }
        return;
    }
    if (t < 0.0f) cpu.vi[VU_STATUS] |= 0x10u;
    cpu.vi[VU_Q] = std::bit_cast<uint32_t>(vu::in(std::bit_cast<uint32_t>(s / std::sqrt(std::fabs(t)))));
}
inline void VWAITQ() {}
inline void VNOP() {}

// ---- integer unit ---------------------------------------------------------------
inline void VIADD(uint32_t id, uint32_t is, uint32_t it) { vu::setVi(id, vu::vi(is) + vu::vi(it)); }
inline void VISUB(uint32_t id, uint32_t is, uint32_t it) { vu::setVi(id, vu::vi(is) - vu::vi(it)); }
inline void VIAND(uint32_t id, uint32_t is, uint32_t it) { vu::setVi(id, vu::vi(is) & vu::vi(it)); }
inline void VIOR(uint32_t id, uint32_t is, uint32_t it) { vu::setVi(id, vu::vi(is) | vu::vi(it)); }
inline void VIADDI(uint32_t it, uint32_t is, int32_t imm5) { vu::setVi(it, vu::vi(is) + static_cast<uint32_t>(imm5)); }
inline void VMTIR(uint32_t it, uint32_t fs, uint32_t fsf) { vu::setVi(it, cpu.vf[fs].c[fsf] & 0xFFFF); }
inline void VMFIR(uint32_t dest, uint32_t ft, uint32_t is) {
    if (!ft) return;
    const auto v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(vu::vi(is))));
    for (int i = 0; i < 4; ++i)
        if (vu::on(dest, i)) cpu.vf[ft].c[i] = v;
}

// ---- VU0 data memory -------------------------------------------------------------
inline void VLQI(uint32_t dest, uint32_t ft, uint32_t is) {
    if (ft) {
        const uint8_t* p = vu::mem(vu::vi(is));
        for (int i = 0; i < 4; ++i)
            if (vu::on(dest, i)) std::memcpy(&cpu.vf[ft].c[i], p + 4 * i, 4);
    }
    if (is) vu::setVi(is, vu::vi(is) + 1);
}
inline void VLQD(uint32_t dest, uint32_t ft, uint32_t is) {
    if (is) vu::setVi(is, vu::vi(is) - 1);
    if (!ft) return;
    const uint8_t* p = vu::mem(vu::vi(is));
    for (int i = 0; i < 4; ++i)
        if (vu::on(dest, i)) std::memcpy(&cpu.vf[ft].c[i], p + 4 * i, 4);
}
inline void VSQI(uint32_t dest, uint32_t fs, uint32_t it) {
    uint8_t* p = vu::mem(vu::vi(it));
    for (int i = 0; i < 4; ++i)
        if (vu::on(dest, i)) std::memcpy(p + 4 * i, &cpu.vf[fs].c[i], 4);
    if (it) vu::setVi(it, vu::vi(it) + 1);
}
inline void VSQD(uint32_t dest, uint32_t fs, uint32_t it) {
    if (it) vu::setVi(it, vu::vi(it) - 1);
    uint8_t* p = vu::mem(vu::vi(it));
    for (int i = 0; i < 4; ++i)
        if (vu::on(dest, i)) std::memcpy(p + 4 * i, &cpu.vf[fs].c[i], 4);
}
inline void VILWR(uint32_t dest, uint32_t it, uint32_t is) {
    if (!it) return;
    const uint8_t* p = vu::mem(vu::vi(is));
    for (int i = 0; i < 4; ++i)
        if (vu::on(dest, i)) {
            uint16_t v;
            std::memcpy(&v, p + 4 * i, 2);
            vu::setVi(it, v);
        }
}
inline void VISWR(uint32_t dest, uint32_t it, uint32_t is) {
    uint8_t* p = vu::mem(vu::vi(is));
    const uint32_t v = vu::vi(it);
    for (int i = 0; i < 4; ++i)
        if (vu::on(dest, i)) std::memcpy(p + 4 * i, &v, 4);
}

// ---- random number unit -----------------------------------------------------------
inline void VRINIT(uint32_t fs, uint32_t fsf) { cpu.vi[VU_R] = 0x3F800000u | (cpu.vf[fs].c[fsf] & 0x007FFFFFu); }
inline void VRXOR(uint32_t fs, uint32_t fsf) {
    cpu.vi[VU_R] = 0x3F800000u | ((cpu.vi[VU_R] ^ cpu.vf[fs].c[fsf]) & 0x007FFFFFu);
}
inline void VRGET(uint32_t dest, uint32_t ft) {
    if (!ft) return;
    for (int i = 0; i < 4; ++i)
        if (vu::on(dest, i)) cpu.vf[ft].c[i] = cpu.vi[VU_R];
}
inline void VRNEXT(uint32_t dest, uint32_t ft) {
    if (!ft) return;
    vu::advanceR();
    VRGET(dest, ft);
}

// ---- EE <-> VU0 transfers -----------------------------------------------------------
inline void QMFC2(uint32_t rt, uint32_t fs) {
    uint64_t v[2];
    std::memcpy(v, cpu.vf[fs].c, 16);
    set128(rt, v[0], v[1]);
}
inline void QMTC2(uint32_t rt, uint32_t fs) {
    if (fs) std::memcpy(cpu.vf[fs].c, cpu.gpr[rt].d, 16);
}
inline void CFC2(uint32_t rt, uint32_t id) {
    if (id == VU_R) setS32(rt, static_cast<int32_t>(cpu.vi[VU_R] & 0x7FFFFF));
    else setS32(rt, static_cast<int32_t>(cpu.vi[id]));
}
inline void CTC2(uint32_t rt, uint32_t id) {
    const uint32_t v = ru32(rt);
    switch (id) {
    case 0:
    case VU_MAC:
    case VU_TPC:
    case VU_VPU_STAT:
        return;   // vi0 is constant; the rest are read-only
    case VU_STATUS: cpu.vi[VU_STATUS] = (cpu.vi[VU_STATUS] & 0x3F) | (v & 0xFC0); return;
    case VU_CLIP: cpu.vi[VU_CLIP] = v & 0xFFFFFF; return;
    case VU_R: cpu.vi[VU_R] = (v & 0x7FFFFF) | 0x3F800000; return;
    case VU_FBRST:
        cpu.vi[VU_FBRST] = v & 0x0C0C;
        if (v & 0x002) vu0Reset();
        if (v & 0x200) vu1Reset();
        return;
    case VU_CMSAR1:
        cpu.vi[VU_CMSAR1] = v & 0xFFFF;
        vu1Start((v & 0xFFFF) * 8);
        return;
    default:
        cpu.vi[id] = id < 16 ? (v & 0xFFFF) : v;
        return;
    }
}
inline void LQC2(uint32_t ft, uint32_t base, int32_t off) {
    uint64_t v[2];
    read128(ru32(base) + static_cast<uint32_t>(off), v);
    if (ft) std::memcpy(cpu.vf[ft].c, v, 16);
}
inline void SQC2(uint32_t ft, uint32_t base, int32_t off) {
    uint64_t v[2];
    std::memcpy(v, cpu.vf[ft].c, 16);
    write128(ru32(base) + static_cast<uint32_t>(off), v);
}
inline void VCALLMS(uint32_t addr) { vu0CallMicro(addr); }
inline void VCALLMSR() { vu0CallMicro((cpu.vi[VU_CMSAR0] & 0xFFFF) * 8); }

} // namespace ops
} // namespace ps2
