// Vector unit micro mode: an interpreter for VU0 and VU1 micro programs.
//
// A micro instruction is a pair: an upper (FMAC) and a lower (integer,
// load/store, divide, branch, ...) operation issued together. The model keeps
// what micro code can observe of the pipeline:
//
//  * Upper and lower read their inputs before either writes; if both write the
//    same VF register the lower operation is dropped.
//  * VF results are visible to the next instruction (the hardware stalls on a
//    read-after-write), but MAC, status and clip flags reach the flag
//    registers 4 cycles after the instruction that produced them. Stalls on
//    register hazards advance the cycle count, so flag reads see what they
//    would on hardware.
//  * Q (DIV, SQRT, RSQRT) and P (EFU, VU1 only) arrive after the unit's
//    latency; WAITQ / WAITP and a second operation on a busy unit stall.
//  * Branches have one delay slot. A branch that tests a VI register written
//    by the instruction just before it sees the register's old value.
//  * The E bit ends the program after one more instruction (its delay slot).
//
// Programs run to completion when they are started (MSCAL, VCALLMS), as if
// the VU were infinitely fast compared to the EE.
#include "vu.h"

#include "hw/gs.h"
#include "hw/vif.h"
#include "internal.h"
#include "log.h"
#include "ps2/ops_vu0.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <deque>

namespace ps2 {

namespace vu {

namespace {

// Control register indices (shared with cpu.vi for VU0).
constexpr uint32_t STATUS = VU_STATUS, MAC = VU_MAC, CLIP = VU_CLIP, R = VU_R, I = VU_I, Q = VU_Q, P = 23, TPC = VU_TPC;

struct Vu1Regs {
    VuReg vf[32];
    VuReg acc;
    uint32_t vi[32];
};
Vu1Regs g_vu1;

using ops::vu::in;

struct Unit {
    Unit(int i, VuReg* f, VuReg* a, uint32_t* v, uint8_t* d, uint32_t dm, uint8_t* m, uint32_t mm)
        : index(i), vf(f), acc(a), vi(v), data(d), dataMask(dm), micro(m), microMask(mm) {}

    int index;
    VuReg* vf;
    VuReg* acc;
    uint32_t* vi;
    uint8_t* data;
    uint32_t dataMask;    // byte mask of data memory
    uint8_t* micro;
    uint32_t microMask;

