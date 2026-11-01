// VU micro mode: the state of a vector unit running a micro program, and what
// its operations do to values. The interpreter (runtime/src/vu.cpp) and
// recompiled micro programs both run these operations; which pair stalls,
// when results land and what each half sends down the pipeline come from
// ps2n/vu_micro.h, which the recompiler reads too.
//
// Every operation takes the unit and its own instruction word. upperFn() and
// lowerFn() map a word to its operation; they are constexpr, so recompiled
// code calls the operation directly, chosen at compile time from the same
// decoding the interpreter does at run time.
#pragma once

#include "ps2/compiler.h"
#include "ps2/cpu.h"
#include "ps2/ops_vu0.h"
#include "ps2n/vu_micro.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ps2::vu {

using ps2n::vu::fBc;
using ps2n::vu::fDest;
using ps2n::vu::fExt;
using ps2n::vu::fFd;
using ps2n::vu::fFs;
using ps2n::vu::fFsf;
using ps2n::vu::fFt;
using ps2n::vu::fFtf;
using ps2n::vu::imm11;
using ps2n::vu::imm12;
using ps2n::vu::imm15;
using ps2n::vu::imm5;
using ps2n::vu::on;
using ops::vu::in;

constexpr uint32_t STATUS = ps2n::vu::kStatus, MAC = ps2n::vu::kMac, CLIP = ps2n::vu::kClip, R = ps2n::vu::kR,
                   I = ps2n::vu::kI, Q = ps2n::vu::kQ, P = ps2n::vu::kP, TPC = ps2n::vu::kTpc;

// A vector unit in micro mode: its registers and memories, the pipeline's
// timing, and the values on their way through the pipeline.
struct Unit {
    int index;
    VuReg* vf;
    VuReg* acc;
    uint32_t* vi;
    uint8_t* data;
    uint32_t dataMask;    // byte mask of data memory
    uint8_t* micro;
    uint32_t microMask;

    ps2n::vu::Timing t;
    // Flags as computed by the most recent instructions (not yet visible).
    uint32_t macNext = 0, statusNext = 0, clipNext = 0;
    struct Flags {
        uint32_t mac, status, clipv;
    };
    Flags ring[8] = {};        // the flags in flight, by the timing's ring slot
    struct {
        uint32_t q = 0, status = 0;
    } fdiv;                    // the FDIV result on its way to Q
    uint32_t efuP = 0;         // the EFU result on its way to P
    uint32_t qRead = 0;        // Q as the pair's upper half read it
    // Branches.
    int branch = 0;
    uint32_t branchPc = 0, delayPc = 0;
    bool delayBranch = false;
    int ebit = 0;
    // A VI register written by the previous integer instruction, and its old
    // value (the timing counts how long a branch still sees it).
    uint32_t viBackupReg = 0;
    uint16_t viBackupValue = 0;
    uint64_t steps = 0;           // pairs the current program has run
    std::vector<uint64_t> kick;   // the GIF packet of an XGKICK
};

// The units (vu.cpp): 0 runs on the EE's COP2 registers, 1 on its own.
Unit& unit(int index);

// Reports an operation micro code should not reach (vu.cpp).
void unknown(Unit& u, const char* which, uint32_t c);
// XGKICK (vu.cpp): sends the GIF packet at VI[is] down PATH1.
void xgkick(Unit& u, uint32_t c);
// XTOP / XITOP (vu.cpp): VIF1's TOP, the unit's VIF's ITOP.
uint32_t vifTop();
uint32_t vifItop(int unit);

// ---- flags -----------------------------------------------------------------------------
// One component's result: its MAC flags (Z, S at bit 4, U at bit 8, O at bit
// 12) and its clamped bits.
PS2_ALWAYS_INLINE uint32_t macLane(float f, uint32_t& flags) {
    const uint32_t v = std::bit_cast<uint32_t>(f);
    const uint32_t s = v & 0x80000000u, e = (v >> 23) & 0xFF;
    if (e - 1 < 254) [[likely]] {   // normal: only the sign
        flags = s >> 27;
        return v;
    }
    const bool zero = f == 0, under = !zero && e == 0, over = e == 255;
    flags = (s >> 27) | (zero || under ? 0x0001u : 0) | (under ? 0x0100u : 0) | (over ? 0x1000u : 0);
    return under ? s : over ? s | 0x7F7FFFFFu : v;
}

inline void statusFromMac(Unit& u) {
    const uint32_t m = u.macNext;
    uint32_t f = 0;
    if (m & 0x000F) f |= 1;
    if (m & 0x00F0) f |= 2;
    if (m & 0x0F00) f |= 4;
    if (m & 0xF000) f |= 8;
    u.statusNext = (u.statusNext & ~0xFu) | f;
}

