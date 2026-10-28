// Emotion Engine (R5900) architectural state.
//
// There is exactly one CPU context, ps2::cpu, a global. Recompiled functions
// take no parameters and operate on it directly; kernel thread switches and
// interrupt delivery save and restore it wholesale. Keeping it a global (and
// guest RAM a separate global array) lets the host compiler prove that guest
// memory stores never alias guest registers, so register values stay in host
// registers across loads and stores.
#pragma once

#include <cstdint>

namespace ps2 {

// 128-bit GPR. d[0] holds bits 0-63 (what ordinary MIPS instructions see),
// d[1] bits 64-127 (only MMI, LQ/SQ and PCPY* touch them).
struct alignas(16) Gpr {
    uint64_t d[2];
};

// VU0 vector register: raw IEEE bit patterns, x y z w in memory order.
struct alignas(16) VuReg {
    uint32_t c[4];
};

// Indices into Cpu::vi[] for the VU0 control registers (CFC2/CTC2 numbering).
enum VuControl : uint32_t {
    VU_STATUS = 16, VU_MAC = 17, VU_CLIP = 18, VU_R = 20, VU_I = 21, VU_Q = 22,
    VU_TPC = 26, VU_CMSAR0 = 27, VU_FBRST = 28, VU_VPU_STAT = 29, VU_CMSAR1 = 31,
};

// COP0 register numbers.
enum Cop0Reg : uint32_t {
    COP0_INDEX = 0, COP0_RANDOM = 1, COP0_ENTRYLO0 = 2, COP0_ENTRYLO1 = 3, COP0_CONTEXT = 4,
    COP0_PAGEMASK = 5, COP0_WIRED = 6, COP0_BADVADDR = 8, COP0_COUNT = 9, COP0_ENTRYHI = 10,
    COP0_COMPARE = 11, COP0_STATUS = 12, COP0_CAUSE = 13, COP0_EPC = 14, COP0_PRID = 15,
    COP0_CONFIG = 16, COP0_BADPADDR = 23, COP0_DEBUG = 24, COP0_PERF = 25, COP0_TAGLO = 28,
    COP0_TAGHI = 29, COP0_ERROREPC = 30,
};

struct Cpu {
    Gpr gpr[32];
    Gpr hi;            // d[0] = HI, d[1] = HI1
    Gpr lo;            // d[0] = LO, d[1] = LO1
    uint32_t sa;       // QFSRV shift amount, in bytes (MTSAB/MTSAH)
    uint32_t pc;       // guest PC; maintained at control transfers, not per instruction

    uint32_t cop0[32];
    uint32_t pccr, pcr0, pcr1;   // performance counter registers (MFPS/MFPC)

    uint32_t fpr[32];  // FPU registers, raw bits (the EE FPU is not IEEE; keep exact patterns)
    uint32_t facc;     // FPU accumulator (ADDA.S, MADD.S, ...)
    uint32_t fcr31;    // FPU control/status: C flag is bit 23

    VuReg vf[32];      // VU0 vector registers; vf0 is constant (0, 0, 0, 1)
    VuReg vacc;        // VU0 accumulator
    uint32_t vi[32];   // vi0-vi15 integer registers (16-bit), 16-31 control registers
};

extern Cpu cpu;

// Register names for generated code ("ADDIU(SP, SP, -32)").
namespace regs {
enum : uint32_t {
    ZERO = 0, AT, V0, V1, A0, A1, A2, A3,
    T0, T1, T2, T3, T4, T5, T6, T7,
    S0, S1, S2, S3, S4, S5, S6, S7,
    T8, T9, K0, K1, GP, SP, FP, RA,
};
} // namespace regs

// GPR accessors. Writes to $zero are discarded; for constant register numbers
// the guard folds away.
inline uint64_t ru64(uint32_t r) { return cpu.gpr[r].d[0]; }
inline int64_t rs64(uint32_t r) { return static_cast<int64_t>(cpu.gpr[r].d[0]); }
inline uint32_t ru32(uint32_t r) { return static_cast<uint32_t>(cpu.gpr[r].d[0]); }
inline int32_t rs32(uint32_t r) { return static_cast<int32_t>(cpu.gpr[r].d[0]); }
inline uint64_t rhi64(uint32_t r) { return cpu.gpr[r].d[1]; }

inline void setU64(uint32_t r, uint64_t v) {
    if (r) cpu.gpr[r].d[0] = v;
}
inline void setS32(uint32_t r, int32_t v) {   // sign-extends into bits 32-63
    if (r) cpu.gpr[r].d[0] = static_cast<uint64_t>(static_cast<int64_t>(v));
}
inline void setU32(uint32_t r, uint32_t v) { setS32(r, static_cast<int32_t>(v)); }
inline void set128(uint32_t r, uint64_t lo, uint64_t hi) {
    if (r) {
        cpu.gpr[r].d[0] = lo;
        cpu.gpr[r].d[1] = hi;
    }
}
// Return-address write for JAL / JALR / BGEZAL ... (sign-extended like any 32-bit result).
inline void setLink(uint32_t r, uint32_t addr) { setS32(r, static_cast<int32_t>(addr)); }

} // namespace ps2