    // Pipeline state.
    uint64_t cycle = 0;
    struct Fmac {
        uint64_t done;
        uint8_t reg[2];        // VF registers written (upper, lower)
        uint8_t mask[2];       // their xyzw masks (bit 3 = x)
        bool clip, fsset;      // the pair wrote the clip flag / set the sticky status bits
        uint32_t mac, status, clipv;
    };
    std::deque<Fmac> fmac;
    struct {
        bool busy = false;
        uint64_t done = 0;
        uint32_t q = 0, status = 0;
    } fdiv;
    struct {
        bool busy = false;
        uint64_t done = 0;
        uint32_t p = 0;
    } efu;
    struct {
        uint64_t done = 0;
        uint32_t regs = 0;     // VI registers an ILW/ILWR is loading
    } ialu;
    // Flags as computed by the most recent instructions (not yet visible).
    uint32_t macNext = 0, statusNext = 0, clipNext = 0;
    uint32_t pNext = 0;
    // Branches.
    int branch = 0;
    uint32_t branchPc = 0, delayPc = 0;
    bool delayBranch = false;
    int ebit = 0;
    // A VI register written by the previous integer instruction, and its old value.
    int viBackupCycles = 0;
    uint32_t viBackupReg = 0;
    uint16_t viBackupValue = 0;
    // Per-pair bookkeeping.
    bool pairFmac = false;
    uint8_t pairReg[2] = {0, 0}, pairMask[2] = {0, 0};
    bool pairClip = false, pairFsset = false;
    uint32_t insn = 0;   // the operation being executed
};

Unit g_unit[2] = {
    Unit(0, cpu.vf, &cpu.vacc, cpu.vi, vu0Mem, 0xFFF, vu0Micro, 0xFFF),
    Unit(1, g_vu1.vf, &g_vu1.acc, g_vu1.vi, vu1Mem, 0x3FFF, vu1Micro, 0x3FFF),
};

// ---- instruction fields ------------------------------------------------------------
inline uint32_t fDest(uint32_t c) { return (c >> 21) & 0xF; }   // bit 3 = x
inline uint32_t fFt(uint32_t c) { return (c >> 16) & 31; }
inline uint32_t fFs(uint32_t c) { return (c >> 11) & 31; }
inline uint32_t fFd(uint32_t c) { return (c >> 6) & 31; }
inline uint32_t fBc(uint32_t c) { return c & 3; }
inline uint32_t fFsf(uint32_t c) { return (c >> 21) & 3; }
inline uint32_t fFtf(uint32_t c) { return (c >> 23) & 3; }
inline int32_t imm11(uint32_t c) { return (c & 0x400) ? static_cast<int32_t>(c | 0xFFFFF800u) : static_cast<int32_t>(c & 0x3FF); }
inline int32_t imm5(uint32_t c) {
    const uint32_t v = (c >> 6) & 31;
    return (v & 0x10) ? static_cast<int32_t>(v | 0xFFFFFFE0u) : static_cast<int32_t>(v);
}
inline uint32_t imm15(uint32_t c) { return ((c >> 10) & 0x7800) | (c & 0x7FF); }
inline uint32_t imm12(uint32_t c) { return (((c >> 21) & 1) << 11) | (c & 0x7FF); }
inline bool on(uint32_t dest, int comp) { return (dest & (8u >> comp)) != 0; }

// ---- flags -----------------------------------------------------------------------------
// MAC flag update for one component (shift 3 = x ... 0 = w); returns the clamped bits.
uint32_t macUpdate(Unit& u, int shift, float f) {
    const uint32_t v = std::bit_cast<uint32_t>(f);
    const uint32_t s = v & 0x80000000u;
    uint32_t& m = u.macNext;
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
        return s | 0x7F7FFFFFu;
    default:
        m &= ~(0x1101u << shift);
        return v;
    }
}

void statusFromMac(Unit& u) {
    const uint32_t m = u.macNext;
    uint32_t f = 0;
    if (m & 0x000F) f |= 1;
    if (m & 0x00F0) f |= 2;
    if (m & 0x0F00) f |= 4;
    if (m & 0xF000) f |= 8;
    u.statusNext = (u.statusNext & ~0xFu) | f;
}

// ---- pipeline ---------------------------------------------------------------------------
void commit(Unit& u, const Unit::Fmac& e) {
    if (e.clip) u.vi[CLIP] = e.clipv;
    uint32_t& st = u.vi[STATUS];
    if (e.fsset) st = (st & 0x30) | (e.status & 0xFC0) | (e.status & 0xF);
    else st = (st & 0xFF0) | (e.status & 0xF) | ((e.status & 0xF) << 6);
    u.vi[MAC] = e.mac;
}

void retire(Unit& u) {
    while (!u.fmac.empty() && u.fmac.front().done <= u.cycle) {
        commit(u, u.fmac.front());
        u.fmac.pop_front();
    }
    if (u.fdiv.busy && u.fdiv.done <= u.cycle) {
        u.fdiv.busy = false;
        u.vi[Q] = u.fdiv.q;
        u.vi[STATUS] = (u.vi[STATUS] & 0xFCF) | (u.fdiv.status & 0x30) | ((u.fdiv.status & 0x30) << 6);
    }
    if (u.efu.busy && u.efu.done <= u.cycle) {
        u.efu.busy = false;
        u.vi[P] = u.efu.p;
    }
    if (u.ialu.regs && u.ialu.done <= u.cycle) u.ialu.regs = 0;
}

void flushAll(Unit& u) {
    for (const auto& e : u.fmac) {
        commit(u, e);
        u.cycle = std::max(u.cycle, e.done);
    }
    u.fmac.clear();
    if (u.fdiv.busy) {
        u.cycle = std::max(u.cycle, u.fdiv.done);
        retire(u);
    }
    if (u.efu.busy) {
        u.cycle = std::max(u.cycle, u.efu.done);
        retire(u);
    }
}

// A stall: time moves on and whatever completes meanwhile becomes visible.
void advanceTo(Unit& u, uint64_t c) {
    if (c <= u.cycle) return;
    u.cycle = c;
    retire(u);
}

// Stall until VF register `reg` (components `mask`) has no write in flight.
void stallOn(Unit& u, uint32_t reg, uint32_t mask) {
    if (!reg || !mask) return;
    uint64_t until = 0;
    for (const auto& e : u.fmac) {
        if (e.done <= u.cycle) continue;
        if ((e.reg[0] == reg && (e.mask[0] & mask)) || (e.reg[1] == reg && (e.mask[1] & mask))) until = std::max(until, e.done);
    }
    advanceTo(u, until);
}

void stallFdiv(Unit& u) {
    if (u.fdiv.busy) advanceTo(u, u.fdiv.done);
}
void stallEfu(Unit& u) {
    if (u.efu.busy) advanceTo(u, u.efu.done - 1);   // released a cycle before P lands
}

void fmacWrites(Unit& u, int slot, uint32_t reg, uint32_t mask) {
    u.pairFmac = true;
    u.pairReg[slot] = static_cast<uint8_t>(reg);
    u.pairMask[slot] = static_cast<uint8_t>(mask);
}

// ---- VF access ----------------------------------------------------------------------------
inline VuReg* vfDst(Unit& u, uint32_t r) {
    static VuReg discard;
    return r ? &u.vf[r] : &discard;
}
inline uint16_t viRead(const Unit& u, uint32_t r) { return static_cast<uint16_t>(u.vi[r & 15]); }
// `integer`: a result of the integer unit (or an address increment), which a
// branch right after it does not see yet.
void viWrite(Unit& u, uint32_t r, uint32_t v, bool integer = true) {
    r &= 15;
    if (!r) return;
    if (!integer) {
        u.vi[r] = v & 0xFFFF;
        return;
    }
    if (u.viBackupCycles && r == u.viBackupReg) {
        u.viBackupCycles = 2;   // keep the value from before the chain of writes
    } else {
        u.viBackupCycles = 2;
        u.viBackupReg = r;
        u.viBackupValue = static_cast<uint16_t>(u.vi[r]);
    }
    u.vi[r] = v & 0xFFFF;
}
// What a branch reads: the old value if the previous instruction just wrote it.
inline uint16_t viBranch(const Unit& u, uint32_t r) {
    r &= 15;
    if (u.viBackupCycles > 0 && r == u.viBackupReg) return u.viBackupValue;
    return static_cast<uint16_t>(u.vi[r]);
}

// ---- data memory ---------------------------------------------------------------------------
// VU0 sees VU1's registers at qword 0x400 onwards (VF0-31, then VI0-15).
uint8_t* qwPtr(Unit& u, uint32_t qw) {
    if (u.index == 0 && (qw & 0x7FF) >= 0x400) {
        const uint32_t r = qw & 0x7F;
        if (r < 32) return reinterpret_cast<uint8_t*>(g_vu1.vf[r].c);
        static uint32_t viWindow[4];
        viWindow[0] = g_vu1.vi[r & 31];
        return reinterpret_cast<uint8_t*>(viWindow);
    }
    return u.data + ((qw * 16) & u.dataMask);
}

// ---- upper (FMAC) operations ----------------------------------------------------------------
template <typename F>
void fmacOp(Unit& u, VuReg* dst, uint32_t dstReg, uint32_t dest, uint32_t fs, const uint32_t (&b)[4], F f) {
    const VuReg a = u.vf[fs];
    const VuReg acc = *u.acc;
    for (int i = 0; i < 4; ++i) {
        if (on(dest, i)) dst->c[i] = macUpdate(u, 3 - i, f(acc.c[i], a.c[i], b[i]));
        else u.macNext &= ~(0x1111u << (3 - i));
    }
    statusFromMac(u);
    fmacWrites(u, 0, dstReg, dest);
}

struct Add  { float operator()(uint32_t, uint32_t a, uint32_t b) const { return in(a) + in(b); } };
struct Sub  { float operator()(uint32_t, uint32_t a, uint32_t b) const { return in(a) - in(b); } };
struct Mul  { float operator()(uint32_t, uint32_t a, uint32_t b) const { return in(a) * in(b); } };
struct MAdd { float operator()(uint32_t c, uint32_t a, uint32_t b) const { const float p = in(a) * in(b); return in(c) + p; } };
struct MSub { float operator()(uint32_t c, uint32_t a, uint32_t b) const { const float p = in(a) * in(b); return in(c) - p; } };

template <typename F>
void fmacFamily(Unit& u, uint32_t c, int form, bool toAcc, F f) {
    // form: 0 vector, 1 broadcast (bc field), 2 Q, 3 I
    uint32_t b[4];
    const uint32_t ft = fFt(c);
    switch (form) {
    case 0: std::memcpy(b, u.vf[ft].c, 16); break;
    case 1: b[0] = b[1] = b[2] = b[3] = u.vf[ft].c[fBc(c)]; break;
    case 2: b[0] = b[1] = b[2] = b[3] = u.vi[Q]; break;
    default: b[0] = b[1] = b[2] = b[3] = u.vi[I]; break;
    }
    if (form == 0) stallOn(u, ft, fDest(c));
    else if (form == 1) stallOn(u, ft, 8u >> fBc(c));
    stallOn(u, fFs(c), fDest(c));
    if (toAcc) fmacOp(u, u.acc, 0, fDest(c), fFs(c), b, f);
    else fmacOp(u, vfDst(u, fFd(c)), fFd(c), fDest(c), fFs(c), b, f);
}

template <typename F>
void minmaxOp(Unit& u, uint32_t c, int form, F f) {
    uint32_t b[4];
    const uint32_t ft = fFt(c);
    if (form == 0) std::memcpy(b, u.vf[ft].c, 16);
    else if (form == 1) b[0] = b[1] = b[2] = b[3] = u.vf[ft].c[fBc(c)];
    else b[0] = b[1] = b[2] = b[3] = u.vi[I];
    stallOn(u, fFs(c), fDest(c));
    if (form == 0) stallOn(u, ft, fDest(c));
    const VuReg a = u.vf[fFs(c)];
    VuReg* d = vfDst(u, fFd(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = f(a.c[i], b[i]);
    fmacWrites(u, 0, fFd(c), fDest(c));
}

template <typename F>
void unaryOp(Unit& u, uint32_t c, F f) {   // ft = f(fs)
    stallOn(u, fFs(c), fDest(c));
    const VuReg a = u.vf[fFs(c)];
    VuReg* d = vfDst(u, fFt(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = f(a.c[i]);
    fmacWrites(u, 0, fFt(c), fDest(c));
}

void clipOp(Unit& u, uint32_t c) {
    stallOn(u, fFs(c), 0xE);
    stallOn(u, fFt(c), 1);
    auto w = static_cast<int32_t>(u.vf[fFt(c)].c[3]);
    w = (w & 0x7F800000) ? (w & 0x7FFFFFFF) : 0x007FFFFF;
    const VuReg a = u.vf[fFs(c)];
    uint32_t clip = u.clipNext << 6;
    for (int i = 0; i < 3; ++i) {
        if (static_cast<int32_t>(a.c[i]) > w) clip |= 1u << (2 * i);
        if (static_cast<int32_t>(a.c[i] ^ 0x80000000u) > w) clip |= 2u << (2 * i);
    }
    u.clipNext = clip & 0xFFFFFF;
    u.pairClip = true;
    u.pairFmac = true;
}

void opmula(Unit& u, uint32_t c) {
    stallOn(u, fFs(c), 0xE);
    stallOn(u, fFt(c), 0xE);
    const VuReg a = u.vf[fFs(c)], b = u.vf[fFt(c)];
    u.acc->c[0] = macUpdate(u, 3, in(a.c[1]) * in(b.c[2]));
    u.acc->c[1] = macUpdate(u, 2, in(a.c[2]) * in(b.c[0]));
    u.acc->c[2] = macUpdate(u, 1, in(a.c[0]) * in(b.c[1]));
    u.macNext &= ~0x1111u;   // w takes no part
    statusFromMac(u);
    fmacWrites(u, 0, 0, 0);
}

void opmsub(Unit& u, uint32_t c) {
    stallOn(u, fFs(c), 0xE);
    stallOn(u, fFt(c), 0xE);
    const VuReg a = u.vf[fFs(c)], b = u.vf[fFt(c)], acc = *u.acc;
    VuReg* d = vfDst(u, fFd(c));
    auto f = [](uint32_t accv, float x, float y) { const float p = x * y; return in(accv) - p; };
    d->c[0] = macUpdate(u, 3, f(acc.c[0], in(a.c[1]), in(b.c[2])));
    d->c[1] = macUpdate(u, 2, f(acc.c[1], in(a.c[2]), in(b.c[0])));
    d->c[2] = macUpdate(u, 1, f(acc.c[2], in(a.c[0]), in(b.c[1])));
    u.macNext &= ~0x1111u;
    statusFromMac(u);
    fmacWrites(u, 0, fFd(c), 0xE);
}

void unknown(Unit& u, const char* which) {
    char key[64];
    std::snprintf(key, sizeof key, "VU%d %s instruction 0x%08x", u.index, which, u.insn);
    PS2_UNIMPL("vu", key, "unknown VU%d %s instruction 0x%08x at 0x%04x", u.index, which, u.insn, u.vi[TPC] - 8);
}

void execUpper(Unit& u, uint32_t c) {
    u.insn = c;
    const uint32_t op = c & 63;
    if (op < 0x3C) {
        const uint32_t bc = op & 3;
        switch (op >> 2) {
        case 0x0: fmacFamily(u, c, 1, false, Add{}); return;     // ADDbc
        case 0x1: fmacFamily(u, c, 1, false, Sub{}); return;     // SUBbc
        case 0x2: fmacFamily(u, c, 1, false, MAdd{}); return;    // MADDbc
        case 0x3: fmacFamily(u, c, 1, false, MSub{}); return;    // MSUBbc
        case 0x4: minmaxOp(u, c, 1, ops::vu::maxBits); return;   // MAXbc
        case 0x5: minmaxOp(u, c, 1, ops::vu::minBits); return;   // MINIbc
        case 0x6: fmacFamily(u, c, 1, false, Mul{}); return;     // MULbc
        case 0x7:
            switch (bc) {
            case 0: fmacFamily(u, c, 2, false, Mul{}); return;   // MULq
            case 1: minmaxOp(u, c, 2, ops::vu::maxBits); return; // MAXi
            case 2: fmacFamily(u, c, 3, false, Mul{}); return;   // MULi
            default: minmaxOp(u, c, 2, ops::vu::minBits); return; // MINIi
            }
        case 0x8:
            switch (bc) {
            case 0: fmacFamily(u, c, 2, false, Add{}); return;   // ADDq
            case 1: fmacFamily(u, c, 2, false, MAdd{}); return;  // MADDq
            case 2: fmacFamily(u, c, 3, false, Add{}); return;   // ADDi
            default: fmacFamily(u, c, 3, false, MAdd{}); return; // MADDi
            }
        case 0x9:
            switch (bc) {
            case 0: fmacFamily(u, c, 2, false, Sub{}); return;   // SUBq
            case 1: fmacFamily(u, c, 2, false, MSub{}); return;  // MSUBq
            case 2: fmacFamily(u, c, 3, false, Sub{}); return;   // SUBi
            default: fmacFamily(u, c, 3, false, MSub{}); return; // MSUBi
            }
        case 0xA:
            switch (bc) {
            case 0: fmacFamily(u, c, 0, false, Add{}); return;   // ADD
            case 1: fmacFamily(u, c, 0, false, MAdd{}); return;  // MADD
            case 2: fmacFamily(u, c, 0, false, Mul{}); return;   // MUL
            default: minmaxOp(u, c, 0, ops::vu::maxBits); return; // MAX
            }
        case 0xB:
            switch (bc) {
            case 0: fmacFamily(u, c, 0, false, Sub{}); return;   // SUB
            case 1: fmacFamily(u, c, 0, false, MSub{}); return;  // MSUB
            case 2: opmsub(u, c); return;                        // OPMSUB
            default: minmaxOp(u, c, 0, ops::vu::minBits); return; // MINI
            }
        default: return unknown(u, "upper");
        }
    }
    // Extended table: bits 6-10 select the operation, bits 0-1 the column.
    const uint32_t ext = (((c >> 6) & 31) << 2) | (c & 3);
    switch (ext) {
    case 0x00: case 0x01: case 0x02: case 0x03: fmacFamily(u, c, 1, true, Add{}); return;   // ADDAbc
    case 0x04: case 0x05: case 0x06: case 0x07: fmacFamily(u, c, 1, true, Sub{}); return;   // SUBAbc
    case 0x08: case 0x09: case 0x0A: case 0x0B: fmacFamily(u, c, 1, true, MAdd{}); return;  // MADDAbc
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: fmacFamily(u, c, 1, true, MSub{}); return;  // MSUBAbc
    case 0x10: unaryOp(u, c, [](uint32_t v) { return ops::vu::itof(v, 0); }); return;
    case 0x11: unaryOp(u, c, [](uint32_t v) { return ops::vu::itof(v, 4); }); return;
    case 0x12: unaryOp(u, c, [](uint32_t v) { return ops::vu::itof(v, 12); }); return;
    case 0x13: unaryOp(u, c, [](uint32_t v) { return ops::vu::itof(v, 15); }); return;
    case 0x14: unaryOp(u, c, [](uint32_t v) { return ops::vu::ftoi(v, 0); }); return;
    case 0x15: unaryOp(u, c, [](uint32_t v) { return ops::vu::ftoi(v, 4); }); return;
    case 0x16: unaryOp(u, c, [](uint32_t v) { return ops::vu::ftoi(v, 12); }); return;
    case 0x17: unaryOp(u, c, [](uint32_t v) { return ops::vu::ftoi(v, 15); }); return;
    case 0x18: case 0x19: case 0x1A: case 0x1B: fmacFamily(u, c, 1, true, Mul{}); return;  // MULAbc
    case 0x1C: fmacFamily(u, c, 2, true, Mul{}); return;    // MULAq
    case 0x1D: unaryOp(u, c, [](uint32_t v) { return v & 0x7FFFFFFFu; }); return;   // ABS
    case 0x1E: fmacFamily(u, c, 3, true, Mul{}); return;    // MULAi
    case 0x1F: clipOp(u, c); return;                        // CLIPw
    case 0x20: fmacFamily(u, c, 2, true, Add{}); return;    // ADDAq
    case 0x21: fmacFamily(u, c, 2, true, MAdd{}); return;   // MADDAq
    case 0x22: fmacFamily(u, c, 3, true, Add{}); return;    // ADDAi
    case 0x23: fmacFamily(u, c, 3, true, MAdd{}); return;   // MADDAi
    case 0x24: fmacFamily(u, c, 2, true, Sub{}); return;    // SUBAq
    case 0x25: fmacFamily(u, c, 2, true, MSub{}); return;   // MSUBAq
    case 0x26: fmacFamily(u, c, 3, true, Sub{}); return;    // SUBAi
    case 0x27: fmacFamily(u, c, 3, true, MSub{}); return;   // MSUBAi
    case 0x28: fmacFamily(u, c, 0, true, Add{}); return;    // ADDA
    case 0x29: fmacFamily(u, c, 0, true, MAdd{}); return;   // MADDA
    case 0x2A: fmacFamily(u, c, 0, true, Mul{}); return;    // MULA
    case 0x2C: fmacFamily(u, c, 0, true, Sub{}); return;    // SUBA
    case 0x2D: fmacFamily(u, c, 0, true, MSub{}); return;   // MSUBA
    case 0x2E: opmula(u, c); return;                        // OPMULA
    case 0x2F: return;                                      // NOP
    default: return unknown(u, "upper");
    }
}

// ---- lower operations ------------------------------------------------------------------------
void setBranch(Unit& u, uint32_t target) {
    target &= u.microMask & ~7u;
    if (u.branch == 1) {   // a branch in a branch delay slot
        u.delayPc = target;
        u.delayBranch = true;
    } else {
        u.branch = 2;
        u.branchPc = target;
    }
}
uint32_t branchTarget(const Unit& u, uint32_t c) {
    return static_cast<uint32_t>(static_cast<int32_t>(u.vi[TPC]) + imm11(c) * 8);
}
uint32_t linkValue(const Unit& u) {
    // In a delay slot, the instruction after the first branch's target is the return point.
    return ((u.branch == 1 ? u.branchPc : u.vi[TPC]) + 8) / 8;
}

void startFdiv(Unit& u, uint32_t q, uint32_t status, int latency) {
    u.fdiv.busy = true;
    u.fdiv.done = u.cycle + static_cast<uint64_t>(latency);
    u.fdiv.q = q;
    u.fdiv.status = status;
}

void startEfu(Unit& u, float p, int latency) {
    if (u.index != 1) return unknown(u, "EFU (VU0 has no EFU)");
    u.efu.busy = true;
    u.efu.done = u.cycle + static_cast<uint64_t>(latency);
    u.efu.p = std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(p)));
}

float comp(const Unit& u, uint32_t reg, uint32_t which) { return in(u.vf[reg].c[which]); }

void xgkick(Unit& u, uint32_t qwAddr) {
    if (u.index != 1) return unknown(u, "XGKICK on VU0");
    // Collect the GIF packet: tags and data until the tag with EOP.
    std::vector<uint64_t> q;
    uint32_t a = qwAddr & 0x3FF;
    for (int guard = 0; guard < 1024; ++guard) {
        uint64_t tag[2];
        std::memcpy(tag, u.data + a * 16, 16);
        q.push_back(tag[0]);
        q.push_back(tag[1]);
        a = (a + 1) & 0x3FF;
        const uint32_t nloop = static_cast<uint32_t>(tag[0] & 0x7FFF);
        const bool eop = (tag[0] >> 15) & 1;
        const uint32_t flg = static_cast<uint32_t>((tag[0] >> 58) & 3);
        uint32_t nreg = static_cast<uint32_t>((tag[0] >> 60) & 0xF);
        if (!nreg) nreg = 16;
        uint32_t data = 0;
        if (flg == 0) data = nloop * nreg;               // PACKED
        else if (flg == 1) data = (nloop * nreg + 1) / 2; // REGLIST
        else data = nloop;                               // IMAGE
        for (uint32_t i = 0; i < data; ++i) {
            uint64_t d[2];
            std::memcpy(d, u.data + a * 16, 16);
            q.push_back(d[0]);
            q.push_back(d[1]);
            a = (a + 1) & 0x3FF;
        }
        if (eop) break;
    }
    gs::gifTransfer(q.data(), q.size() / 2, 1);
}

void execLower(Unit& u, uint32_t c) {
    u.insn = c;
    const uint32_t op = c >> 25;
    const uint32_t it = fFt(c), is = fFs(c), id = fFd(c), dest = fDest(c);
    switch (op) {
    case 0x00: {   // LQ ft, imm11(is)
        VuReg* d = vfDst(u, it);
        const uint8_t* p = qwPtr(u, viRead(u, is) + static_cast<uint32_t>(imm11(c)));
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) std::memcpy(&d->c[i], p + 4 * i, 4);
        fmacWrites(u, 1, it, dest);
        return;
    }
    case 0x01: {   // SQ fs, imm11(it)
        stallOn(u, is, dest);
        uint8_t* p = qwPtr(u, viRead(u, it) + static_cast<uint32_t>(imm11(c)));
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) std::memcpy(p + 4 * i, &u.vf[is].c[i], 4);
        u.pairFmac = true;
        return;
    }
    case 0x04: {   // ILW it, imm11(is): the component chosen by dest
        const uint8_t* p = qwPtr(u, viRead(u, is) + static_cast<uint32_t>(imm11(c)));
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) {
                uint16_t v;
                std::memcpy(&v, p + 4 * i, 2);
                viWrite(u, it, v);
                break;
            }
        u.ialu.done = u.cycle + 4;
        u.ialu.regs |= 1u << (it & 15);
        return;
    }
    case 0x05: {   // ISW it, imm11(is)
        uint8_t* p = qwPtr(u, viRead(u, is) + static_cast<uint32_t>(imm11(c)));
        const uint32_t v = viRead(u, it);
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) std::memcpy(p + 4 * i, &v, 4);
        return;
    }
    case 0x08: viWrite(u, it, viRead(u, is) + imm15(c)); return;   // IADDIU
    case 0x09: viWrite(u, it, viRead(u, is) - imm15(c)); return;   // ISUBIU
    case 0x10: viWrite(u, 1, (u.vi[CLIP] & 0xFFFFFF) == (c & 0xFFFFFF) ? 1 : 0, false); return;   // FCEQ
    case 0x11: u.clipNext = c & 0xFFFFFF; u.pairClip = true; u.pairFmac = true; return;   // FCSET
    case 0x12: viWrite(u, 1, (u.vi[CLIP] & c & 0xFFFFFF) ? 1 : 0, false); return;               // FCAND
    case 0x13: viWrite(u, 1, ((u.vi[CLIP] | c) & 0xFFFFFF) == 0xFFFFFF ? 1 : 0, false); return; // FCOR
    case 0x14: viWrite(u, it, (u.vi[STATUS] & 0xFFF) == imm12(c) ? 1 : 0, false); return;       // FSEQ
    case 0x15: u.statusNext = (imm12(c) & 0xFC0) | (u.statusNext & 0x3F); u.pairFsset = true; u.pairFmac = true; return;   // FSSET
    case 0x16: viWrite(u, it, u.vi[STATUS] & 0xFFF & imm12(c), false); return;                  // FSAND
    case 0x17: viWrite(u, it, (u.vi[STATUS] & 0xFFF) | imm12(c), false); return;                // FSOR
    case 0x18: viWrite(u, it, (u.vi[MAC] & 0xFFFF) == viRead(u, is) ? 1 : 0, false); return;    // FMEQ
    case 0x1A: viWrite(u, it, u.vi[MAC] & 0xFFFF & viRead(u, is), false); return;               // FMAND
    case 0x1B: viWrite(u, it, (u.vi[MAC] & 0xFFFF) | viRead(u, is), false); return;             // FMOR
    case 0x1C: viWrite(u, it, u.vi[CLIP] & 0xFFF, false); return;                               // FCGET
    case 0x20: setBranch(u, branchTarget(u, c)); return;                                 // B
    case 0x21: viWrite(u, it, linkValue(u), false); setBranch(u, branchTarget(u, c)); return;  // BAL
    case 0x24: setBranch(u, viRead(u, is) * 8u); return;                                 // JR
    case 0x25: {                                                                         // JALR
        const uint32_t target = viRead(u, is) * 8u;
        viWrite(u, it, linkValue(u), false);
        setBranch(u, target);
        return;
    }
    case 0x28: case 0x29: case 0x2C: case 0x2D: case 0x2E: case 0x2F: {   // conditional branches
        if (u.ialu.regs && u.cycle < u.ialu.done && (u.ialu.regs & ((1u << (is & 15)) | (1u << (it & 15))))) advanceTo(u, u.ialu.done);
        const auto s = static_cast<int16_t>(viBranch(u, is)), t = static_cast<int16_t>(viBranch(u, it));
        bool taken = false;
        switch (op) {
        case 0x28: taken = s == t; break;   // IBEQ
        case 0x29: taken = s != t; break;   // IBNE
        case 0x2C: taken = s < 0; break;    // IBLTZ
        case 0x2D: taken = s > 0; break;    // IBGTZ
        case 0x2E: taken = s <= 0; break;   // IBLEZ
        default: taken = s >= 0; break;     // IBGEZ
        }
        if (taken) setBranch(u, branchTarget(u, c));
        return;
    }
    case 0x40: break;
    default: return unknown(u, "lower");
    }

