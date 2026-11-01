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
//  * A stall holds the whole pair, whichever half caused it, so both halves
//    read what is visible once it ends: an operation on Q paired with WAITQ
//    gets the new Q.
//  * Branches have one delay slot. A branch that tests a VI register written
//    by the instruction just before it sees the register's old value.
//  * The E bit ends the program after one more instruction (its delay slot).
//
// Programs run to completion when they are started (MSCAL, VCALLMS), as if
// the VU were infinitely fast compared to the EE.
//
// A pair is decoded once: the operations its halves run, the stalls its lower
// half causes and what it sends down the FMAC pipeline, all of which its words
// fix. The decoding serves while the words stay the same: micro memory changes
// between programs (MPG, EE stores), never during one. At most four FMAC
// results are in flight (one issued a cycle, each landing 4 cycles later), so
// they wait in a ring, and a table holds the cycle at which each VF
// component's latest write lands: a stall on a register is a lookup.
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
#include <vector>

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

struct Unit;
using Op = void (*)(Unit&, uint32_t);   // one half of a pair, given its word

// What a lower operation waits for besides its VF sources.
enum Wait : uint8_t { WaitNone, WaitFdiv, WaitEfu, WaitP };

// A decoded instruction pair: the operations its halves run (none for a NOP),
// the stalls its lower half causes, which hold the whole pair (lowerStalls),
// and what it sends down the FMAC pipeline (issue).
struct Pair {
    uint64_t words = 0;      // lower | upper << 32, as decoded
    Op upper = nullptr;
    Op lower = nullptr;      // also none with the I bit, or when the upper writes the same VF register
    bool ibit = false, ebit = false;
    uint8_t isolate = 0;     // a VF register the upper writes and the lower reads
    bool stalls = false;     // the lower half can stall:
    uint8_t stallReg[2] = {0, 0}, stallMask[2] = {0, 0};   // on VF components it reads,
    Wait wait = WaitNone;    // on the FDIV or the EFU,
    uint16_t branchRegs = 0; // on an ILW loading a VI register a conditional branch tests
    bool issues = false;     // the pair sends flags down the FMAC pipeline,
    bool clip = false, fsset = false;   // including the clip flag / the sticky status bits,
    uint8_t writeReg[2] = {0, 0}, writeMask[2] = {0, 0};   // and the VF components it writes (upper, lower)
};

Pair g_code0[4096 / 8], g_code1[16384 / 8];

void decode(Pair& p, uint64_t words);

struct Unit {
    Unit(int i, VuReg* f, VuReg* a, uint32_t* v, uint8_t* d, uint32_t dm, uint8_t* m, uint32_t mm, Pair* c)
        : index(i), vf(f), acc(a), vi(v), data(d), dataMask(dm), micro(m), microMask(mm), code(c) {
        for (uint32_t pc = 0; pc < mm; pc += 8) decode(code[pc / 8], 0);   // micro memory starts cleared
    }

    int index;
    VuReg* vf;
    VuReg* acc;
    uint32_t* vi;
    uint8_t* data;
    uint32_t dataMask;    // byte mask of data memory
    uint8_t* micro;
    uint32_t microMask;
    Pair* code;           // micro memory decoded, a pair per 8 bytes

    // Pipeline state.
    uint64_t cycle = 0;
    struct Fmac {
        uint64_t done;
        bool clip, fsset;      // the pair wrote the clip flag / set the sticky status bits
        uint32_t mac, status, clipv;
    };
    Fmac fmac[8];              // in flight, oldest first from fmacHead (at most 4)
    uint32_t fmacHead = 0, fmacCount = 0;
    uint64_t vfLands[32][4] = {};   // when the latest write to each VF component lands
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
    // Branches.
    int branch = 0;
    uint32_t branchPc = 0, delayPc = 0;
    bool delayBranch = false;
    int ebit = 0;
    // A VI register written by the previous integer instruction, and its old value.
    int viBackupCycles = 0;
    uint32_t viBackupReg = 0;
    uint16_t viBackupValue = 0;
    std::vector<uint64_t> kick;   // the GIF packet of an XGKICK
};

Unit g_unit[2] = {
    Unit(0, cpu.vf, &cpu.vacc, cpu.vi, vu0Mem, 0xFFF, vu0Micro, 0xFFF, g_code0),
    Unit(1, g_vu1.vf, &g_vu1.acc, g_vu1.vi, vu1Mem, 0x3FFF, vu1Micro, 0x3FFF, g_code1),
};

// ---- instruction fields ------------------------------------------------------------
inline uint32_t fDest(uint32_t c) { return (c >> 21) & 0xF; }   // bit 3 = x
inline uint32_t fFt(uint32_t c) { return (c >> 16) & 31; }
inline uint32_t fFs(uint32_t c) { return (c >> 11) & 31; }
inline uint32_t fFd(uint32_t c) { return (c >> 6) & 31; }
inline uint32_t fBc(uint32_t c) { return c & 3; }
inline uint32_t fFsf(uint32_t c) { return (c >> 21) & 3; }
inline uint32_t fFtf(uint32_t c) { return (c >> 23) & 3; }
// The extended opcode of the upper and lower OP groups: bits 6-10, then bits 0-1.
inline uint32_t fExt(uint32_t c) { return (((c >> 6) & 31) << 2) | (c & 3); }
inline int32_t imm11(uint32_t c) { return (c & 0x400) ? static_cast<int32_t>(c | 0xFFFFF800u) : static_cast<int32_t>(c & 0x3FF); }
inline int32_t imm5(uint32_t c) {
    const uint32_t v = (c >> 6) & 31;
    return (v & 0x10) ? static_cast<int32_t>(v | 0xFFFFFFE0u) : static_cast<int32_t>(v);
}
inline uint32_t imm15(uint32_t c) { return ((c >> 10) & 0x7800) | (c & 0x7FF); }
inline uint32_t imm12(uint32_t c) { return (((c >> 21) & 1) << 11) | (c & 0x7FF); }
inline bool on(uint32_t dest, int comp) { return (dest & (8u >> comp)) != 0; }

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