// An FMAC operation's MAC flags replace all 16 (an unwritten component's are
// cleared) and set the status flags.
inline void setMac(Unit& u, uint32_t mac) {
    u.macNext = (u.macNext & ~0xFFFFu) | mac;
    statusFromMac(u);
}

// A pair's flags land: they become the flag registers.
PS2_ALWAYS_INLINE void commitFlags(Unit& u, const Unit::Flags& e, bool clip, bool fsset) {
    if (clip) u.vi[CLIP] = e.clipv;
    uint32_t& st = u.vi[STATUS];
    if (fsset) st = (st & 0x30) | (e.status & 0xFC0) | (e.status & 0xF);
    else st = (st & 0xFF0) | (e.status & 0xF) | ((e.status & 0xF) << 6);
    u.vi[MAC] = e.mac;
}
// The FDIV result lands in Q, with its status bits (D, I and their sticky copies).
PS2_ALWAYS_INLINE void landQ(Unit& u) {
    u.vi[Q] = u.fdiv.q;
    u.vi[STATUS] = (u.vi[STATUS] & 0xFCF) | (u.fdiv.status & 0x30) | ((u.fdiv.status & 0x30) << 6);
}
PS2_ALWAYS_INLINE void landP(Unit& u) { u.vi[P] = u.efuP; }

// ---- VF and VI access ----------------------------------------------------------------------
inline VuReg* vfDst(Unit& u, uint32_t r) {
    static VuReg discard;
    return r ? &u.vf[r] : &discard;
}
inline uint16_t viRead(const Unit& u, uint32_t r) { return static_cast<uint16_t>(u.vi[r & 15]); }
// `integer`: a result of the integer unit (or an address increment), which a
// branch right after it does not see yet.
inline void viWrite(Unit& u, uint32_t r, uint32_t v, bool integer = true) {
    r &= 15;
    if (!r) return;
    if (!integer) {
        u.vi[r] = v & 0xFFFF;
        return;
    }
    if (u.t.viBackupCycles && r == u.viBackupReg) {
        u.t.viBackupCycles = 2;   // keep the value from before the chain of writes
    } else {
        u.t.viBackupCycles = 2;
        u.viBackupReg = r;
        u.viBackupValue = static_cast<uint16_t>(u.vi[r]);
    }
    u.vi[r] = v & 0xFFFF;
}
// What a branch reads: the old value if the previous instruction just wrote it.
inline uint16_t viBranch(const Unit& u, uint32_t r) {
    r &= 15;
    if (u.t.viBackupCycles > 0 && r == u.viBackupReg) return u.viBackupValue;
    return static_cast<uint16_t>(u.vi[r]);
}

// ---- data memory ---------------------------------------------------------------------------
// VU0 sees VU1's registers at qword 0x400 onwards (VF0-31, then VI0-15).
uint8_t* vu0Window(uint32_t qw);   // vu.cpp
inline uint8_t* qwPtr(Unit& u, uint32_t qw) {
    if (u.index == 0 && (qw & 0x7FF) >= 0x400) return vu0Window(qw);
    return u.data + ((qw * 16) & u.dataMask);
}

using Op = void (*)(Unit&, uint32_t);   // one half of a pair, given its word

// ---- upper (FMAC) operations ----------------------------------------------------------------
// The components dest names take the clamped results; the MAC flags are those
// of the results written. Stalls are the timing's business: by the time an
// operation runs, its sources have landed; a Q operand is the Q the pair read.
template <typename F>
PS2_ALWAYS_INLINE void fmacOp(Unit& u, VuReg* dst, uint32_t dest, uint32_t fs, const uint32_t (&b)[4], F f) {
    const VuReg a = u.vf[fs], acc = *u.acc, old = *dst;
    uint32_t r[4], flags[4];
    for (int i = 0; i < 4; ++i) {
        const bool w = on(dest, i);
        const uint32_t v = macLane(f(acc.c[i], a.c[i], b[i]), flags[i]);
        r[i] = w ? v : old.c[i];
        flags[i] = w ? flags[i] : 0;
    }
    std::memcpy(dst->c, r, 16);
    setMac(u, (flags[0] << 3) | (flags[1] << 2) | (flags[2] << 1) | flags[3]);
}

struct Add  { float operator()(uint32_t, uint32_t a, uint32_t b) const { return in(a) + in(b); } };
struct Sub  { float operator()(uint32_t, uint32_t a, uint32_t b) const { return in(a) - in(b); } };
struct Mul  { float operator()(uint32_t, uint32_t a, uint32_t b) const { return in(a) * in(b); } };
struct MAdd { float operator()(uint32_t c, uint32_t a, uint32_t b) const { const float p = in(a) * in(b); return in(c) + p; } };
struct MSub { float operator()(uint32_t c, uint32_t a, uint32_t b) const { const float p = in(a) * in(b); return in(c) - p; } };