    // Lower OP group: bits 0-5, with an extended table on 0x3C-0x3F.
    switch (c & 63) {
    case 0x30: viWrite(u, id, viRead(u, is) + viRead(u, it)); return;                   // IADD
    case 0x31: viWrite(u, id, viRead(u, is) - viRead(u, it)); return;                   // ISUB
    case 0x32: viWrite(u, it, viRead(u, is) + static_cast<uint32_t>(imm5(c))); return;  // IADDI
    case 0x34: viWrite(u, id, viRead(u, is) & viRead(u, it)); return;                   // IAND
    case 0x35: viWrite(u, id, viRead(u, is) | viRead(u, it)); return;                   // IOR
    case 0x3C: case 0x3D: case 0x3E: case 0x3F: break;
    default: return unknown(u, "lower");
    }
    const uint32_t ext = (((c >> 6) & 31) << 2) | (c & 3);
    switch (ext) {
    case 0x30: {   // MOVE
        stallOn(u, is, dest);
        const VuReg a = u.vf[is];
        VuReg* d = vfDst(u, it);
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) d->c[i] = a.c[i];
        if (it) fmacWrites(u, 1, it, dest);
        return;
    }
    case 0x31: {   // MR32: rotate fs by one component (y z w x)
        stallOn(u, is, 0xF);
        const VuReg a = u.vf[is];
        const uint32_t rot[4] = {a.c[1], a.c[2], a.c[3], a.c[0]};
        VuReg* d = vfDst(u, it);
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) d->c[i] = rot[i];
        fmacWrites(u, 1, it, dest);
        return;
    }
    case 0x34:     // LQI ft, (is++)
    case 0x36: {   // LQD ft, (--is)
        if (ext == 0x36) viWrite(u, is, viRead(u, is) - 1);
        const uint8_t* p = qwPtr(u, viRead(u, is));
        VuReg* d = vfDst(u, it);
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) std::memcpy(&d->c[i], p + 4 * i, 4);
        if (ext == 0x34) viWrite(u, is, viRead(u, is) + 1);
        fmacWrites(u, 1, it, dest);
        return;
    }
    case 0x35:     // SQI fs, (it++)
    case 0x37: {   // SQD fs, (--it)
        stallOn(u, is, dest);
        if (ext == 0x37) viWrite(u, it, viRead(u, it) - 1);
        uint8_t* p = qwPtr(u, viRead(u, it));
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) std::memcpy(p + 4 * i, &u.vf[is].c[i], 4);
        if (ext == 0x35) viWrite(u, it, viRead(u, it) + 1);
        u.pairFmac = true;
        return;
    }
    case 0x38: {   // DIV Q, fs.fsf, ft.ftf
        stallOn(u, is, 8u >> fFsf(c));
        stallOn(u, it, 8u >> fFtf(c));
        stallFdiv(u);
        const uint32_t tb = u.vf[it].c[fFtf(c)], sb = u.vf[is].c[fFsf(c)];
        const float t = in(tb), s = in(sb);
        uint32_t status = 0, q;
        if (t == 0.0f) {
            status = s == 0.0f ? 0x10 : 0x20;
            q = ((tb ^ sb) & 0x80000000u) ? 0xFF7FFFFFu : 0x7F7FFFFFu;
        } else {
            q = std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(s / t)));
        }
        return startFdiv(u, q, status, 7);
    }
    case 0x39: {   // SQRT Q, ft.ftf
        stallOn(u, it, 8u >> fFtf(c));
        stallFdiv(u);
        const float t = in(u.vf[it].c[fFtf(c)]);
        return startFdiv(u, std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(std::sqrt(std::fabs(t))))), t < 0.0f ? 0x10 : 0, 7);
    }
    case 0x3A: {   // RSQRT Q, fs.fsf / sqrt(ft.ftf)
        stallOn(u, is, 8u >> fFsf(c));
        stallOn(u, it, 8u >> fFtf(c));
        stallFdiv(u);
        const uint32_t tb = u.vf[it].c[fFtf(c)], sb = u.vf[is].c[fFsf(c)];
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
        return startFdiv(u, q, status, 13);
    }
    case 0x3B: stallFdiv(u); return;   // WAITQ
    case 0x3C:   // MTIR it, fs.fsf
        stallOn(u, is, 8u >> fFsf(c));
        viWrite(u, it, u.vf[is].c[fFsf(c)] & 0xFFFF, false);
        return;
    case 0x3D: {   // MFIR ft, is
        const auto v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(viRead(u, is))));
        VuReg* d = vfDst(u, it);
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) d->c[i] = v;
        fmacWrites(u, 1, it, dest);
        return;
    }
    case 0x3E: {   // ILWR it, (is): the component chosen by dest
        const uint8_t* p = qwPtr(u, viRead(u, is));
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) {
                uint16_t v;
                std::memcpy(&v, p + 4 * i, 2);
                viWrite(u, it, v);
                break;
            }
        u.ialu.done = u.cycle + 4;
        u.ialu.regs |= 1u << (it & 15);
        return;
    }
    case 0x3F: {   // ISWR it, (is)
        uint8_t* p = qwPtr(u, viRead(u, is));
        const uint32_t v = viRead(u, it);
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) std::memcpy(p + 4 * i, &v, 4);
        return;
    }
    case 0x40:   // RNEXT
    case 0x41: { // RGET
        if (ext == 0x40) {
            uint32_t r = u.vi[R];
            const uint32_t x = (r >> 4) & 1, y = (r >> 22) & 1;
            r = (r << 1) ^ x ^ y;
            u.vi[R] = (r & 0x7FFFFF) | 0x3F800000;
        }
        VuReg* d = vfDst(u, it);
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) d->c[i] = u.vi[R];
        fmacWrites(u, 1, it, dest);
        return;
    }
    case 0x42: u.vi[R] = 0x3F800000u | (u.vf[is].c[fFsf(c)] & 0x007FFFFFu); return;   // RINIT
    case 0x43: u.vi[R] = 0x3F800000u | ((u.vi[R] ^ u.vf[is].c[fFsf(c)]) & 0x007FFFFFu); return;   // RXOR
    case 0x64: {   // MFP ft, P
        VuReg* d = vfDst(u, it);
        for (int i = 0; i < 4; ++i)
            if (on(dest, i)) d->c[i] = u.vi[P];
        fmacWrites(u, 1, it, dest);
        return;
    }
    case 0x68: viWrite(u, it, vif::top(1), false); return;                // XTOP
    case 0x69: viWrite(u, it, vif::itop(u.index), false); return;         // XITOP
    case 0x6C: xgkick(u, viRead(u, is)); return;                   // XGKICK
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x76: {       // ESADD ERSADD ELENG ERLENG ESUM
        stallOn(u, is, 0xF);
        stallEfu(u);
        const float x = comp(u, is, 0), y = comp(u, is, 1), z = comp(u, is, 2), w = comp(u, is, 3);
        const float sq = x * x + y * y + z * z;
        switch (ext) {
        case 0x70: return startEfu(u, sq, 11);
        case 0x71: return startEfu(u, sq != 0 ? 1.0f / sq : std::bit_cast<float>(0x7F7FFFFFu), 18);
        case 0x72: return startEfu(u, std::sqrt(sq), 18);
        case 0x73: return startEfu(u, sq != 0 ? 1.0f / std::sqrt(sq) : std::bit_cast<float>(0x7F7FFFFFu), 24);
        default: return startEfu(u, x + y + z + w, 12);
        }
    }
    case 0x74: case 0x75: {   // EATANxy, EATANxz: atan(y/x) / atan(z/x)
        stallOn(u, is, 0xF);
        stallEfu(u);
        const float x = comp(u, is, 0), o = comp(u, is, ext == 0x74 ? 1 : 2);
        return startEfu(u, x != 0 ? std::atan(o / x) : 0.0f, 54);
    }
    case 0x78: case 0x79: case 0x7A: case 0x7C: case 0x7D: case 0x7E: {   // ESQRT ERSQRT ERCPR ESIN EATAN EEXP
        stallOn(u, is, 8u >> fFsf(c));
        stallEfu(u);
        const float s = comp(u, is, fFsf(c));
        switch (ext) {
        case 0x78: return startEfu(u, std::sqrt(std::fabs(s)), 12);
        case 0x79: return startEfu(u, s != 0 ? 1.0f / std::sqrt(std::fabs(s)) : std::bit_cast<float>(0x7F7FFFFFu), 18);
        case 0x7A: return startEfu(u, s != 0 ? 1.0f / s : std::bit_cast<float>(0x7F7FFFFFu), 12);
        case 0x7C: return startEfu(u, std::sin(s), 29);
        case 0x7D: return startEfu(u, std::atan(s), 54);
        default: return startEfu(u, std::exp(-s), 44);
        }
    }
    case 0x7B: if (u.efu.busy) advanceTo(u, u.efu.done); return;   // WAITP
    default: return unknown(u, "lower");
    }
}