void statusFromMac(Unit& u) {
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
void setMac(Unit& u, uint32_t mac) {
    u.macNext = (u.macNext & ~0xFFFFu) | mac;
    statusFromMac(u);
}

// ---- pipeline ---------------------------------------------------------------------------
void commit(Unit& u, const Unit::Fmac& e) {
    if (e.clip) u.vi[CLIP] = e.clipv;
    uint32_t& st = u.vi[STATUS];
    if (e.fsset) st = (st & 0x30) | (e.status & 0xFC0) | (e.status & 0xF);
    else st = (st & 0xFF0) | (e.status & 0xF) | ((e.status & 0xF) << 6);
    u.vi[MAC] = e.mac;
}

// Whatever has landed by now becomes visible: flags, Q, P, ILW results.
PS2_ALWAYS_INLINE void retireDue(Unit& u) {
    while (u.fmacCount && u.fmac[u.fmacHead].done <= u.cycle) {
        commit(u, u.fmac[u.fmacHead]);
        u.fmacHead = (u.fmacHead + 1) & 7;
        --u.fmacCount;
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
void retire(Unit& u) { retireDue(u); }   // out of line, after a stall

void flushAll(Unit& u) {
    for (; u.fmacCount; --u.fmacCount) {
        const Unit::Fmac& e = u.fmac[u.fmacHead];
        commit(u, e);
        u.cycle = std::max(u.cycle, e.done);
        u.fmacHead = (u.fmacHead + 1) & 7;
    }
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
    const uint64_t* lands = u.vfLands[reg];
    uint64_t until = 0;
    for (int i = 0; i < 4; ++i) until = std::max(until, lands[i] & (0 - static_cast<uint64_t>(on(mask, i))));
    advanceTo(u, until);
}

void stallFdiv(Unit& u) {
    if (u.fdiv.busy) advanceTo(u, u.fdiv.done);
}
void stallEfu(Unit& u) {
    if (u.efu.busy) advanceTo(u, u.efu.done - 1);   // released a cycle before P lands
}

// The pair's flags enter the FMAC pipeline, landing with its VF results 4 cycles later.
void issue(Unit& u, const Pair& p) {
    Unit::Fmac& e = u.fmac[(u.fmacHead + u.fmacCount++) & 7];
    e.done = u.cycle + 4;
    e.clip = p.clip;
    e.fsset = p.fsset;
    e.mac = u.macNext;
    e.status = u.statusNext;
    e.clipv = u.clipNext;
    for (int slot = 0; slot < 2; ++slot) {
        if (!p.writeReg[slot]) continue;   // vf0 and ACC hold nothing up
        uint64_t* lands = u.vfLands[p.writeReg[slot]];
        for (int i = 0; i < 4; ++i) lands[i] = on(p.writeMask[slot], i) ? e.done : lands[i];
    }
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

void unknown(Unit& u, const char* which, uint32_t c) {
    char key[64];
    std::snprintf(key, sizeof key, "VU%d %s instruction 0x%08x", u.index, which, c);
    PS2_UNIMPL("vu", key, "unknown VU%d %s instruction 0x%08x at 0x%04x", u.index, which, c, u.vi[TPC] - 8);
}

// ---- upper (FMAC) operations ----------------------------------------------------------------
// The components dest names take the clamped results; the MAC flags are those
// of the results written.
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
    else if constexpr (Form == 2) b[0] = b[1] = b[2] = b[3] = u.vi[Q];
    else b[0] = b[1] = b[2] = b[3] = u.vi[I];
    if constexpr (Form == 0) stallOn(u, ft, dest);
    else if constexpr (Form == 1) stallOn(u, ft, 8u >> fBc(c));
    stallOn(u, fFs(c), dest);
    if constexpr (ToAcc) fmacOp(u, u.acc, dest, fFs(c), b, F{});
    else fmacOp(u, vfDst(u, fFd(c)), dest, fFs(c), b, F{});
}

template <typename F, int Form, bool ToAcc>
Op fmac(uint32_t c) {
    switch (fDest(c)) {
    case 0xF: return fmacFamily<F, Form, ToAcc, 0xF>;
    case 0xE: return fmacFamily<F, Form, ToAcc, 0xE>;
    default: return fmacFamily<F, Form, ToAcc, 0>;
    }
}

// Form: 0 vector, 1 broadcast (bc field), 2 I.
template <uint32_t (*F)(uint32_t, uint32_t), int Form>
void minmaxOp(Unit& u, uint32_t c) {
    uint32_t b[4];
    const uint32_t ft = fFt(c);
    if constexpr (Form == 0) std::memcpy(b, u.vf[ft].c, 16);
    else if constexpr (Form == 1) b[0] = b[1] = b[2] = b[3] = u.vf[ft].c[fBc(c)];
    else b[0] = b[1] = b[2] = b[3] = u.vi[I];
    stallOn(u, fFs(c), fDest(c));
    if constexpr (Form == 0) stallOn(u, ft, fDest(c));
    const VuReg a = u.vf[fFs(c)];
    VuReg* d = vfDst(u, fFd(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = F(a.c[i], b[i]);
}

template <typename F>
void unaryOp(Unit& u, uint32_t c) {   // ft = f(fs)
    stallOn(u, fFs(c), fDest(c));
    const VuReg a = u.vf[fFs(c)];
    VuReg* d = vfDst(u, fFt(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = F{}(a.c[i]);
}

template <int Frac> struct Itof { uint32_t operator()(uint32_t v) const { return ops::vu::itof(v, Frac); } };
template <int Frac> struct Ftoi { uint32_t operator()(uint32_t v) const { return ops::vu::ftoi(v, Frac); } };
struct Abs { uint32_t operator()(uint32_t v) const { return v & 0x7FFFFFFFu; } };

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
}

void opmula(Unit& u, uint32_t c) {
    stallOn(u, fFs(c), 0xE);
    stallOn(u, fFt(c), 0xE);
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

void opmsub(Unit& u, uint32_t c) {
    stallOn(u, fFs(c), 0xE);
    stallOn(u, fFt(c), 0xE);
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

void unknownUpper(Unit& u, uint32_t c) { unknown(u, "upper", c); }

Op upperOp(uint32_t c) {
    const uint32_t op = c & 63;
    if (op < 0x3C) {
        const uint32_t bc = op & 3;
        switch (op >> 2) {
        case 0x0: return fmac<Add, 1, false>(c);         // ADDbc
        case 0x1: return fmac<Sub, 1, false>(c);         // SUBbc
        case 0x2: return fmac<MAdd, 1, false>(c);        // MADDbc
        case 0x3: return fmac<MSub, 1, false>(c);        // MSUBbc
        case 0x4: return minmaxOp<ops::vu::maxBits, 1>;  // MAXbc
        case 0x5: return minmaxOp<ops::vu::minBits, 1>;  // MINIbc
        case 0x6: return fmac<Mul, 1, false>(c);         // MULbc
        case 0x7:
            switch (bc) {
            case 0: return fmac<Mul, 2, false>(c);          // MULq
            case 1: return minmaxOp<ops::vu::maxBits, 2>;   // MAXi
            case 2: return fmac<Mul, 3, false>(c);          // MULi
            default: return minmaxOp<ops::vu::minBits, 2>;  // MINIi
            }
        case 0x8:
            switch (bc) {
            case 0: return fmac<Add, 2, false>(c);    // ADDq
            case 1: return fmac<MAdd, 2, false>(c);   // MADDq
            case 2: return fmac<Add, 3, false>(c);    // ADDi
            default: return fmac<MAdd, 3, false>(c);  // MADDi
            }
        case 0x9:
            switch (bc) {
            case 0: return fmac<Sub, 2, false>(c);    // SUBq
            case 1: return fmac<MSub, 2, false>(c);   // MSUBq
            case 2: return fmac<Sub, 3, false>(c);    // SUBi
            default: return fmac<MSub, 3, false>(c);  // MSUBi
            }
        case 0xA:
            switch (bc) {
            case 0: return fmac<Add, 0, false>(c);          // ADD
            case 1: return fmac<MAdd, 0, false>(c);         // MADD
            case 2: return fmac<Mul, 0, false>(c);          // MUL
            default: return minmaxOp<ops::vu::maxBits, 0>;  // MAX
            }
        case 0xB:
            switch (bc) {
            case 0: return fmac<Sub, 0, false>(c);          // SUB
            case 1: return fmac<MSub, 0, false>(c);         // MSUB
            case 2: return opmsub;                          // OPMSUB
            default: return minmaxOp<ops::vu::minBits, 0>;  // MINI
            }
        default: return unknownUpper;
        }
    }
    // Extended table: bits 6-10 select the operation, bits 0-1 the column.
    switch (fExt(c)) {
    case 0x00: case 0x01: case 0x02: case 0x03: return fmac<Add, 1, true>(c);   // ADDAbc
    case 0x04: case 0x05: case 0x06: case 0x07: return fmac<Sub, 1, true>(c);   // SUBAbc
    case 0x08: case 0x09: case 0x0A: case 0x0B: return fmac<MAdd, 1, true>(c);  // MADDAbc
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: return fmac<MSub, 1, true>(c);  // MSUBAbc
    case 0x10: return unaryOp<Itof<0>>;
    case 0x11: return unaryOp<Itof<4>>;
    case 0x12: return unaryOp<Itof<12>>;
    case 0x13: return unaryOp<Itof<15>>;
    case 0x14: return unaryOp<Ftoi<0>>;
    case 0x15: return unaryOp<Ftoi<4>>;
    case 0x16: return unaryOp<Ftoi<12>>;
    case 0x17: return unaryOp<Ftoi<15>>;
    case 0x18: case 0x19: case 0x1A: case 0x1B: return fmac<Mul, 1, true>(c);  // MULAbc
    case 0x1C: return fmac<Mul, 2, true>(c);   // MULAq
    case 0x1D: return unaryOp<Abs>;            // ABS
    case 0x1E: return fmac<Mul, 3, true>(c);   // MULAi
    case 0x1F: return clipOp;                  // CLIPw
    case 0x20: return fmac<Add, 2, true>(c);   // ADDAq
    case 0x21: return fmac<MAdd, 2, true>(c);  // MADDAq
    case 0x22: return fmac<Add, 3, true>(c);   // ADDAi
    case 0x23: return fmac<MAdd, 3, true>(c);  // MADDAi
    case 0x24: return fmac<Sub, 2, true>(c);   // SUBAq
    case 0x25: return fmac<MSub, 2, true>(c);  // MSUBAq
    case 0x26: return fmac<Sub, 3, true>(c);   // SUBAi
    case 0x27: return fmac<MSub, 3, true>(c);  // MSUBAi
    case 0x28: return fmac<Add, 0, true>(c);   // ADDA
    case 0x29: return fmac<MAdd, 0, true>(c);  // MADDA
    case 0x2A: return fmac<Mul, 0, true>(c);   // MULA
    case 0x2C: return fmac<Sub, 0, true>(c);   // SUBA
    case 0x2D: return fmac<MSub, 0, true>(c);  // MSUBA
    case 0x2E: return opmula;                  // OPMULA
    case 0x2F: return nullptr;                 // NOP
    default: return unknownUpper;
    }
}

// ---- lower operations ------------------------------------------------------------------------
// Their stalls hold the whole pair, so they are applied before it runs
// (lowerStalls), not here.
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

void startEfu(Unit& u, uint32_t c, float p, int latency) {
    if (u.index != 1) return unknown(u, "EFU (VU0 has no EFU)", c);
    u.efu.busy = true;
    u.efu.done = u.cycle + static_cast<uint64_t>(latency);
    u.efu.p = std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(p)));
}

float comp(const Unit& u, uint32_t reg, uint32_t which) { return in(u.vf[reg].c[which]); }

void loadVf(Unit& u, uint32_t ft, uint32_t dest, const uint8_t* p) {   // LQ, LQI, LQD
    VuReg* d = vfDst(u, ft);
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) std::memcpy(&d->c[i], p + 4 * i, 4);
}

void storeVf(Unit& u, uint32_t fs, uint32_t dest, uint8_t* p) {   // SQ, SQI, SQD
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) std::memcpy(p + 4 * i, &u.vf[fs].c[i], 4);
}

void loadVi(Unit& u, uint32_t it, uint32_t dest, const uint8_t* p) {   // ILW, ILWR: the component chosen by dest
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) {
            uint16_t v;
            std::memcpy(&v, p + 4 * i, 2);
            viWrite(u, it, v);
            break;
        }
    u.ialu.done = u.cycle + 4;
    u.ialu.regs |= 1u << (it & 15);
}

void storeVi(uint8_t* p, uint32_t dest, uint32_t v) {   // ISW, ISWR
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) std::memcpy(p + 4 * i, &v, 4);
}

void fillVf(Unit& u, uint32_t ft, uint32_t dest, uint32_t v) {   // MFIR, RNEXT, RGET, MFP
    VuReg* d = vfDst(u, ft);
    for (int i = 0; i < 4; ++i)
        if (on(dest, i)) d->c[i] = v;
}

namespace lower {

void lq(Unit& u, uint32_t c) { loadVf(u, fFt(c), fDest(c), qwPtr(u, viRead(u, fFs(c)) + static_cast<uint32_t>(imm11(c)))); }   // LQ ft, imm11(is)
void sq(Unit& u, uint32_t c) { storeVf(u, fFs(c), fDest(c), qwPtr(u, viRead(u, fFt(c)) + static_cast<uint32_t>(imm11(c)))); }  // SQ fs, imm11(it)
void ilw(Unit& u, uint32_t c) { loadVi(u, fFt(c), fDest(c), qwPtr(u, viRead(u, fFs(c)) + static_cast<uint32_t>(imm11(c)))); }  // ILW it, imm11(is)
void isw(Unit& u, uint32_t c) { storeVi(qwPtr(u, viRead(u, fFs(c)) + static_cast<uint32_t>(imm11(c))), fDest(c), viRead(u, fFt(c))); }   // ISW it, imm11(is)
void iaddiu(Unit& u, uint32_t c) { viWrite(u, fFt(c), viRead(u, fFs(c)) + imm15(c)); }
void isubiu(Unit& u, uint32_t c) { viWrite(u, fFt(c), viRead(u, fFs(c)) - imm15(c)); }
void fceq(Unit& u, uint32_t c) { viWrite(u, 1, (u.vi[CLIP] & 0xFFFFFF) == (c & 0xFFFFFF) ? 1 : 0, false); }
void fcset(Unit& u, uint32_t c) { u.clipNext = c & 0xFFFFFF; }
void fcand(Unit& u, uint32_t c) { viWrite(u, 1, (u.vi[CLIP] & c & 0xFFFFFF) ? 1 : 0, false); }
void fcor(Unit& u, uint32_t c) { viWrite(u, 1, ((u.vi[CLIP] | c) & 0xFFFFFF) == 0xFFFFFF ? 1 : 0, false); }
void fseq(Unit& u, uint32_t c) { viWrite(u, fFt(c), (u.vi[STATUS] & 0xFFF) == imm12(c) ? 1 : 0, false); }
void fsset(Unit& u, uint32_t c) { u.statusNext = (imm12(c) & 0xFC0) | (u.statusNext & 0x3F); }
void fsand(Unit& u, uint32_t c) { viWrite(u, fFt(c), u.vi[STATUS] & 0xFFF & imm12(c), false); }
void fsor(Unit& u, uint32_t c) { viWrite(u, fFt(c), (u.vi[STATUS] & 0xFFF) | imm12(c), false); }
void fmeq(Unit& u, uint32_t c) { viWrite(u, fFt(c), (u.vi[MAC] & 0xFFFF) == viRead(u, fFs(c)) ? 1 : 0, false); }
void fmand(Unit& u, uint32_t c) { viWrite(u, fFt(c), u.vi[MAC] & 0xFFFF & viRead(u, fFs(c)), false); }
void fmor(Unit& u, uint32_t c) { viWrite(u, fFt(c), (u.vi[MAC] & 0xFFFF) | viRead(u, fFs(c)), false); }
void fcget(Unit& u, uint32_t c) { viWrite(u, fFt(c), u.vi[CLIP] & 0xFFF, false); }
void b(Unit& u, uint32_t c) { setBranch(u, branchTarget(u, c)); }
void bal(Unit& u, uint32_t c) {
    viWrite(u, fFt(c), linkValue(u), false);
    setBranch(u, branchTarget(u, c));
}
void jr(Unit& u, uint32_t c) { setBranch(u, viRead(u, fFs(c)) * 8u); }
void jalr(Unit& u, uint32_t c) {
    const uint32_t target = viRead(u, fFs(c)) * 8u;
    viWrite(u, fFt(c), linkValue(u), false);
    setBranch(u, target);
}
template <uint32_t Op>
void branchIf(Unit& u, uint32_t c) {   // conditional branches
    const auto s = static_cast<int16_t>(viBranch(u, fFs(c))), t = static_cast<int16_t>(viBranch(u, fFt(c)));
    bool taken = false;
    if constexpr (Op == 0x28) taken = s == t;        // IBEQ
    else if constexpr (Op == 0x29) taken = s != t;   // IBNE
    else if constexpr (Op == 0x2C) taken = s < 0;    // IBLTZ
    else if constexpr (Op == 0x2D) taken = s > 0;    // IBGTZ
    else if constexpr (Op == 0x2E) taken = s <= 0;   // IBLEZ
    else taken = s >= 0;                             // IBGEZ
    if (taken) setBranch(u, branchTarget(u, c));
}
void iadd(Unit& u, uint32_t c) { viWrite(u, fFd(c), viRead(u, fFs(c)) + viRead(u, fFt(c))); }
void isub(Unit& u, uint32_t c) { viWrite(u, fFd(c), viRead(u, fFs(c)) - viRead(u, fFt(c))); }
void iaddi(Unit& u, uint32_t c) { viWrite(u, fFt(c), viRead(u, fFs(c)) + static_cast<uint32_t>(imm5(c))); }
void iand(Unit& u, uint32_t c) { viWrite(u, fFd(c), viRead(u, fFs(c)) & viRead(u, fFt(c))); }
void ior(Unit& u, uint32_t c) { viWrite(u, fFd(c), viRead(u, fFs(c)) | viRead(u, fFt(c))); }
void move(Unit& u, uint32_t c) {
    const VuReg a = u.vf[fFs(c)];
    VuReg* d = vfDst(u, fFt(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = a.c[i];
}
void mr32(Unit& u, uint32_t c) {   // rotate fs by one component (y z w x)
    const VuReg a = u.vf[fFs(c)];
    const uint32_t rot[4] = {a.c[1], a.c[2], a.c[3], a.c[0]};
    VuReg* d = vfDst(u, fFt(c));
    for (int i = 0; i < 4; ++i)
        if (on(fDest(c), i)) d->c[i] = rot[i];
}
void lqi(Unit& u, uint32_t c) {   // LQI ft, (is++)
    const uint32_t is = fFs(c);
    loadVf(u, fFt(c), fDest(c), qwPtr(u, viRead(u, is)));
    viWrite(u, is, viRead(u, is) + 1);
}
void lqd(Unit& u, uint32_t c) {   // LQD ft, (--is)
    const uint32_t is = fFs(c);
    viWrite(u, is, viRead(u, is) - 1);
    loadVf(u, fFt(c), fDest(c), qwPtr(u, viRead(u, is)));
}
void sqi(Unit& u, uint32_t c) {   // SQI fs, (it++)
    const uint32_t it = fFt(c);
    storeVf(u, fFs(c), fDest(c), qwPtr(u, viRead(u, it)));
    viWrite(u, it, viRead(u, it) + 1);
}
void sqd(Unit& u, uint32_t c) {   // SQD fs, (--it)
    const uint32_t it = fFt(c);
    viWrite(u, it, viRead(u, it) - 1);
    storeVf(u, fFs(c), fDest(c), qwPtr(u, viRead(u, it)));
}
void div(Unit& u, uint32_t c) {   // DIV Q, fs.fsf, ft.ftf
    const uint32_t tb = u.vf[fFt(c)].c[fFtf(c)], sb = u.vf[fFs(c)].c[fFsf(c)];
    const float t = in(tb), s = in(sb);
    uint32_t status = 0, q;
    if (t == 0.0f) {
        status = s == 0.0f ? 0x10 : 0x20;
        q = ((tb ^ sb) & 0x80000000u) ? 0xFF7FFFFFu : 0x7F7FFFFFu;
    } else {
        q = std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(s / t)));
    }
    startFdiv(u, q, status, 7);
}
void sqrt(Unit& u, uint32_t c) {   // SQRT Q, ft.ftf
    const float t = in(u.vf[fFt(c)].c[fFtf(c)]);
    startFdiv(u, std::bit_cast<uint32_t>(in(std::bit_cast<uint32_t>(std::sqrt(std::fabs(t))))), t < 0.0f ? 0x10 : 0, 7);
}
void rsqrt(Unit& u, uint32_t c) {   // RSQRT Q, fs.fsf / sqrt(ft.ftf)
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
    startFdiv(u, q, status, 13);
}
void mtir(Unit& u, uint32_t c) { viWrite(u, fFt(c), u.vf[fFs(c)].c[fFsf(c)] & 0xFFFF, false); }   // MTIR it, fs.fsf
void mfir(Unit& u, uint32_t c) {   // MFIR ft, is
    fillVf(u, fFt(c), fDest(c), static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(viRead(u, fFs(c))))));
}
void ilwr(Unit& u, uint32_t c) { loadVi(u, fFt(c), fDest(c), qwPtr(u, viRead(u, fFs(c)))); }   // ILWR it, (is)
void iswr(Unit& u, uint32_t c) { storeVi(qwPtr(u, viRead(u, fFs(c))), fDest(c), viRead(u, fFt(c))); }   // ISWR it, (is)
void rnext(Unit& u, uint32_t c) {
    uint32_t r = u.vi[R];
    const uint32_t x = (r >> 4) & 1, y = (r >> 22) & 1;
    r = (r << 1) ^ x ^ y;
    u.vi[R] = (r & 0x7FFFFF) | 0x3F800000;
    fillVf(u, fFt(c), fDest(c), u.vi[R]);
}
void rget(Unit& u, uint32_t c) { fillVf(u, fFt(c), fDest(c), u.vi[R]); }
void rinit(Unit& u, uint32_t c) { u.vi[R] = 0x3F800000u | (u.vf[fFs(c)].c[fFsf(c)] & 0x007FFFFFu); }
void rxor(Unit& u, uint32_t c) { u.vi[R] = 0x3F800000u | ((u.vi[R] ^ u.vf[fFs(c)].c[fFsf(c)]) & 0x007FFFFFu); }
void mfp(Unit& u, uint32_t c) { fillVf(u, fFt(c), fDest(c), u.vi[P]); }   // MFP ft, P
void xtop(Unit& u, uint32_t c) { viWrite(u, fFt(c), vif::top(1), false); }
void xitop(Unit& u, uint32_t c) { viWrite(u, fFt(c), vif::itop(u.index), false); }
void xgkick(Unit& u, uint32_t c) {
    if (u.index != 1) return unknown(u, "XGKICK on VU0", c);
    // Collect the GIF packet: tags and data until the tag with EOP.
    std::vector<uint64_t>& q = u.kick;
    q.clear();
    uint32_t a = viRead(u, fFs(c)) & 0x3FF;
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
void esum(Unit& u, uint32_t c) {   // ESADD ERSADD ELENG ERLENG ESUM
    const uint32_t is = fFs(c);
    const float x = comp(u, is, 0), y = comp(u, is, 1), z = comp(u, is, 2), w = comp(u, is, 3);
    const float sq = x * x + y * y + z * z;
    switch (fExt(c)) {
    case 0x70: return startEfu(u, c, sq, 11);
    case 0x71: return startEfu(u, c, sq != 0 ? 1.0f / sq : std::bit_cast<float>(0x7F7FFFFFu), 18);
    case 0x72: return startEfu(u, c, std::sqrt(sq), 18);
    case 0x73: return startEfu(u, c, sq != 0 ? 1.0f / std::sqrt(sq) : std::bit_cast<float>(0x7F7FFFFFu), 24);
    default: return startEfu(u, c, x + y + z + w, 12);
    }
}
void eatanxy(Unit& u, uint32_t c) {   // EATANxy, EATANxz: atan(y/x) / atan(z/x)
    const float x = comp(u, fFs(c), 0), o = comp(u, fFs(c), fExt(c) == 0x74 ? 1 : 2);
    startEfu(u, c, x != 0 ? std::atan(o / x) : 0.0f, 54);
}
void escalar(Unit& u, uint32_t c) {   // ESQRT ERSQRT ERCPR ESIN EATAN EEXP
    const float s = comp(u, fFs(c), fFsf(c));
    switch (fExt(c)) {
    case 0x78: return startEfu(u, c, std::sqrt(std::fabs(s)), 12);
    case 0x79: return startEfu(u, c, s != 0 ? 1.0f / std::sqrt(std::fabs(s)) : std::bit_cast<float>(0x7F7FFFFFu), 18);
    case 0x7A: return startEfu(u, c, s != 0 ? 1.0f / s : std::bit_cast<float>(0x7F7FFFFFu), 12);
    case 0x7C: return startEfu(u, c, std::sin(s), 29);
    case 0x7D: return startEfu(u, c, std::atan(s), 54);
    default: return startEfu(u, c, std::exp(-s), 44);
    }
}
void unknownLower(Unit& u, uint32_t c) { unknown(u, "lower", c); }

} // namespace lower

Op lowerOp(uint32_t c) {
    switch (c >> 25) {
    case 0x00: return lower::lq;
    case 0x01: return lower::sq;
    case 0x04: return lower::ilw;
    case 0x05: return lower::isw;
    case 0x08: return lower::iaddiu;
    case 0x09: return lower::isubiu;
    case 0x10: return lower::fceq;
    case 0x11: return lower::fcset;
    case 0x12: return lower::fcand;
    case 0x13: return lower::fcor;
    case 0x14: return lower::fseq;
    case 0x15: return lower::fsset;
    case 0x16: return lower::fsand;
    case 0x17: return lower::fsor;
    case 0x18: return lower::fmeq;
    case 0x1A: return lower::fmand;
    case 0x1B: return lower::fmor;
    case 0x1C: return lower::fcget;
    case 0x20: return lower::b;
    case 0x21: return lower::bal;
    case 0x24: return lower::jr;
    case 0x25: return lower::jalr;
    case 0x28: return lower::branchIf<0x28>;
    case 0x29: return lower::branchIf<0x29>;
    case 0x2C: return lower::branchIf<0x2C>;
    case 0x2D: return lower::branchIf<0x2D>;
    case 0x2E: return lower::branchIf<0x2E>;
    case 0x2F: return lower::branchIf<0x2F>;
    case 0x40: break;
    default: return lower::unknownLower;
    }
    // Lower OP group: bits 0-5, with an extended table on 0x3C-0x3F.
    switch (c & 63) {
    case 0x30: return lower::iadd;
    case 0x31: return lower::isub;
    case 0x32: return lower::iaddi;
    case 0x34: return lower::iand;
    case 0x35: return lower::ior;
    case 0x3C: case 0x3D: case 0x3E: case 0x3F: break;
    default: return lower::unknownLower;
    }
    switch (fExt(c)) {
    case 0x30: return fFt(c) && fDest(c) ? lower::move : nullptr;   // MOVE (NOP is MOVE vf00, vf00)
    case 0x31: return lower::mr32;
    case 0x34: return lower::lqi;
    case 0x35: return lower::sqi;
    case 0x36: return lower::lqd;
    case 0x37: return lower::sqd;
    case 0x38: return lower::div;
    case 0x39: return lower::sqrt;
    case 0x3A: return lower::rsqrt;
    case 0x3B: return nullptr;   // WAITQ: the pair waited
    case 0x3C: return lower::mtir;
    case 0x3D: return lower::mfir;
    case 0x3E: return lower::ilwr;
    case 0x3F: return lower::iswr;
    case 0x40: return lower::rnext;
    case 0x41: return lower::rget;
    case 0x42: return lower::rinit;
    case 0x43: return lower::rxor;
    case 0x64: return lower::mfp;
    case 0x68: return lower::xtop;
    case 0x69: return lower::xitop;
    case 0x6C: return lower::xgkick;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x76: return lower::esum;
    case 0x74: case 0x75: return lower::eatanxy;
    case 0x78: case 0x79: case 0x7A: case 0x7C: case 0x7D: case 0x7E: return lower::escalar;
    case 0x7B: return nullptr;   // WAITP: the pair waited
    default: return lower::unknownLower;
    }
}

// The VF register a lower operation reads, for the upper/lower read-before-write rule.
uint32_t lowerReadsVf(uint32_t c) {
    const uint32_t op = c >> 25;
    if (op == 0x01) return fFs(c);   // SQ
    if (op != 0x40 || (c & 0x3C) != 0x3C) return 0;
    switch (fExt(c)) {
    case 0x30: case 0x31: case 0x35: case 0x37: case 0x38: case 0x3A: case 0x3C: case 0x42: case 0x43:
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x78: case 0x79: case 0x7A:
    case 0x7C: case 0x7D: case 0x7E:
        return fFs(c);
    case 0x39: return fFt(c);   // SQRT reads ft
    default: return 0;
    }
}

// What one half of a pair sends down the FMAC pipeline: whether it does, the
// VF register it writes (0: none, vf0 or ACC) and its components, and whether
// it sets the clip flag or the sticky status bits.
struct Writes {
    bool issues = false, clip = false, fsset = false;
    uint32_t reg = 0, mask = 0;
};
Writes vfWrites(uint32_t reg, uint32_t mask) { return {true, false, false, reg, mask}; }

Writes upperWrites(uint32_t c) {
    const uint32_t op = c & 63;
    if (op < 0x3C) {
        if ((op >> 2) > 0xB) return {};                         // unknown
        return vfWrites(fFd(c), op == 0x2E ? 0xE : fDest(c));   // OPMSUB writes xyz
    }
    const uint32_t ext = fExt(c);
    if ((ext >= 0x10 && ext <= 0x17) || ext == 0x1D) return vfWrites(fFt(c), fDest(c));   // ITOF / FTOI / ABS
    if (ext == 0x1F) return {true, true, false, 0, 0};          // CLIPw
    if (ext <= 0x2E && ext != 0x2B) return vfWrites(0, 0);      // to ACC
    return {};                                                  // NOP, unknown
}
// The VF register an upper operation writes, for the read-before-write rule (0: none or ACC).
uint32_t upperWritesVf(uint32_t c) {
    const uint32_t op = c & 63;
    if (op < 0x3C) return fFd(c);
    const uint32_t ext = fExt(c);
    if ((ext >= 0x10 && ext <= 0x17) || ext == 0x1D) return fFt(c);   // ITOF / FTOI / ABS
    return 0;
}

Writes lowerWrites(uint32_t c) {
    const uint32_t op = c >> 25;
    if (op == 0x00) return vfWrites(fFt(c), fDest(c));   // LQ
    if (op == 0x01) return vfWrites(0, 0);               // SQ
    if (op == 0x11) return {true, true, false, 0, 0};    // FCSET
    if (op == 0x15) return {true, false, true, 0, 0};    // FSSET
    if (op != 0x40 || (c & 0x3C) != 0x3C) return {};
    switch (fExt(c)) {
    case 0x30: return fFt(c) ? vfWrites(fFt(c), fDest(c)) : Writes{};   // MOVE
    case 0x31: case 0x34: case 0x36: case 0x3D: case 0x40: case 0x41: case 0x64:   // MR32 LQI LQD MFIR RNEXT RGET MFP
        return vfWrites(fFt(c), fDest(c));
    case 0x35: case 0x37: return vfWrites(0, 0);         // SQI SQD
    default: return {};
    }
}
// The VF register a lower operation writes.
uint32_t lowerWritesVf(uint32_t c) {
    const uint32_t op = c >> 25;
    if (op == 0x00) return fFt(c);   // LQ
    if (op != 0x40 || (c & 0x3C) != 0x3C) return 0;
    switch (fExt(c)) {
    case 0x30: case 0x31: case 0x34: case 0x36: case 0x3D: case 0x40: case 0x41: case 0x64: return fFt(c);
    default: return 0;
    }
}

// The stalls a lower operation causes: a source register still being written,
// WAITQ / WAITP, a busy FDIV or EFU, a branch on an ILW result. A stall holds
// the whole pair, so they are applied before the upper operation reads
// anything: an operation on Q paired with WAITQ gets the new Q.
void decodeStalls(Pair& p, uint32_t c) {
    const uint32_t op = c >> 25, it = fFt(c), is = fFs(c), dest = fDest(c);
    auto vf = [&p](int n, uint32_t reg, uint32_t mask) {
        p.stallReg[n] = static_cast<uint8_t>(reg);
        p.stallMask[n] = static_cast<uint8_t>(mask);
    };
    switch (op) {
    case 0x01: return vf(0, is, dest);   // SQ
    case 0x28: case 0x29: case 0x2C: case 0x2D: case 0x2E: case 0x2F:   // conditional branches
        p.branchRegs = static_cast<uint16_t>((1u << (is & 15)) | (1u << (it & 15)));
        return;
    case 0x40: break;
    default: return;
    }
    if ((c & 0x3C) != 0x3C) return;
    switch (fExt(c)) {
    case 0x30: case 0x35: case 0x37: return vf(0, is, dest);   // MOVE SQI SQD
    case 0x31: return vf(0, is, 0xF);                          // MR32
    case 0x38: case 0x3A:                                      // DIV RSQRT
        vf(0, is, 8u >> fFsf(c));
        vf(1, it, 8u >> fFtf(c));
        p.wait = WaitFdiv;
        return;
    case 0x39:                                                 // SQRT
        vf(0, it, 8u >> fFtf(c));
        p.wait = WaitFdiv;
        return;
    case 0x3B: p.wait = WaitFdiv; return;                      // WAITQ
    case 0x3C: return vf(0, is, 8u >> fFsf(c));                // MTIR
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76:
        vf(0, is, 0xF);
        p.wait = WaitEfu;
        return;
    case 0x78: case 0x79: case 0x7A: case 0x7C: case 0x7D: case 0x7E:
        vf(0, is, 8u >> fFsf(c));
        p.wait = WaitEfu;
        return;
    case 0x7B: p.wait = WaitP; return;                         // WAITP
    default: return;
    }
}

void decode(Pair& p, uint64_t words) {
    const auto lower = static_cast<uint32_t>(words), upper = static_cast<uint32_t>(words >> 32);
    p = {};
    p.words = words;
    p.upper = upperOp(upper);
    p.ebit = (upper & 0x40000000u) != 0;
    const Writes uw = upperWrites(upper);
    Writes lw;
    if (upper & 0x80000000u) {   // I: the lower word is a float for the I register
        p.ibit = true;
    } else {
        const uint32_t ur = upperWritesVf(upper), lr = lowerReadsVf(lower);
        if (!ur || ur != lowerWritesVf(lower)) {   // else the lower operation is dropped
            p.lower = lowerOp(lower);
            lw = lowerWrites(lower);
        }
        if (ur && lr == ur) p.isolate = static_cast<uint8_t>(ur);
        decodeStalls(p, lower);
        p.stalls = p.stallMask[0] || p.stallMask[1] || p.wait != WaitNone || p.branchRegs;
    }
    p.issues = uw.issues || lw.issues;
    p.clip = uw.clip || lw.clip;
    p.fsset = lw.fsset;
    p.writeReg[0] = static_cast<uint8_t>(uw.reg);
    p.writeMask[0] = static_cast<uint8_t>(uw.mask);
    p.writeReg[1] = static_cast<uint8_t>(lw.reg);
    p.writeMask[1] = static_cast<uint8_t>(lw.mask);
}

void lowerStalls(Unit& u, const Pair& p) {
    if (p.stallMask[0]) stallOn(u, p.stallReg[0], p.stallMask[0]);
    if (p.stallMask[1]) stallOn(u, p.stallReg[1], p.stallMask[1]);
    switch (p.wait) {
    case WaitFdiv: stallFdiv(u); break;
    case WaitEfu: stallEfu(u); break;
    case WaitP:
        if (u.efu.busy) advanceTo(u, u.efu.done);
        break;
    default: break;
    }
    if (p.branchRegs && u.ialu.regs && u.cycle < u.ialu.done && (u.ialu.regs & p.branchRegs)) advanceTo(u, u.ialu.done);
}

// A pair whose lower operation reads a VF register its upper operation
// writes: the lower operation reads it as it was.
PS2_NOINLINE void runIsolated(Unit& u, const Pair& p, uint32_t upper, uint32_t lower) {
    const VuReg before = u.vf[p.isolate];
    if (p.upper) p.upper(u, upper);
    if (p.lower) {
        const VuReg after = u.vf[p.isolate];
        u.vf[p.isolate] = before;
        p.lower(u, lower);
        u.vf[p.isolate] = after;
    }
}

// Executes one instruction pair. Returns false when the program has ended.
bool step(Unit& u) {
    const uint32_t pc = u.vi[TPC] & u.microMask & ~7u;
    uint64_t words;
    std::memcpy(&words, u.micro + pc, 8);
    Pair& p = u.code[pc / 8];
    if (p.words != words) decode(p, words);
    const auto lower = static_cast<uint32_t>(words), upper = static_cast<uint32_t>(words >> 32);
    u.vi[TPC] = (pc + 8) & u.microMask;
    ++u.cycle;
    if (p.ebit) u.ebit = 2;          // E: stop after the next instruction

    retireDue(u);
    if (u.viBackupCycles > 0) --u.viBackupCycles;

    if (p.ibit) {
        if (p.upper) p.upper(u, upper);
        u.vi[I] = lower;
    } else {
        if (p.stalls) lowerStalls(u, p);
        if (p.isolate) {
            runIsolated(u, p, upper, lower);
        } else {
            if (p.upper) p.upper(u, upper);
            if (p.lower) p.lower(u, lower);
        }
    }
    if (p.issues) issue(u, p);

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
    u.fmacCount = 0;
    u.fdiv = {};
    u.efu = {};
    u.ialu = {};
    u.branch = 0;
    u.delayBranch = false;
    u.ebit = 0;
    u.viBackupCycles = 0;
    const uint32_t start = u.vi[TPC];
    uint64_t n = 0;
    bool more = step(u);
    // vf0 and vi0 are constants. Only writes from outside the program (VU0
    // stores through its window on VU1's registers) can have changed them,
    // which the first pair still sees.
    u.vf[0].c[0] = u.vf[0].c[1] = u.vf[0].c[2] = 0;
    u.vf[0].c[3] = 0x3F800000u;
    u.vi[0] = 0;
    while (more) {
        if (++n == 50000000) {
            PS2_ERROR("vu", "VU%d micro program started at 0x%04x has not ended after 50M instructions (pc 0x%04x); stopping it",
                      u.index, start, u.vi[TPC]);
            flushAll(u);
            break;
        }
        more = step(u);
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