// Form: 0 vector, 1 broadcast (bc field), 2 Q, 3 I. Dest: the components
// written, for the usual xyzw and xyz; 0 takes them from the word.
template <typename F, int Form, bool ToAcc, uint32_t Dest>
void fmacFamily(Unit& u, uint32_t c) {
    const uint32_t dest = Dest ? Dest : fDest(c);
    uint32_t b[4];
    const uint32_t ft = fFt(c);
    if constexpr (Form == 0) std::memcpy(b, u.vf[ft].c, 16);
    else if constexpr (Form == 1) b[0] = b[1] = b[2] = b[3] = u.vf[ft].c[fBc(c)];
    else if constexpr (Form == 2) b[0] = b[1] = b[2] = b[3] = u.qRead;
    else b[0] = b[1] = b[2] = b[3] = u.vi[I];
    if constexpr (ToAcc) fmacOp(u, u.acc, dest, fFs(c), b, F{});
    else fmacOp(u, vfDst(u, fFd(c)), dest, fFs(c), b, F{});
}

// Form: 0 vector, 1 broadcast (bc field), 2 I.
template <uint32_t (*F)(uint32_t, uint32_t), int Form>
void minmaxOp(Unit& u, uint32_t c) {
    uint32_t b[4];
    const uint32_t ft = fFt(c);
    if constexpr (Form == 0) std::memcpy(b, u.vf[ft].c, 16);
    else if constexpr (Form == 1) b[0] = b[1] = b[2] = b[3] = u.vf[ft].c[fBc(c)];
    else b[0] = b[1] = b[2] = b[3] = u.vi[I];
    const VuReg a = u.vf[fFs(c)];
    VuReg* d = vfDst(u, fFd(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = F(a.c[i], b[i]);
}

template <typename F>
void unaryOp(Unit& u, uint32_t c) {   // ft = f(fs)
    const VuReg a = u.vf[fFs(c)];
    VuReg* d = vfDst(u, fFt(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = F{}(a.c[i]);
}

template <int Frac> struct Itof { uint32_t operator()(uint32_t v) const { return ops::vu::itof(v, Frac); } };
template <int Frac> struct Ftoi { uint32_t operator()(uint32_t v) const { return ops::vu::ftoi(v, Frac); } };
struct Abs { uint32_t operator()(uint32_t v) const { return v & 0x7FFFFFFFu; } };

inline void clipOp(Unit& u, uint32_t c) {
    auto w = static_cast<int32_t>(u.vf[fFt(c)].c[3]);
    w = (w & 0x7F800000) ? (w & 0x7FFFFFFF) : 0x007FFFFF;
    const VuReg a = u.vf[fFs(c)];
    uint32_t clip = u.clipNext << 6;
    for (int i = 0; i < 3; ++i) {
        if (static_cast<int32_t>(a.c[i]) > w) clip |= 1u << (2 * i);
        if (static_cast<int32_t>(a.c[i] ^ 0x80000000u) > w) clip |= 2u << (2 * i);
    }
    u.clipNext = clip & 0xFFFFFF;
}

inline void opmula(Unit& u, uint32_t c) {
    const VuReg a = u.vf[fFs(c)], b = u.vf[fFt(c)];
    uint32_t mac = 0, flags;   // w takes no part
    u.acc->c[0] = macLane(in(a.c[1]) * in(b.c[2]), flags);
    mac |= flags << 3;
    u.acc->c[1] = macLane(in(a.c[2]) * in(b.c[0]), flags);
    mac |= flags << 2;
    u.acc->c[2] = macLane(in(a.c[0]) * in(b.c[1]), flags);
    mac |= flags << 1;
    setMac(u, mac);
}

inline void opmsub(Unit& u, uint32_t c) {
    const VuReg a = u.vf[fFs(c)], b = u.vf[fFt(c)], acc = *u.acc;
    VuReg* d = vfDst(u, fFd(c));
    auto f = [](uint32_t accv, float x, float y) { const float p = x * y; return in(accv) - p; };
    uint32_t mac = 0, flags;
    d->c[0] = macLane(f(acc.c[0], in(a.c[1]), in(b.c[2])), flags);
    mac |= flags << 3;
    d->c[1] = macLane(f(acc.c[1], in(a.c[2]), in(b.c[0])), flags);
    mac |= flags << 2;
    d->c[2] = macLane(f(acc.c[2], in(a.c[0]), in(b.c[1])), flags);
    mac |= flags << 1;
    setMac(u, mac);
}

inline void unknownUpper(Unit& u, uint32_t c) { unknown(u, "upper", c); }

template <typename F, int Form, bool ToAcc>
constexpr Op fmacFn(uint32_t c) {
    switch (fDest(c)) {
    case 0xF: return fmacFamily<F, Form, ToAcc, 0xF>;
    case 0xE: return fmacFamily<F, Form, ToAcc, 0xE>;
    default: return fmacFamily<F, Form, ToAcc, 0>;
    }
}
template <int Form, bool ToAcc>
constexpr Op fmacFn(ps2n::vu::FmacFn fn, uint32_t c) {
    switch (fn) {
    case ps2n::vu::FmacFn::Add: return fmacFn<Add, Form, ToAcc>(c);
    case ps2n::vu::FmacFn::Sub: return fmacFn<Sub, Form, ToAcc>(c);
    case ps2n::vu::FmacFn::Mul: return fmacFn<Mul, Form, ToAcc>(c);
    case ps2n::vu::FmacFn::MAdd: return fmacFn<MAdd, Form, ToAcc>(c);
    default: return fmacFn<MSub, Form, ToAcc>(c);
    }
}
template <bool ToAcc>
constexpr Op fmacFn(const ps2n::vu::Upper& d, uint32_t c) {
    switch (d.form) {
    case ps2n::vu::Operand::Vector: return fmacFn<0, ToAcc>(d.fn, c);
    case ps2n::vu::Operand::Broadcast: return fmacFn<1, ToAcc>(d.fn, c);
    case ps2n::vu::Operand::Q: return fmacFn<2, ToAcc>(d.fn, c);
    default: return fmacFn<3, ToAcc>(d.fn, c);
    }
}
template <uint32_t (*F)(uint32_t, uint32_t)>
constexpr Op minmaxFn(ps2n::vu::Operand form) {
    switch (form) {
    case ps2n::vu::Operand::Vector: return minmaxOp<F, 0>;
    case ps2n::vu::Operand::Broadcast: return minmaxOp<F, 1>;
    default: return minmaxOp<F, 2>;
    }
}

// The upper operation of word `c` (none for a NOP).
constexpr Op upperFn(uint32_t c) {
    using ps2n::vu::UpperOp;
    const ps2n::vu::Upper d = ps2n::vu::decodeUpper(c);
    switch (d.op) {
    case UpperOp::Nop: return nullptr;
    case UpperOp::Fmac: return d.toAcc ? fmacFn<true>(d, c) : fmacFn<false>(d, c);
    case UpperOp::MinMax: return d.max ? minmaxFn<ops::vu::maxBits>(d.form) : minmaxFn<ops::vu::minBits>(d.form);
    case UpperOp::Itof:
        switch (d.frac) {
        case 0: return unaryOp<Itof<0>>;
        case 4: return unaryOp<Itof<4>>;
        case 12: return unaryOp<Itof<12>>;
        default: return unaryOp<Itof<15>>;
        }
    case UpperOp::Ftoi:
        switch (d.frac) {
        case 0: return unaryOp<Ftoi<0>>;
        case 4: return unaryOp<Ftoi<4>>;
        case 12: return unaryOp<Ftoi<12>>;
        default: return unaryOp<Ftoi<15>>;
        }
    case UpperOp::Abs: return unaryOp<Abs>;
    case UpperOp::Clip: return clipOp;
    case UpperOp::Opmula: return opmula;
    case UpperOp::Opmsub: return opmsub;
    default: return unknownUpper;
    }
}

// ---- lower operations ------------------------------------------------------------------------
// Their stalls and the units they start are the timing's business.
inline float comp(const Unit& u, uint32_t reg, uint32_t which) { return in(u.vf[reg].c[which]); }

inline void loadVf(Unit& u, uint32_t ft, uint32_t dest, const uint8_t* p) {   // LQ, LQI, LQD
    VuReg* d = vfDst(u, ft);
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) std::memcpy(&d->c[i], p + 4 * i, 4);
}
inline void storeVf(Unit& u, uint32_t fs, uint32_t dest, uint8_t* p) {   // SQ, SQI, SQD
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) std::memcpy(p + 4 * i, &u.vf[fs].c[i], 4);
}
inline void loadVi(Unit& u, uint32_t it, uint32_t dest, const uint8_t* p) {   // ILW, ILWR: the component chosen by dest
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) {
            uint16_t v;
            std::memcpy(&v, p + 4 * i, 2);
            viWrite(u, it, v);
            break;
        }
}
inline void storeVi(uint8_t* p, uint32_t dest, uint32_t v) {   // ISW, ISWR
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) std::memcpy(p + 4 * i, &v, 4);
}
inline void fillVf(Unit& u, uint32_t ft, uint32_t dest, uint32_t v) {   // MFIR, RNEXT, RGET, MFP
    VuReg* d = vfDst(u, ft);
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) d->c[i] = v;
}
inline void startFdiv(Unit& u, uint32_t q, uint32_t status) {
    u.fdiv.q = q;
    u.fdiv.status = status;
}
inline void startEfu(Unit& u, uint32_t c, float p) {
    if (u.index != 1) return unknown(u, "EFU (VU0 has no EFU)", c);
    u.efuP = std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(p)));
}