// The VF register a lower operation reads, for the upper/lower read-before-write rule.
uint32_t lowerReadsVf(uint32_t c) {
    const uint32_t op = c >> 25;
    if (op == 0x01) return fFs(c);   // SQ
    if (op != 0x40 || (c & 0x3C) != 0x3C) return 0;
    switch ((((c >> 6) & 31) << 2) | (c & 3)) {
    case 0x30: case 0x31: case 0x35: case 0x37: case 0x38: case 0x3A: case 0x3C: case 0x42: case 0x43:
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x78: case 0x79: case 0x7A:
    case 0x7C: case 0x7D: case 0x7E:
        return fFs(c);
    case 0x39: return fFt(c);   // SQRT reads ft
    default: return 0;
    }
}

// The VF register an upper operation writes (0: none or ACC).
uint32_t upperWritesVf(uint32_t c) {
    const uint32_t op = c & 63;
    if (op < 0x3C) return fFd(c);
    const uint32_t ext = (((c >> 6) & 31) << 2) | (c & 3);
    if ((ext >= 0x10 && ext <= 0x17) || ext == 0x1D) return fFt(c);   // ITOF / FTOI / ABS
    return 0;
}
// The VF register a lower operation writes.
uint32_t lowerWritesVf(uint32_t c) {
    const uint32_t op = c >> 25;
    if (op == 0x00) return fFt(c);   // LQ
    if (op != 0x40 || (c & 0x3C) != 0x3C) return 0;
    switch ((((c >> 6) & 31) << 2) | (c & 3)) {
    case 0x30: case 0x31: case 0x34: case 0x36: case 0x3D: case 0x40: case 0x41: case 0x64: return fFt(c);
    default: return 0;
    }
}

