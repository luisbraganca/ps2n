// R5900 (PlayStation 2 Emotion Engine) instruction decoding.
//
// The instruction set is described once, in r5900_ops.inc. This header turns
// that table into the Op enum and per-op metadata; decoder.cpp maps a raw
// 32-bit word to an Op; disasm.cpp and assembler.cpp format and parse the
// operand syntax listed in the table.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ps2n::r5900 {

// Classification flags (column 5 of r5900_ops.inc).
enum : uint32_t {
    F_BRANCH     = 1u << 0,   // PC-relative conditional branch
    F_JUMP       = 1u << 1,   // J / JAL (26-bit absolute target)
    F_JUMPREG    = 1u << 2,   // JR / JALR
    F_LINK       = 1u << 3,   // writes a return address
    F_LIKELY     = 1u << 4,   // delay slot only runs when the branch is taken
    F_DELAY      = 1u << 5,   // followed by a delay slot
    F_LOAD       = 1u << 6,
    F_STORE      = 1u << 7,
    F_SYSCALL    = 1u << 8,
    F_BREAK      = 1u << 9,
    F_TRAP       = 1u << 10,
    F_COP0       = 1u << 11,
    F_FPU        = 1u << 12,
    F_VU0        = 1u << 13,
    F_MMI        = 1u << 14,
    F_HINT       = 1u << 15,  // CACHE / PREF / SYNC: no architectural effect here
    F_ERET       = 1u << 16,
    F_WRITES_RT  = 1u << 17,
    F_WRITES_RD  = 1u << 18,
    F_VUDEST     = 1u << 19,  // has a .xyzw destination mask
    F_VUBC       = 1u << 20,  // bits 1-0 select a broadcast component
    F_INTERLOCK  = 1u << 21,  // bit 0 is the COP2 interlock (.i / .ni) bit
};

enum class Op : uint16_t {
    Invalid = 0,
#define X(op, mn, match, ops, flags, mem) op,
#include "ps2n/r5900_ops.inc"
#undef X
    Count
};

struct OpInfo {
    Op op;
    const char* mnemonic;
    uint32_t match;      // encoding with all operand fields zero
    const char* operands;
    uint32_t flags;
    uint8_t memSize;     // bytes accessed by loads / stores
};

const OpInfo& info(Op op);
inline const char* mnemonic(Op op) { return info(op).mnemonic; }
inline bool has(Op op, uint32_t f) { return (info(op).flags & f) != 0; }

// Raw word -> Op. Never throws; unknown encodings decode to Op::Invalid.
Op decodeOp(uint32_t raw);

// A decoded instruction: the raw word, its address, and field accessors.
// Field names follow the MIPS manuals; the same bits double as FPU and VU0
// register numbers (fs = rd bits, ft = rt bits, fd = sa bits).
struct Insn {
    uint32_t raw = 0;
    uint32_t addr = 0;
    Op op = Op::Invalid;

    Insn() = default;
    Insn(uint32_t word, uint32_t address) : raw(word), addr(address), op(decodeOp(word)) {}

    uint32_t opcode() const { return raw >> 26; }
    uint32_t rs() const { return (raw >> 21) & 31; }
    uint32_t rt() const { return (raw >> 16) & 31; }
    uint32_t rd() const { return (raw >> 11) & 31; }
    uint32_t sa() const { return (raw >> 6) & 31; }
    uint32_t funct() const { return raw & 63; }
    int32_t simm() const { return static_cast<int16_t>(raw & 0xFFFF); }
    uint32_t uimm() const { return raw & 0xFFFF; }
    uint32_t code20() const { return (raw >> 6) & 0xFFFFF; }
    uint32_t code10() const { return (raw >> 6) & 0x3FF; }

    // FPU / VU register fields.
    uint32_t fs() const { return rd(); }
    uint32_t ft() const { return rt(); }
    uint32_t fd() const { return sa(); }
    uint32_t base() const { return rs(); }

    // VU0 macro fields.
    uint32_t dest() const { return (raw >> 21) & 15; }   // bit 3 = x ... bit 0 = w
    uint32_t bc() const { return raw & 3; }               // 0=x 1=y 2=z 3=w
    uint32_t fsf() const { return (raw >> 21) & 3; }
    uint32_t ftf() const { return (raw >> 23) & 3; }
    int32_t imm5() const { return static_cast<int32_t>(static_cast<int8_t>(static_cast<uint8_t>(sa() << 3))) >> 3; }
    uint32_t imm15() const { return (raw >> 6) & 0x7FFF; }
    bool interlock() const { return (raw & 1) != 0; }
    uint32_t perfReg() const { return (raw >> 1) & 31; }

    bool valid() const { return op != Op::Invalid; }
    uint32_t flags() const { return info(op).flags; }
    bool is(uint32_t f) const { return (flags() & f) != 0; }
    bool hasDelaySlot() const { return is(F_DELAY); }
    bool isBranch() const { return is(F_BRANCH); }
    bool isLikely() const { return is(F_LIKELY); }
    bool isCall() const { return is(F_LINK); }
    bool isControlFlow() const { return is(F_BRANCH | F_JUMP | F_JUMPREG); }

    // Absolute target of a branch (PC-relative) or J/JAL (region-relative).
    uint32_t branchTarget() const { return addr + 4 + (static_cast<uint32_t>(simm()) << 2); }
    uint32_t jumpTarget() const { return ((addr + 4) & 0xF0000000u) | ((raw & 0x03FFFFFFu) << 2); }
    uint32_t staticTarget() const { return is(F_JUMP) ? jumpTarget() : branchTarget(); }

    // An unconditional transfer: execution never falls through past the delay slot.
    bool endsBlock() const;
    // "b"/"beq $zero,$zero" style branches that are always taken.
    bool isUnconditionalBranch() const;
    // jr $ra
    bool isReturn() const { return op == Op::JR && rs() == 31; }
    bool isNop() const { return raw == 0; }

    // Does this instruction write general-purpose register r? (Used by the
    // jump-table and constant-propagation helpers in the recompiler.)
    bool writesGpr(uint32_t r) const;
};

// Register names in GNU assembler syntax, without the '$'.
const char* gprName(uint32_t r);
const char* cop0Name(uint32_t r);   // "Status", "Count", ... or nullptr
const char* vuControlName(uint32_t r); // "Status", "MAC", "Clip", "R", "I", "Q", ...

std::string disassemble(const Insn& insn);
inline std::string disassemble(uint32_t raw, uint32_t addr) { return disassemble(Insn(raw, addr)); }

} // namespace ps2n::r5900