// Branches: where the pair after the delay slot comes from.
inline void setBranch(Unit& u, uint32_t target) {
    target &= u.microMask & ~7u;
    if (u.branch == 1) {   // a branch in a branch delay slot
        u.delayPc = target;
        u.delayBranch = true;
    } else {
        u.branch = 2;
        u.branchPc = target;
    }
}
inline uint32_t branchTarget(const Unit& u, uint32_t c) {
    return static_cast<uint32_t>(static_cast<int32_t>(u.vi[TPC]) + imm11(c) * 8);
}
inline uint32_t linkValue(const Unit& u) {
    // In a delay slot, the instruction after the first branch's target is the return point.
    return ((u.branch == 1 ? u.branchPc : u.vi[TPC]) + 8) / 8;
}
// Whether conditional branch `op` (bits 25-31) is taken.
inline bool branchTaken(const Unit& u, uint32_t op, uint32_t c) {
    const auto s = static_cast<int16_t>(viBranch(u, fFs(c))), t = static_cast<int16_t>(viBranch(u, fFt(c)));
    switch (op) {
    case 0x28: return s == t;    // IBEQ
    case 0x29: return s != t;    // IBNE
    case 0x2C: return s < 0;     // IBLTZ
    case 0x2D: return s > 0;     // IBGTZ
    case 0x2E: return s <= 0;    // IBLEZ
    default: return s >= 0;      // IBGEZ
    }
}