// Executes one instruction pair. Returns false when the program has ended.
bool step(Unit& u) {
    const uint32_t pc = u.vi[TPC] & u.microMask & ~7u;
    uint32_t lower, upper;
    std::memcpy(&lower, u.micro + pc, 4);
    std::memcpy(&upper, u.micro + pc + 4, 4);
    u.vi[TPC] = (pc + 8) & u.microMask;
    ++u.cycle;
    if (upper & 0x40000000u) u.ebit = 2;          // E: stop after the next instruction
    u.pairFmac = false;
    u.pairReg[0] = u.pairReg[1] = 0;
    u.pairMask[0] = u.pairMask[1] = 0;
    u.pairClip = u.pairFsset = false;

    retire(u);
    if (u.viBackupCycles > 0) --u.viBackupCycles;

    if (upper & 0x80000000u) {   // I: the lower word is a float for the I register
        execUpper(u, upper);
        u.vi[I] = lower;
    } else {
        const uint32_t uw = upperWritesVf(upper), lw = lowerWritesVf(lower), lr = lowerReadsVf(lower);
        const bool dropLower = uw && uw == lw;
        VuReg before{};
        const bool isolate = uw && lr == uw;
        if (isolate) before = u.vf[uw];
        execUpper(u, upper);
        if (!dropLower) {
            if (isolate) {
                const VuReg after = u.vf[uw];
                u.vf[uw] = before;
                execLower(u, lower);
                u.vf[uw] = after;
            } else {
                execLower(u, lower);
            }
        }
    }
    if (u.pairFmac) {
        Unit::Fmac e{};
        e.done = u.cycle + 4;
        e.reg[0] = u.pairReg[0];
        e.reg[1] = u.pairReg[1];
        e.mask[0] = u.pairMask[0];
        e.mask[1] = u.pairMask[1];
        e.clip = u.pairClip;
        e.fsset = u.pairFsset;
        e.mac = u.macNext;
        e.status = u.statusNext;
        e.clipv = u.clipNext;
        u.fmac.push_back(e);
    }
    u.vf[0].c[0] = u.vf[0].c[1] = u.vf[0].c[2] = 0;
    u.vf[0].c[3] = 0x3F800000u;
    u.vi[0] = 0;

    if (u.branch > 0 && u.branch-- == 1) {
        u.vi[TPC] = u.branchPc;
        if (u.delayBranch) {
            u.branch = 1;
            u.branchPc = u.delayPc;
            u.delayBranch = false;
        }
    }
    if (u.ebit > 0 && u.ebit-- == 1) {
        flushAll(u);
        return false;
    }
    return true;
}

