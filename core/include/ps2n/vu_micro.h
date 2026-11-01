// VU micro instruction pairs as the pipeline sees them, shared by the
// runtime's interpreter (runtime/src/vu.cpp) and the recompiler's VU code
// generator (recompiler/src/vu_codegen.cpp).
//
// A micro instruction is a pair: an upper (FMAC) and a lower (integer,
// load/store, divide, branch, ...) operation issued together. decodePair()
// says everything its words fix: which operations its halves run, which
// registers each waits for, what it sends down the FMAC pipeline, which
// units it starts and how it branches. The Timing below keeps the
// pipeline's clock for both users through hooks: the interpreter computes
// values in its hooks, while the code generator records at generation time
// where compiled code commits flags, Q and P. Both therefore keep time the
// same way, and tests compare them on fuzzed programs as well.
//
// The pipeline as micro code sees it:
//  * Upper and lower read their inputs before either writes; if both write the
//    same VF register the lower operation is dropped (its stalls still hold).
//  * VF results are visible to the next instruction (the hardware stalls on a
//    read-after-write), but MAC, status and clip flags reach the flag
//    registers 4 cycles after the instruction that produced them.
//  * Q (DIV, SQRT, RSQRT) and P (EFU, VU1 only) arrive after the unit's
//    latency; WAITQ / WAITP and a second operation on a busy unit stall.
//  * The lower half's stalls hold the whole pair; the upper half then reads Q
//    and waits for its own VF sources.
//  * Branches have one delay slot. A branch that tests a VI register written
//    by the integer instruction just before it sees the register's old value.
//  * The E bit ends the program after one more instruction (its delay slot).
#pragma once

#include <algorithm>
#include <cstdint>

// The timing runs on every pair the interpreter steps through: its hot paths are inlined.
#if defined(_MSC_VER) && !defined(__clang__)
#define PS2N_VU_INLINE __forceinline
#define PS2N_VU_COLD __declspec(noinline)
#else
#define PS2N_VU_INLINE inline __attribute__((always_inline))
#define PS2N_VU_COLD __attribute__((noinline))
#endif