namespace lower {

inline void lq(Unit& u, uint32_t c) { loadVf(u, fFt(c), fDest(c), qwPtr(u, viRead(u, fFs(c)) + static_cast<uint32_t>(imm11(c)))); }   // LQ ft, imm11(is)
inline void sq(Unit& u, uint32_t c) { storeVf(u, fFs(c), fDest(c), qwPtr(u, viRead(u, fFt(c)) + static_cast<uint32_t>(imm11(c)))); }  // SQ fs, imm11(it)
inline void ilw(Unit& u, uint32_t c) { loadVi(u, fFt(c), fDest(c), qwPtr(u, viRead(u, fFs(c)) + static_cast<uint32_t>(imm11(c)))); }  // ILW it, imm11(is)
inline void isw(Unit& u, uint32_t c) { storeVi(qwPtr(u, viRead(u, fFs(c)) + static_cast<uint32_t>(imm11(c))), fDest(c), viRead(u, fFt(c))); }   // ISW it, imm11(is)
inline void iaddiu(Unit& u, uint32_t c) { viWrite(u, fFt(c), viRead(u, fFs(c)) + imm15(c)); }
inline void isubiu(Unit& u, uint32_t c) { viWrite(u, fFt(c), viRead(u, fFs(c)) - imm15(c)); }
inline void fceq(Unit& u, uint32_t c) { viWrite(u, 1, (u.vi[CLIP] & 0xFFFFFF) == (c & 0xFFFFFF) ? 1 : 0, false); }
inline void fcset(Unit& u, uint32_t c) { u.clipNext = c & 0xFFFFFF; }
inline void fcand(Unit& u, uint32_t c) { viWrite(u, 1, (u.vi[CLIP] & c & 0xFFFFFF) ? 1 : 0, false); }
inline void fcor(Unit& u, uint32_t c) { viWrite(u, 1, ((u.vi[CLIP] | c) & 0xFFFFFF) == 0xFFFFFF ? 1 : 0, false); }
inline void fseq(Unit& u, uint32_t c) { viWrite(u, fFt(c), (u.vi[STATUS] & 0xFFF) == imm12(c) ? 1 : 0, false); }
inline void fsset(Unit& u, uint32_t c) { u.statusNext = (imm12(c) & 0xFC0) | (u.statusNext & 0x3F); }
inline void fsand(Unit& u, uint32_t c) { viWrite(u, fFt(c), u.vi[STATUS] & 0xFFF & imm12(c), false); }
inline void fsor(Unit& u, uint32_t c) { viWrite(u, fFt(c), (u.vi[STATUS] & 0xFFF) | imm12(c), false); }
inline void fmeq(Unit& u, uint32_t c) { viWrite(u, fFt(c), (u.vi[MAC] & 0xFFFF) == viRead(u, fFs(c)) ? 1 : 0, false); }
inline void fmand(Unit& u, uint32_t c) { viWrite(u, fFt(c), u.vi[MAC] & 0xFFFF & viRead(u, fFs(c)), false); }
inline void fmor(Unit& u, uint32_t c) { viWrite(u, fFt(c), (u.vi[MAC] & 0xFFFF) | viRead(u, fFs(c)), false); }
inline void fcget(Unit& u, uint32_t c) { viWrite(u, fFt(c), u.vi[CLIP] & 0xFFF, false); }
inline void b(Unit& u, uint32_t c) { setBranch(u, branchTarget(u, c)); }
inline void bal(Unit& u, uint32_t c) {
    viWrite(u, fFt(c), linkValue(u), false);
    setBranch(u, branchTarget(u, c));
}
inline void jr(Unit& u, uint32_t c) { setBranch(u, viRead(u, fFs(c)) * 8u); }
inline void jalr(Unit& u, uint32_t c) {
    const uint32_t target = viRead(u, fFs(c)) * 8u;
    viWrite(u, fFt(c), linkValue(u), false);
    setBranch(u, target);
}
template <uint32_t Op>
void branchIf(Unit& u, uint32_t c) {   // conditional branches
    if (branchTaken(u, Op, c)) setBranch(u, branchTarget(u, c));
}
inline void iadd(Unit& u, uint32_t c) { viWrite(u, fFd(c), viRead(u, fFs(c)) + viRead(u, fFt(c))); }
inline void isub(Unit& u, uint32_t c) { viWrite(u, fFd(c), viRead(u, fFs(c)) - viRead(u, fFt(c))); }
inline void iaddi(Unit& u, uint32_t c) { viWrite(u, fFt(c), viRead(u, fFs(c)) + static_cast<uint32_t>(imm5(c))); }
inline void iand(Unit& u, uint32_t c) { viWrite(u, fFd(c), viRead(u, fFs(c)) & viRead(u, fFt(c))); }
inline void ior(Unit& u, uint32_t c) { viWrite(u, fFd(c), viRead(u, fFs(c)) | viRead(u, fFt(c))); }
inline void move(Unit& u, uint32_t c) {
    const VuReg a = u.vf[fFs(c)];
    VuReg* d = vfDst(u, fFt(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = a.c[i];
}
inline void mr32(Unit& u, uint32_t c) {   // rotate fs by one component (y z w x)
    const VuReg a = u.vf[fFs(c)];
    const uint32_t rot[4] = {a.c[1], a.c[2], a.c[3], a.c[0]};
    VuReg* d = vfDst(u, fFt(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = rot[i];
}
inline void lqi(Unit& u, uint32_t c) {   // LQI ft, (is++)
    const uint32_t is = fFs(c);
    loadVf(u, fFt(c), fDest(c), qwPtr(u, viRead(u, is)));
    viWrite(u, is, viRead(u, is) + 1);
}
inline void lqd(Unit& u, uint32_t c) {   // LQD ft, (--is)
    const uint32_t is = fFs(c);
    viWrite(u, is, viRead(u, is) - 1);
    loadVf(u, fFt(c), fDest(c), qwPtr(u, viRead(u, is)));
}
inline void sqi(Unit& u, uint32_t c) {   // SQI fs, (it++)
    const uint32_t it = fFt(c);
    storeVf(u, fFs(c), fDest(c), qwPtr(u, viRead(u, it)));
    viWrite(u, it, viRead(u, it) + 1);
}
inline void sqd(Unit& u, uint32_t c) {   // SQD fs, (--it)
    const uint32_t it = fFt(c);
    viWrite(u, it, viRead(u, it) - 1);
    storeVf(u, fFs(c), fDest(c), qwPtr(u, viRead(u, it)));
}
inline void div(Unit& u, uint32_t c) {   // DIV Q, fs.fsf, ft.ftf
    const uint32_t tb = u.vf[fFt(c)].c[fFtf(c)], sb = u.vf[fFs(c)].c[fFsf(c)];
    const float t = in(tb), s = in(sb);
    uint32_t status = 0, q;
    if (t == 0.0f) {
        status = s == 0.0f ? 0x10 : 0x20;
        q = ((tb ^ sb) & 0x80000000u) ? 0xFF7FFFFFu : 0x7F7FFFFFu;
    } else {
        q = std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(s / t)));
    }
    startFdiv(u, q, status);
}
inline void sqrt(Unit& u, uint32_t c) {   // SQRT Q, ft.ftf
    const float t = in(u.vf[fFt(c)].c[fFtf(c)]);
    startFdiv(u, std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(std::sqrt(std::fabs(t))))), t < 0.0f ? 0x10 : 0);
}
inline void rsqrt(Unit& u, uint32_t c) {   // RSQRT Q, fs.fsf / sqrt(ft.ftf)
    const uint32_t tb = u.vf[fFt(c)].c[fFtf(c)], sb = u.vf[fFs(c)].c[fFsf(c)];
    const float t = in(tb), s = in(sb);
    uint32_t status = 0, q;
    if (t == 0.0f) {
        status = 0x20;
        if (s != 0.0f) {
            q = ((tb ^ sb) & 0x80000000u) ? 0xFF7FFFFFu : 0x7F7FFFFFu;
        } else {
            q = ((tb ^ sb) & 0x80000000u) ? 0x80000000u : 0u;
            status |= 0x10;
        }
    } else {
        if (t < 0.0f) status = 0x10;
        q = std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(s / std::sqrt(std::fabs(t)))));
    }
    startFdiv(u, q, status);
}
inline void mtir(Unit& u, uint32_t c) { viWrite(u, fFt(c), u.vf[fFs(c)].c[fFsf(c)] & 0xFFFF, false); }   // MTIR it, fs.fsf
inline void mfir(Unit& u, uint32_t c) {   // MFIR ft, is
    fillVf(u, fFt(c), fDest(c), static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(viRead(u, fFs(c))))));
}
inline void ilwr(Unit& u, uint32_t c) { loadVi(u, fFt(c), fDest(c), qwPtr(u, viRead(u, fFs(c)))); }   // ILWR it, (is)
inline void iswr(Unit& u, uint32_t c) { storeVi(qwPtr(u, viRead(u, fFs(c))), fDest(c), viRead(u, fFt(c))); }   // ISWR it, (is)
inline void rnext(Unit& u, uint32_t c) {
    uint32_t r = u.vi[R];
    const uint32_t x = (r >> 4) & 1, y = (r >> 22) & 1;
    r = (r << 1) ^ x ^ y;
    u.vi[R] = (r & 0x7FFFFF) | 0x3F800000;
    fillVf(u, fFt(c), fDest(c), u.vi[R]);
}
inline void rget(Unit& u, uint32_t c) { fillVf(u, fFt(c), fDest(c), u.vi[R]); }
inline void rinit(Unit& u, uint32_t c) { u.vi[R] = 0x3F800000u | (u.vf[fFs(c)].c[fFsf(c)] & 0x007FFFFFu); }
inline void rxor(Unit& u, uint32_t c) { u.vi[R] = 0x3F800000u | ((u.vi[R] ^ u.vf[fFs(c)].c[fFsf(c)]) & 0x007FFFFFu); }
inline void mfp(Unit& u, uint32_t c) { fillVf(u, fFt(c), fDest(c), u.vi[P]); }   // MFP ft, P
inline void xtop(Unit& u, uint32_t c) { viWrite(u, fFt(c), vifTop(), false); }
inline void xitop(Unit& u, uint32_t c) { viWrite(u, fFt(c), vifItop(u.index), false); }
inline void esum(Unit& u, uint32_t c) {   // ESADD ERSADD ELENG ERLENG ESUM
    const uint32_t is = fFs(c);
    const float x = comp(u, is, 0), y = comp(u, is, 1), z = comp(u, is, 2), w = comp(u, is, 3);
    const float sq = x * x + y * y + z * z;
    switch (fExt(c)) {
    case 0x70: return startEfu(u, c, sq);
    case 0x71: return startEfu(u, c, sq != 0 ? 1.0f / sq : std::bit_cast<float>(0x7F7FFFFFu));
    case 0x72: return startEfu(u, c, std::sqrt(sq));
    case 0x73: return startEfu(u, c, sq != 0 ? 1.0f / std::sqrt(sq) : std::bit_cast<float>(0x7F7FFFFFu));
    default: return startEfu(u, c, x + y + z + w);
    }
}
inline void eatanxy(Unit& u, uint32_t c) {   // EATANxy, EATANxz: atan(y/x) / atan(z/x)
    const float x = comp(u, fFs(c), 0), o = comp(u, fFs(c), fExt(c) == 0x74 ? 1 : 2);
    startEfu(u, c, x != 0 ? std::atan(o / x) : 0.0f);
}
inline void escalar(Unit& u, uint32_t c) {   // ESQRT ERSQRT ERCPR ESIN EATAN EEXP
    const float s = comp(u, fFs(c), fFsf(c));
    switch (fExt(c)) {
    case 0x78: return startEfu(u, c, std::sqrt(std::fabs(s)));
    case 0x79: return startEfu(u, c, s != 0 ? 1.0f / std::sqrt(std::fabs(s)) : std::bit_cast<float>(0x7F7FFFFFu));
    case 0x7A: return startEfu(u, c, s != 0 ? 1.0f / s : std::bit_cast<float>(0x7F7FFFFFu));
    case 0x7C: return startEfu(u, c, std::sin(s));
    case 0x7D: return startEfu(u, c, std::atan(s));
    default: return startEfu(u, c, std::exp(-s));
    }
}
inline void xgkickOp(Unit& u, uint32_t c) { xgkick(u, c); }
inline void unknownLower(Unit& u, uint32_t c) { unknown(u, "lower", c); }

} // namespace lower