void run(Unit& u, uint32_t startByte) {
    if (startByte != 0xFFFFFFFFu) u.vi[TPC] = startByte & u.microMask;
    // The flag registers are what the program starts from.
    u.macNext = u.vi[MAC];
    u.statusNext = u.vi[STATUS];
    u.clipNext = u.vi[CLIP];
    u.fmac.clear();
    u.fdiv = {};
    u.efu = {};
    u.ialu = {};
    u.branch = 0;
    u.delayBranch = false;
    u.ebit = 0;
    u.viBackupCycles = 0;
    const uint32_t start = u.vi[TPC];
    uint64_t n = 0;
    while (step(u)) {
        if (++n == 50000000) {
            PS2_ERROR("vu", "VU%d micro program started at 0x%04x has not ended after 50M instructions (pc 0x%04x); stopping it",
                      u.index, start, u.vi[TPC]);
            flushAll(u);
            break;
        }
    }
    PS2_TRACE("vu", "VU%d program at 0x%04x ran %llu instructions", u.index, start, static_cast<unsigned long long>(n + 1));
}

} // namespace

uint32_t vu1Reg(uint32_t index) { return g_vu1.vi[index & 31]; }

void runVu1(uint32_t startByte) { run(g_unit[1], startByte); }

} // namespace vu

// ---- hooks used by recompiled code and the VIF ---------------------------------------------------
void vu0CallMicro(uint32_t addr) { vu::run(vu::g_unit[0], addr); }

void vu1Start(uint32_t addr) { vu::run(vu::g_unit[1], addr); }

void vu0Reset() {
    for (uint32_t i = 1; i < 32; ++i) std::memset(cpu.vf[i].c, 0, 16);
    for (uint32_t i = 0; i < 16; ++i) cpu.vi[i] = 0;
    std::memset(cpu.vacc.c, 0, 16);
    cpu.vi[VU_STATUS] = cpu.vi[VU_MAC] = cpu.vi[VU_CLIP] = 0;
}

void vu1Reset() {
    vu::g_vu1 = {};
    vu::g_vu1.vf[0].c[3] = 0x3F800000u;
}

} // namespace ps2