namespace ps2n::vu {

// ---- instruction fields ---------------------------------------------------------------
constexpr uint32_t fDest(uint32_t c) { return (c >> 21) & 0xF; }   // bit 3 = x
constexpr uint32_t fFt(uint32_t c) { return (c >> 16) & 31; }
constexpr uint32_t fFs(uint32_t c) { return (c >> 11) & 31; }
constexpr uint32_t fFd(uint32_t c) { return (c >> 6) & 31; }
constexpr uint32_t fBc(uint32_t c) { return c & 3; }
constexpr uint32_t fFsf(uint32_t c) { return (c >> 21) & 3; }
constexpr uint32_t fFtf(uint32_t c) { return (c >> 23) & 3; }
// The extended opcode of the upper and lower OP groups: bits 6-10, then bits 0-1.
constexpr uint32_t fExt(uint32_t c) { return (((c >> 6) & 31) << 2) | (c & 3); }
constexpr int32_t imm11(uint32_t c) { return (c & 0x400) ? static_cast<int32_t>(c | 0xFFFFF800u) : static_cast<int32_t>(c & 0x3FF); }
constexpr int32_t imm5(uint32_t c) {
    const uint32_t v = (c >> 6) & 31;
    return (v & 0x10) ? static_cast<int32_t>(v | 0xFFFFFFE0u) : static_cast<int32_t>(v);
}
constexpr uint32_t imm15(uint32_t c) { return ((c >> 10) & 0x7800) | (c & 0x7FF); }
constexpr uint32_t imm12(uint32_t c) { return (((c >> 21) & 1) << 11) | (c & 0x7FF); }
constexpr bool on(uint32_t dest, int comp) { return (dest & (8u >> comp)) != 0; }

// Control register indices (VI 16-31, the CFC2/CTC2 numbering).
constexpr uint32_t kStatus = 16, kMac = 17, kClip = 18, kR = 20, kI = 21, kQ = 22, kP = 23, kTpc = 26;

// ---- upper operations -----------------------------------------------------------------
enum class UpperOp : uint8_t { Nop, Fmac, MinMax, Itof, Ftoi, Abs, Clip, Opmula, Opmsub, Unknown };
enum class FmacFn : uint8_t { Add, Sub, Mul, MAdd, MSub };
enum class Operand : uint8_t { Vector, Broadcast, Q, I };   // the second source

struct Upper {
    UpperOp op = UpperOp::Unknown;
    FmacFn fn = FmacFn::Add;              // Fmac
    Operand form = Operand::Vector;       // Fmac, MinMax
    bool toAcc = false;                   // Fmac: the result goes to ACC
    bool max = false;                     // MinMax: MAX, else MINI
    uint8_t frac = 0;                     // Itof, Ftoi: fraction bits
    // The VF sources it waits for, in the order it checks them.
    uint8_t stallReg[2] = {0, 0}, stallMask[2] = {0, 0};
    // What it sends down the FMAC pipeline: whether it does, the clip flag,
    // and the VF components it writes (0: none, vf0 or ACC).
    bool issues = false, clip = false;
    uint8_t writeReg = 0, writeMask = 0;
    uint8_t writesVf = 0;                 // the VF register it writes, for the read-before-write rule
};

constexpr Upper decodeUpper(uint32_t c) {
    Upper u;
    auto stall = [&u](int n, uint32_t reg, uint32_t mask) {
        u.stallReg[n] = static_cast<uint8_t>(reg);
        u.stallMask[n] = static_cast<uint8_t>(mask);
    };
    auto fmac = [&](FmacFn fn, Operand form, bool toAcc) {
        u.op = UpperOp::Fmac;
        u.fn = fn;
        u.form = form;
        u.toAcc = toAcc;
        const uint32_t dest = fDest(c);
        if (form == Operand::Vector) {
            stall(0, fFt(c), dest);
            stall(1, fFs(c), dest);
        } else if (form == Operand::Broadcast) {
            stall(0, fFt(c), 8u >> fBc(c));
            stall(1, fFs(c), dest);
        } else {
            stall(0, fFs(c), dest);
        }
    };
    auto minmax = [&](bool max, Operand form) {
        u.op = UpperOp::MinMax;
        u.max = max;
        u.form = form;
        stall(0, fFs(c), fDest(c));
        if (form == Operand::Vector) stall(1, fFt(c), fDest(c));
    };
    const uint32_t op = c & 63;
    if (op < 0x3C) {
        const uint32_t bc = op & 3;
        switch (op >> 2) {
        case 0x0: fmac(FmacFn::Add, Operand::Broadcast, false); break;    // ADDbc
        case 0x1: fmac(FmacFn::Sub, Operand::Broadcast, false); break;    // SUBbc
        case 0x2: fmac(FmacFn::MAdd, Operand::Broadcast, false); break;   // MADDbc
        case 0x3: fmac(FmacFn::MSub, Operand::Broadcast, false); break;   // MSUBbc
        case 0x4: minmax(true, Operand::Broadcast); break;                // MAXbc
        case 0x5: minmax(false, Operand::Broadcast); break;               // MINIbc
        case 0x6: fmac(FmacFn::Mul, Operand::Broadcast, false); break;    // MULbc
        case 0x7:
            if (bc == 0) fmac(FmacFn::Mul, Operand::Q, false);            // MULq
            else if (bc == 1) minmax(true, Operand::I);                   // MAXi
            else if (bc == 2) fmac(FmacFn::Mul, Operand::I, false);       // MULi
            else minmax(false, Operand::I);                               // MINIi
            break;
        case 0x8:
            if (bc == 0) fmac(FmacFn::Add, Operand::Q, false);            // ADDq
            else if (bc == 1) fmac(FmacFn::MAdd, Operand::Q, false);      // MADDq
            else if (bc == 2) fmac(FmacFn::Add, Operand::I, false);       // ADDi
            else fmac(FmacFn::MAdd, Operand::I, false);                   // MADDi
            break;
        case 0x9:
            if (bc == 0) fmac(FmacFn::Sub, Operand::Q, false);            // SUBq
            else if (bc == 1) fmac(FmacFn::MSub, Operand::Q, false);      // MSUBq
            else if (bc == 2) fmac(FmacFn::Sub, Operand::I, false);       // SUBi
            else fmac(FmacFn::MSub, Operand::I, false);                   // MSUBi
            break;
        case 0xA:
            if (bc == 0) fmac(FmacFn::Add, Operand::Vector, false);       // ADD
            else if (bc == 1) fmac(FmacFn::MAdd, Operand::Vector, false); // MADD
            else if (bc == 2) fmac(FmacFn::Mul, Operand::Vector, false);  // MUL
            else minmax(true, Operand::Vector);                           // MAX
            break;
        case 0xB:
            if (bc == 0) fmac(FmacFn::Sub, Operand::Vector, false);       // SUB
            else if (bc == 1) fmac(FmacFn::MSub, Operand::Vector, false); // MSUB
            else if (bc == 2) {                                           // OPMSUB
                u.op = UpperOp::Opmsub;
                stall(0, fFs(c), 0xE);
                stall(1, fFt(c), 0xE);
            } else {
                minmax(false, Operand::Vector);                           // MINI
            }
            break;
        default: u.op = UpperOp::Unknown; break;
        }
        // Everything in this range takes part in the pipeline, writing fd
        // (OPMSUB its xyz); the unknown rows do not.
        if ((op >> 2) <= 0xB) {
            u.issues = true;
            u.writeReg = static_cast<uint8_t>(fFd(c));
            u.writeMask = static_cast<uint8_t>(op == 0x2E ? 0xE : fDest(c));
        }
        u.writesVf = static_cast<uint8_t>(fFd(c));
        return u;
    }
    // Extended table: bits 6-10 select the operation, bits 0-1 the column.
    const uint32_t ext = fExt(c);
    switch (ext) {
    case 0x00: case 0x01: case 0x02: case 0x03: fmac(FmacFn::Add, Operand::Broadcast, true); break;    // ADDAbc
    case 0x04: case 0x05: case 0x06: case 0x07: fmac(FmacFn::Sub, Operand::Broadcast, true); break;    // SUBAbc
    case 0x08: case 0x09: case 0x0A: case 0x0B: fmac(FmacFn::MAdd, Operand::Broadcast, true); break;   // MADDAbc
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: fmac(FmacFn::MSub, Operand::Broadcast, true); break;   // MSUBAbc
    case 0x10: case 0x11: case 0x12: case 0x13:                                                        // ITOF0/4/12/15
    case 0x14: case 0x15: case 0x16: case 0x17: {                                                      // FTOI0/4/12/15
        constexpr uint8_t kFrac[4] = {0, 4, 12, 15};
        u.op = ext < 0x14 ? UpperOp::Itof : UpperOp::Ftoi;
        u.frac = kFrac[ext & 3];
        stall(0, fFs(c), fDest(c));
        break;
    }
    case 0x18: case 0x19: case 0x1A: case 0x1B: fmac(FmacFn::Mul, Operand::Broadcast, true); break;    // MULAbc
    case 0x1C: fmac(FmacFn::Mul, Operand::Q, true); break;     // MULAq
    case 0x1D:                                                 // ABS
        u.op = UpperOp::Abs;
        stall(0, fFs(c), fDest(c));
        break;
    case 0x1E: fmac(FmacFn::Mul, Operand::I, true); break;     // MULAi
    case 0x1F:                                                 // CLIPw
        u.op = UpperOp::Clip;
        stall(0, fFs(c), 0xE);
        stall(1, fFt(c), 1);
        break;
    case 0x20: fmac(FmacFn::Add, Operand::Q, true); break;     // ADDAq
    case 0x21: fmac(FmacFn::MAdd, Operand::Q, true); break;    // MADDAq
    case 0x22: fmac(FmacFn::Add, Operand::I, true); break;     // ADDAi
    case 0x23: fmac(FmacFn::MAdd, Operand::I, true); break;    // MADDAi
    case 0x24: fmac(FmacFn::Sub, Operand::Q, true); break;     // SUBAq
    case 0x25: fmac(FmacFn::MSub, Operand::Q, true); break;    // MSUBAq
    case 0x26: fmac(FmacFn::Sub, Operand::I, true); break;     // SUBAi
    case 0x27: fmac(FmacFn::MSub, Operand::I, true); break;    // MSUBAi
    case 0x28: fmac(FmacFn::Add, Operand::Vector, true); break;    // ADDA
    case 0x29: fmac(FmacFn::MAdd, Operand::Vector, true); break;   // MADDA
    case 0x2A: fmac(FmacFn::Mul, Operand::Vector, true); break;    // MULA
    case 0x2C: fmac(FmacFn::Sub, Operand::Vector, true); break;    // SUBA
    case 0x2D: fmac(FmacFn::MSub, Operand::Vector, true); break;   // MSUBA
    case 0x2E:                                                     // OPMULA
        u.op = UpperOp::Opmula;
        stall(0, fFs(c), 0xE);
        stall(1, fFt(c), 0xE);
        break;
    case 0x2F: u.op = UpperOp::Nop; break;
    default: u.op = UpperOp::Unknown; break;
    }
    if ((ext >= 0x10 && ext <= 0x17) || ext == 0x1D) {   // ITOF / FTOI / ABS write ft
        u.issues = true;
        u.writeReg = static_cast<uint8_t>(fFt(c));
        u.writeMask = static_cast<uint8_t>(fDest(c));
        u.writesVf = static_cast<uint8_t>(fFt(c));
    } else if (ext == 0x1F) {
        u.issues = true;
        u.clip = true;
    } else if (ext <= 0x2E && ext != 0x2B) {             // to ACC
        u.issues = true;
    }
    return u;
}

// ---- lower operations -----------------------------------------------------------------
enum class LowerOp : uint8_t {
    Nop, Lq, Sq, Ilw, Isw, Iaddiu, Isubiu, Fceq, Fcset, Fcand, Fcor, Fseq, Fsset, Fsand, Fsor, Fmeq, Fmand, Fmor, Fcget,
    B, Bal, Jr, Jalr, Ibeq, Ibne, Ibltz, Ibgtz, Iblez, Ibgez,
    Iadd, Isub, Iaddi, Iand, Ior, Move, Mr32, Lqi, Sqi, Lqd, Sqd, Div, Sqrt, Rsqrt, Waitq, Mtir, Mfir, Ilwr, Iswr,
    Rnext, Rget, Rinit, Rxor, Mfp, Xtop, Xitop, Xgkick, Esum, Eatan2, Escalar, Waitp, Unknown
};
// What a lower operation waits for besides its VF sources.
enum Wait : uint8_t { WaitNone, WaitFdiv, WaitEfu, WaitP };

struct Lower {
    LowerOp op = LowerOp::Unknown;
    // Stalls (they hold the whole pair): VF components it reads, the FDIV or
    // EFU, or an ILW loading a VI register a conditional branch tests.
    uint8_t stallReg[2] = {0, 0}, stallMask[2] = {0, 0};
    Wait wait = WaitNone;
    uint16_t branchRegs = 0;
    // What it sends down the FMAC pipeline.
    bool issues = false, clip = false, fsset = false;
    uint8_t writeReg = 0, writeMask = 0;
    uint8_t readsVf = 0, writesVf = 0;    // for the read-before-write rule
    uint8_t fdiv = 0;                     // the FDIV latency it starts (0: none)
    uint8_t efu = 0;                      // the EFU latency it starts (0: none)
    bool loadsVi = false;                 // ILW / ILWR: the integer unit is busy for 4 cycles,
    uint8_t ialuReg = 0;                  // loading this VI register (masked to 0-15)
    // The VI register an integer result goes to (VI 1-15; 0: none), which a
    // branch right after it does not see yet, and whether it is written
    // before the operation's memory access (LQD, SQD).
    uint8_t intWrite = 0;
    bool branch = false;                  // B BAL JR JALR and the conditional branches
};

// `unit`: 0 or 1. VU0 has no EFU: its operations start nothing there.
constexpr Lower decodeLower(uint32_t c, int unit) {
    Lower l;
    auto stall = [&l](int n, uint32_t reg, uint32_t mask) {
        l.stallReg[n] = static_cast<uint8_t>(reg);
        l.stallMask[n] = static_cast<uint8_t>(mask);
    };
    auto vfWrite = [&l](uint32_t reg, uint32_t mask) {
        l.issues = true;
        l.writeReg = static_cast<uint8_t>(reg);
        l.writeMask = static_cast<uint8_t>(mask);
    };
    auto intWrite = [&l](uint32_t reg) { l.intWrite = static_cast<uint8_t>(reg & 15); };
    const uint32_t op = c >> 25, it = fFt(c), is = fFs(c), dest = fDest(c);
    switch (op) {
    case 0x00:   // LQ ft, imm11(is)
        l.op = LowerOp::Lq;
        vfWrite(it, dest);
        l.writesVf = static_cast<uint8_t>(it);
        return l;
    case 0x01:   // SQ fs, imm11(it)
        l.op = LowerOp::Sq;
        stall(0, is, dest);
        vfWrite(0, 0);
        l.readsVf = static_cast<uint8_t>(is);
        return l;
    case 0x04:   // ILW it, imm11(is)
        l.op = LowerOp::Ilw;
        l.loadsVi = true;
        l.ialuReg = static_cast<uint8_t>(it & 15);
        if (dest) intWrite(it);
        return l;
    case 0x05: l.op = LowerOp::Isw; return l;
    case 0x08: l.op = LowerOp::Iaddiu; intWrite(it); return l;
    case 0x09: l.op = LowerOp::Isubiu; intWrite(it); return l;
    case 0x10: l.op = LowerOp::Fceq; return l;
    case 0x11: l.op = LowerOp::Fcset; l.issues = true; l.clip = true; return l;
    case 0x12: l.op = LowerOp::Fcand; return l;
    case 0x13: l.op = LowerOp::Fcor; return l;
    case 0x14: l.op = LowerOp::Fseq; return l;
    case 0x15: l.op = LowerOp::Fsset; l.issues = true; l.fsset = true; return l;
    case 0x16: l.op = LowerOp::Fsand; return l;
    case 0x17: l.op = LowerOp::Fsor; return l;
    case 0x18: l.op = LowerOp::Fmeq; return l;
    case 0x1A: l.op = LowerOp::Fmand; return l;
    case 0x1B: l.op = LowerOp::Fmor; return l;
    case 0x1C: l.op = LowerOp::Fcget; return l;
    case 0x20: l.op = LowerOp::B; l.branch = true; return l;
    case 0x21: l.op = LowerOp::Bal; l.branch = true; return l;
    case 0x24: l.op = LowerOp::Jr; l.branch = true; return l;
    case 0x25: l.op = LowerOp::Jalr; l.branch = true; return l;
    case 0x28: case 0x29: case 0x2C: case 0x2D: case 0x2E: case 0x2F: {
        constexpr LowerOp kBranch[8] = {LowerOp::Ibeq, LowerOp::Ibne, LowerOp::Unknown, LowerOp::Unknown,
                                        LowerOp::Ibltz, LowerOp::Ibgtz, LowerOp::Iblez, LowerOp::Ibgez};
        l.op = kBranch[op - 0x28];
        l.branch = true;
        l.branchRegs = static_cast<uint16_t>((1u << (is & 15)) | (1u << (it & 15)));
        return l;
    }
    case 0x40: break;
    default: l.op = LowerOp::Unknown; return l;
    }
    // Lower OP group: bits 0-5, with an extended table on 0x3C-0x3F.
    switch (c & 63) {
    case 0x30: l.op = LowerOp::Iadd; intWrite(fFd(c)); return l;
    case 0x31: l.op = LowerOp::Isub; intWrite(fFd(c)); return l;
    case 0x32: l.op = LowerOp::Iaddi; intWrite(it); return l;
    case 0x34: l.op = LowerOp::Iand; intWrite(fFd(c)); return l;
    case 0x35: l.op = LowerOp::Ior; intWrite(fFd(c)); return l;
    case 0x3C: case 0x3D: case 0x3E: case 0x3F: break;
    default: l.op = LowerOp::Unknown; return l;
    }
    const uint32_t ext = fExt(c);
    switch (ext) {
    case 0x30:   // MOVE (NOP is MOVE vf00, vf00)
        l.op = it && dest ? LowerOp::Move : LowerOp::Nop;
        stall(0, is, dest);
        if (it) vfWrite(it, dest);
        l.readsVf = static_cast<uint8_t>(is);
        l.writesVf = static_cast<uint8_t>(it);
        return l;
    case 0x31:   // MR32
        l.op = LowerOp::Mr32;
        stall(0, is, 0xF);
        vfWrite(it, dest);
        l.readsVf = static_cast<uint8_t>(is);
        l.writesVf = static_cast<uint8_t>(it);
        return l;
    case 0x34: case 0x36:   // LQI ft, (is++) / LQD ft, (--is)
        l.op = ext == 0x34 ? LowerOp::Lqi : LowerOp::Lqd;
        vfWrite(it, dest);
        l.writesVf = static_cast<uint8_t>(it);
        intWrite(is);
        return l;
    case 0x35: case 0x37:   // SQI fs, (it++) / SQD fs, (--it)
        l.op = ext == 0x35 ? LowerOp::Sqi : LowerOp::Sqd;
        stall(0, is, dest);
        vfWrite(0, 0);
        l.readsVf = static_cast<uint8_t>(is);
        intWrite(it);
        return l;
    case 0x38: case 0x3A:   // DIV Q, fs.fsf, ft.ftf / RSQRT
        l.op = ext == 0x38 ? LowerOp::Div : LowerOp::Rsqrt;
        stall(0, is, 8u >> fFsf(c));
        stall(1, it, 8u >> fFtf(c));
        l.wait = WaitFdiv;
        l.readsVf = static_cast<uint8_t>(is);
        l.fdiv = ext == 0x38 ? 7 : 13;
        return l;
    case 0x39:   // SQRT Q, ft.ftf
        l.op = LowerOp::Sqrt;
        stall(0, it, 8u >> fFtf(c));
        l.wait = WaitFdiv;
        l.readsVf = static_cast<uint8_t>(it);
        l.fdiv = 7;
        return l;
    case 0x3B: l.op = LowerOp::Waitq; l.wait = WaitFdiv; return l;
    case 0x3C:   // MTIR it, fs.fsf
        l.op = LowerOp::Mtir;
        stall(0, is, 8u >> fFsf(c));
        l.readsVf = static_cast<uint8_t>(is);
        return l;
    case 0x3D:   // MFIR ft, is
        l.op = LowerOp::Mfir;
        vfWrite(it, dest);
        l.writesVf = static_cast<uint8_t>(it);
        return l;
    case 0x3E:   // ILWR it, (is)
        l.op = LowerOp::Ilwr;
        l.loadsVi = true;
        l.ialuReg = static_cast<uint8_t>(it & 15);
        if (dest) intWrite(it);
        return l;
    case 0x3F: l.op = LowerOp::Iswr; return l;
    case 0x40: case 0x41:   // RNEXT / RGET
        l.op = ext == 0x40 ? LowerOp::Rnext : LowerOp::Rget;
        vfWrite(it, dest);
        l.writesVf = static_cast<uint8_t>(it);
        return l;
    case 0x42: case 0x43:   // RINIT / RXOR R, fs.fsf
        l.op = ext == 0x42 ? LowerOp::Rinit : LowerOp::Rxor;
        l.readsVf = static_cast<uint8_t>(is);
        return l;
    case 0x64:   // MFP ft, P
        l.op = LowerOp::Mfp;
        vfWrite(it, dest);
        l.writesVf = static_cast<uint8_t>(it);
        return l;
    case 0x68: l.op = LowerOp::Xtop; return l;
    case 0x69: l.op = LowerOp::Xitop; return l;
    case 0x6C: l.op = LowerOp::Xgkick; return l;   // on VU0 it reports itself
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: {
        constexpr uint8_t kLatency[7] = {11, 18, 18, 24, 54, 54, 12};   // ESADD ERSADD ELENG ERLENG EATANxy EATANxz ESUM
        l.op = ext == 0x74 || ext == 0x75 ? LowerOp::Eatan2 : LowerOp::Esum;
        stall(0, is, 0xF);
        l.wait = WaitEfu;
        l.readsVf = static_cast<uint8_t>(is);
        if (unit == 1) l.efu = kLatency[ext - 0x70];
        return l;
    }
    case 0x78: case 0x79: case 0x7A: case 0x7C: case 0x7D: case 0x7E: {
        constexpr uint8_t kLatency[7] = {12, 18, 12, 0, 29, 54, 44};   // ESQRT ERSQRT ERCPR - ESIN EATAN EEXP
        l.op = LowerOp::Escalar;
        stall(0, is, 8u >> fFsf(c));
        l.wait = WaitEfu;
        l.readsVf = static_cast<uint8_t>(is);
        if (unit == 1) l.efu = kLatency[ext - 0x78];
        return l;
    }
    case 0x7B: l.op = LowerOp::Waitp; l.wait = WaitP; return l;
    default: l.op = LowerOp::Unknown; return l;
    }
}

// ---- pairs ------------------------------------------------------------------------------
struct Pair {
    // What every pair is asked first, together.
    bool ibit = false, ebit = false;
    bool runLower = false;   // false with the I bit, or when the upper writes the VF register the lower writes
    uint8_t isolate = 0;     // a VF register the upper writes and the lower reads: the lower reads it as it was
    bool lowerStalls = false;   // the lower half can stall (I bit: never)
    bool upperStalls = false;   // the upper half waits for VF sources
    bool readsQ = false;        // the upper half reads Q (before its own stalls)
    bool startsUnits = false;   // the lower operation runs and starts the FDIV, the EFU or an ILW
    // What the pair sends down the FMAC pipeline.
    bool issues = false, clip = false, fsset = false;
    uint8_t writeReg[2] = {0, 0}, writeMask[2] = {0, 0};   // upper, lower
    Upper upper;
    Lower lower;
};

constexpr Pair decodePair(uint32_t lower, uint32_t upper, int unit) {
    Pair p;
    p.upper = decodeUpper(upper);
    p.lower = decodeLower(lower, unit);
    p.ebit = (upper & 0x40000000u) != 0;
    bool lowerTakesPart = false;
    if (upper & 0x80000000u) {   // I: the lower word is a float for the I register
        p.ibit = true;
    } else {
        const uint32_t ur = p.upper.writesVf;
        if (!ur || ur != p.lower.writesVf) {   // else the lower operation is dropped
            p.runLower = true;
            lowerTakesPart = true;
        }
        if (ur && p.lower.readsVf == ur) p.isolate = static_cast<uint8_t>(ur);
        p.lowerStalls = p.lower.stallMask[0] || p.lower.stallMask[1] || p.lower.wait != WaitNone || p.lower.branchRegs;
    }
    p.upperStalls = p.upper.stallMask[0] || p.upper.stallMask[1];
    p.readsQ = p.upper.op == UpperOp::Fmac && p.upper.form == Operand::Q;
    p.startsUnits = p.runLower && (p.lower.fdiv || p.lower.efu || p.lower.loadsVi);
    p.issues = p.upper.issues || (lowerTakesPart && p.lower.issues);
    p.clip = p.upper.clip || (lowerTakesPart && p.lower.clip);
    p.fsset = lowerTakesPart && p.lower.fsset;
    p.writeReg[0] = p.upper.writeReg;
    p.writeMask[0] = p.upper.writeMask;
    if (lowerTakesPart) {
        p.writeReg[1] = p.lower.writeReg;
        p.writeMask[1] = p.lower.writeMask;
    }
    return p;
}

// ---- timing ------------------------------------------------------------------------------
// The pipeline's clock and what is in flight. At most four FMAC results are
// in flight (one issued a cycle, each landing 4 cycles later); they wait in
// a ring, and a table holds the cycle at which each VF component's latest
// write lands, so a stall on a register is a lookup.
struct Timing {
    uint64_t cycle = 0;
    struct Entry {
        uint64_t done;
        bool clip, fsset;      // the pair wrote the clip flag / set the sticky status bits
    };
    Entry fmac[8] = {};        // in flight, oldest first from head (at most 4)
    uint32_t head = 0, count = 0;
    uint64_t lands[32][4] = {};
    uint64_t landsMax[32] = {};   // the latest of each register's four
    struct Busy {
        bool busy = false;
        uint64_t done = 0;
    } fdiv, efu;
    struct {
        uint64_t done = 0;
        uint32_t regs = 0;     // VI registers an ILW/ILWR is loading
    } ialu;
    int viBackupCycles = 0;    // a branch reads the old value of the VI register written just before it
};

// Hooks the timing calls as values land:
//   void commit(uint32_t slot);   the FMAC ring entry in `slot` lands: its flags become visible
//   void landQ();                 the FDIV result lands in Q (and the status D/I bits)
//   void landP();                 the EFU result lands in P
//   void retire();                runs retireDue() after a stall
template <class H>
PS2N_VU_INLINE void retireDue(Timing& t, H& h) {
    while (t.count && t.fmac[t.head].done <= t.cycle) {
        h.commit(t.head);
        t.head = (t.head + 1) & 7;
        --t.count;
    }
    if (t.fdiv.busy && t.fdiv.done <= t.cycle) {
        t.fdiv.busy = false;
        h.landQ();
    }
    if (t.efu.busy && t.efu.done <= t.cycle) {
        t.efu.busy = false;
        h.landP();
    }
    if (t.ialu.regs && t.ialu.done <= t.cycle) t.ialu.regs = 0;
}

// A stall: time moves on and whatever completes meanwhile becomes visible
// (through the hooks' `void retire()`, which runs retireDue; the
// interpreter keeps it out of line).
template <class H>
PS2N_VU_INLINE void advanceTo(Timing& t, uint64_t c, H& h) {
    if (c <= t.cycle) return;
    t.cycle = c;
    h.retire();
}

// When the latest write to VF register `reg` (components `mask`) lands; at
// most the current cycle once nothing of the register is in flight.
PS2N_VU_INLINE uint64_t landsOf(const Timing& t, uint32_t reg, uint32_t mask) {
    if (t.landsMax[reg] <= t.cycle) return 0;
    const uint64_t* lands = t.lands[reg];
    uint64_t until = 0;
    for (int i = 0; i < 4; ++i) until = std::max(until, lands[i] & (0 - static_cast<uint64_t>(on(mask, i))));
    return until;
}

// The lower half's stalls, which hold the whole pair: applied before either half reads.
template <class H>
PS2N_VU_COLD void lowerStalls(Timing& t, const Lower& l, H& h) {
    if (l.stallMask[0]) advanceTo(t, landsOf(t, l.stallReg[0], l.stallMask[0]), h);
    if (l.stallMask[1]) advanceTo(t, landsOf(t, l.stallReg[1], l.stallMask[1]), h);
    switch (l.wait) {
    case WaitFdiv:
        if (t.fdiv.busy) advanceTo(t, t.fdiv.done, h);
        break;
    case WaitEfu:
        if (t.efu.busy) advanceTo(t, t.efu.done - 1, h);   // released a cycle before P lands
        break;
    case WaitP:
        if (t.efu.busy) advanceTo(t, t.efu.done, h);
        break;
    default: break;
    }
    if (l.branchRegs && t.ialu.regs && t.cycle < t.ialu.done && (t.ialu.regs & l.branchRegs)) advanceTo(t, t.ialu.done, h);
}

// The upper half's own stalls, after it has read Q.
template <class H>
PS2N_VU_COLD void upperStalls(Timing& t, const Upper& u, H& h) {
    if (u.stallMask[0]) advanceTo(t, landsOf(t, u.stallReg[0], u.stallMask[0]), h);
    if (u.stallMask[1]) advanceTo(t, landsOf(t, u.stallReg[1], u.stallMask[1]), h);
}

// The units the lower operation starts, once it has run.
inline void startUnits(Timing& t, const Lower& l) {
    if (l.fdiv) {
        t.fdiv.busy = true;
        t.fdiv.done = t.cycle + l.fdiv;
    }
    if (l.efu) {
        t.efu.busy = true;
        t.efu.done = t.cycle + l.efu;
    }
    if (l.loadsVi) {
        t.ialu.done = t.cycle + 4;
        t.ialu.regs |= 1u << l.ialuReg;
    }
}

// The pair's flags enter the FMAC pipeline, landing with its VF results 4
// cycles later. Returns the ring slot.
PS2N_VU_INLINE uint32_t issue(Timing& t, const Pair& p) {
    const uint32_t slot = (t.head + t.count++) & 7;
    Timing::Entry& e = t.fmac[slot];
    e.done = t.cycle + 4;
    e.clip = p.clip;
    e.fsset = p.fsset;
    for (int n = 0; n < 2; ++n) {
        if (!p.writeReg[n]) continue;   // vf0 and ACC hold nothing up
        uint64_t* lands = t.lands[p.writeReg[n]];
        for (int i = 0; i < 4; ++i) lands[i] = on(p.writeMask[n], i) ? e.done : lands[i];
        if (p.writeMask[n]) t.landsMax[p.writeReg[n]] = e.done;
    }
    return slot;
}

// The program ends: everything in flight lands, in order.
template <class H>
PS2N_VU_COLD void flushAll(Timing& t, H& h) {
    for (; t.count; --t.count) {
        const Timing::Entry& e = t.fmac[t.head];
        h.commit(t.head);
        t.cycle = std::max(t.cycle, e.done);
        t.head = (t.head + 1) & 7;
    }
    if (t.fdiv.busy) {
        t.cycle = std::max(t.cycle, t.fdiv.done);
        h.retire();
    }
    if (t.efu.busy) {
        t.cycle = std::max(t.cycle, t.efu.done);
        h.retire();
    }
}

// What a pair's hooks run, in the interpreter's order:
//   void readQ();            the upper half reads Q (before its own stalls)
//   void upper();            the upper operation
//   void lower();            the lower operation (isolated as Pair::isolate says)
//   void loadI();            I bit: the lower word goes to I, after the upper read it
//   void issued(uint32_t slot); the pair's flags entered the FMAC ring at `slot`
//   plus commit / landQ / landP above.
// One pair's timing and values. The caller has advanced TPC, and handles
// branches and the E bit afterwards.
template <class H>
PS2N_VU_INLINE void stepPair(Timing& t, const Pair& p, H& h) {
    ++t.cycle;
    retireDue(t, h);
    if (t.viBackupCycles > 0) --t.viBackupCycles;
    if (p.lowerStalls) lowerStalls(t, p.lower, h);
    if (p.readsQ) h.readQ();
    if (p.upperStalls) upperStalls(t, p.upper, h);
    h.upper();
    if (p.ibit) {
        h.loadI();
    } else if (p.runLower) {
        h.lower();
        if (p.startsUnits) startUnits(t, p.lower);
    }
    if (p.issues) h.issued(issue(t, p));
}

} // namespace ps2n::vu