// The lower operation of word `c` (none for a NOP, WAITQ, WAITP).
constexpr Op lowerFn(uint32_t c, int unit) {
    using ps2n::vu::LowerOp;
    switch (ps2n::vu::decodeLower(c, unit).op) {
    case LowerOp::Nop: case LowerOp::Waitq: case LowerOp::Waitp: return nullptr;
    case LowerOp::Lq: return lower::lq;
    case LowerOp::Sq: return lower::sq;
    case LowerOp::Ilw: return lower::ilw;
    case LowerOp::Isw: return lower::isw;
    case LowerOp::Iaddiu: return lower::iaddiu;
    case LowerOp::Isubiu: return lower::isubiu;
    case LowerOp::Fceq: return lower::fceq;
    case LowerOp::Fcset: return lower::fcset;
    case LowerOp::Fcand: return lower::fcand;
    case LowerOp::Fcor: return lower::fcor;
    case LowerOp::Fseq: return lower::fseq;
    case LowerOp::Fsset: return lower::fsset;
    case LowerOp::Fsand: return lower::fsand;
    case LowerOp::Fsor: return lower::fsor;
    case LowerOp::Fmeq: return lower::fmeq;
    case LowerOp::Fmand: return lower::fmand;
    case LowerOp::Fmor: return lower::fmor;
    case LowerOp::Fcget: return lower::fcget;
    case LowerOp::B: return lower::b;
    case LowerOp::Bal: return lower::bal;
    case LowerOp::Jr: return lower::jr;
    case LowerOp::Jalr: return lower::jalr;
    case LowerOp::Ibeq: return lower::branchIf<0x28>;
    case LowerOp::Ibne: return lower::branchIf<0x29>;
    case LowerOp::Ibltz: return lower::branchIf<0x2C>;
    case LowerOp::Ibgtz: return lower::branchIf<0x2D>;
    case LowerOp::Iblez: return lower::branchIf<0x2E>;
    case LowerOp::Ibgez: return lower::branchIf<0x2F>;
    case LowerOp::Iadd: return lower::iadd;
    case LowerOp::Isub: return lower::isub;
    case LowerOp::Iaddi: return lower::iaddi;
    case LowerOp::Iand: return lower::iand;
    case LowerOp::Ior: return lower::ior;
    case LowerOp::Move: return lower::move;
    case LowerOp::Mr32: return lower::mr32;
    case LowerOp::Lqi: return lower::lqi;
    case LowerOp::Sqi: return lower::sqi;
    case LowerOp::Lqd: return lower::lqd;
    case LowerOp::Sqd: return lower::sqd;
    case LowerOp::Div: return lower::div;
    case LowerOp::Sqrt: return lower::sqrt;
    case LowerOp::Rsqrt: return lower::rsqrt;
    case LowerOp::Mtir: return lower::mtir;
    case LowerOp::Mfir: return lower::mfir;
    case LowerOp::Ilwr: return lower::ilwr;
    case LowerOp::Iswr: return lower::iswr;
    case LowerOp::Rnext: return lower::rnext;
    case LowerOp::Rget: return lower::rget;
    case LowerOp::Rinit: return lower::rinit;
    case LowerOp::Rxor: return lower::rxor;
    case LowerOp::Mfp: return lower::mfp;
    case LowerOp::Xtop: return lower::xtop;
    case LowerOp::Xitop: return lower::xitop;
    case LowerOp::Xgkick: return lower::xgkickOp;
    case LowerOp::Esum: return lower::esum;
    case LowerOp::Eatan2: return lower::eatanxy;
    case LowerOp::Escalar: return lower::escalar;
    default: return lower::unknownLower;
    }
}

// A pair whose lower operation reads a VF register its upper operation
// writes: the lower operation reads it as it was.
PS2_NOINLINE inline void runIsolated(Unit& u, uint32_t isolate, Op upper, uint32_t upperWord, Op lower, uint32_t lowerWord) {
    const VuReg before = u.vf[isolate];
    if (upper) upper(u, upperWord);
    if (lower) {
        const VuReg after = u.vf[isolate];
        u.vf[isolate] = before;
        lower(u, lowerWord);
        u.vf[isolate] = after;
    }
}

} // namespace ps2::vu
