// Structured R5900 decoder. The switch tree follows the opcode maps in the
// "EE Core Instruction Set Manual" (and matches rabbitizer's R5900 tables).
// Encodings the EE does not implement (DMULT, LL/SC, LDC1, ...) decode to
// Op::Invalid so the recompiler reports them instead of guessing.
#include "ps2n/r5900.h"

#include <array>

namespace ps2n::r5900 {

namespace {

constexpr OpInfo kInfo[] = {
    {Op::Invalid, "(invalid)", 0, "", 0, 0},
#define X(op, mn, match, ops, flags, mem) {Op::op, mn, match, ops, flags, mem},
#include "ps2n/r5900_ops.inc"
#undef X
};
static_assert(sizeof(kInfo) / sizeof(kInfo[0]) == static_cast<size_t>(Op::Count));

Op decodeSpecial(uint32_t raw) {
    switch (raw & 63) {
    case 0x00: return Op::SLL;
    case 0x02: return Op::SRL;
    case 0x03: return Op::SRA;
    case 0x04: return Op::SLLV;
    case 0x06: return Op::SRLV;
    case 0x07: return Op::SRAV;
    case 0x08: return Op::JR;
    case 0x09: return Op::JALR;
    case 0x0A: return Op::MOVZ;
    case 0x0B: return Op::MOVN;
    case 0x0C: return Op::SYSCALL;
    case 0x0D: return Op::BREAK;
    case 0x0F: return Op::SYNC;
    case 0x10: return Op::MFHI;
    case 0x11: return Op::MTHI;
    case 0x12: return Op::MFLO;
    case 0x13: return Op::MTLO;
    case 0x14: return Op::DSLLV;
    case 0x16: return Op::DSRLV;
    case 0x17: return Op::DSRAV;
    case 0x18: return Op::MULT;
    case 0x19: return Op::MULTU;
    case 0x1A: return Op::DIV;
    case 0x1B: return Op::DIVU;
    case 0x20: return Op::ADD;
    case 0x21: return Op::ADDU;
    case 0x22: return Op::SUB;
    case 0x23: return Op::SUBU;
    case 0x24: return Op::AND;
    case 0x25: return Op::OR;
    case 0x26: return Op::XOR;
    case 0x27: return Op::NOR;
    case 0x28: return Op::MFSA;
    case 0x29: return Op::MTSA;
    case 0x2A: return Op::SLT;
    case 0x2B: return Op::SLTU;
    case 0x2C: return Op::DADD;
    case 0x2D: return Op::DADDU;
    case 0x2E: return Op::DSUB;
    case 0x2F: return Op::DSUBU;
    case 0x30: return Op::TGE;
    case 0x31: return Op::TGEU;
    case 0x32: return Op::TLT;
    case 0x33: return Op::TLTU;
    case 0x34: return Op::TEQ;
    case 0x36: return Op::TNE;
    case 0x38: return Op::DSLL;
    case 0x3A: return Op::DSRL;
    case 0x3B: return Op::DSRA;
    case 0x3C: return Op::DSLL32;
    case 0x3E: return Op::DSRL32;
    case 0x3F: return Op::DSRA32;
    default: return Op::Invalid;   // includes DMULT/DDIV, absent on the EE
    }
}

Op decodeRegimm(uint32_t raw) {
    switch ((raw >> 16) & 31) {
    case 0x00: return Op::BLTZ;
    case 0x01: return Op::BGEZ;
    case 0x02: return Op::BLTZL;
    case 0x03: return Op::BGEZL;
    case 0x08: return Op::TGEI;
    case 0x09: return Op::TGEIU;
    case 0x0A: return Op::TLTI;
    case 0x0B: return Op::TLTIU;
    case 0x0C: return Op::TEQI;
    case 0x0E: return Op::TNEI;
    case 0x10: return Op::BLTZAL;
    case 0x11: return Op::BGEZAL;
    case 0x12: return Op::BLTZALL;
    case 0x13: return Op::BGEZALL;
    case 0x18: return Op::MTSAB;
    case 0x19: return Op::MTSAH;
    default: return Op::Invalid;
    }
}

Op decodeMmi0(uint32_t sub) {
    static constexpr std::array<Op, 32> t = {
        Op::PADDW,  Op::PSUBW,  Op::PCGTW,   Op::PMAXW,   Op::PADDH,  Op::PSUBH,  Op::PCGTH,   Op::PMAXH,
        Op::PADDB,  Op::PSUBB,  Op::PCGTB,   Op::Invalid, Op::Invalid,Op::Invalid,Op::Invalid, Op::Invalid,
        Op::PADDSW, Op::PSUBSW, Op::PEXTLW,  Op::PPACW,   Op::PADDSH, Op::PSUBSH, Op::PEXTLH,  Op::PPACH,
        Op::PADDSB, Op::PSUBSB, Op::PEXTLB,  Op::PPACB,   Op::Invalid,Op::Invalid,Op::PEXT5,   Op::PPAC5,
    };
    return t[sub];
}

Op decodeMmi1(uint32_t sub) {
    static constexpr std::array<Op, 32> t = {
        Op::Invalid,Op::PABSW,  Op::PCEQW,   Op::PMINW,   Op::PADSBH, Op::PABSH,  Op::PCEQH,   Op::PMINH,
        Op::Invalid,Op::Invalid,Op::PCEQB,   Op::Invalid, Op::Invalid,Op::Invalid,Op::Invalid, Op::Invalid,
        Op::PADDUW, Op::PSUBUW, Op::PEXTUW,  Op::Invalid, Op::PADDUH, Op::PSUBUH, Op::PEXTUH,  Op::Invalid,
        Op::PADDUB, Op::PSUBUB, Op::PEXTUB,  Op::QFSRV,   Op::Invalid,Op::Invalid,Op::Invalid, Op::Invalid,
    };
    return t[sub];
}

Op decodeMmi2(uint32_t sub) {
    static constexpr std::array<Op, 32> t = {
        Op::PMADDW, Op::Invalid,Op::PSLLVW,  Op::PSRLVW,  Op::PMSUBW, Op::Invalid,Op::Invalid, Op::Invalid,
        Op::PMFHI,  Op::PMFLO,  Op::PINTH,   Op::Invalid, Op::PMULTW, Op::PDIVW,  Op::PCPYLD,  Op::Invalid,
        Op::PMADDH, Op::PHMADH, Op::PAND,    Op::PXOR,    Op::PMSUBH, Op::PHMSBH, Op::Invalid, Op::Invalid,
        Op::Invalid,Op::Invalid,Op::PEXEH,   Op::PREVH,   Op::PMULTH, Op::PDIVBW, Op::PEXEW,   Op::PROT3W,
    };
    return t[sub];
}

Op decodeMmi3(uint32_t sub) {
    static constexpr std::array<Op, 32> t = {
        Op::PMADDUW,Op::Invalid,Op::Invalid, Op::PSRAVW,  Op::Invalid,Op::Invalid,Op::Invalid, Op::Invalid,
        Op::PMTHI,  Op::PMTLO,  Op::PINTEH,  Op::Invalid, Op::PMULTUW,Op::PDIVUW, Op::PCPYUD,  Op::Invalid,
        Op::Invalid,Op::Invalid,Op::POR,     Op::PNOR,    Op::Invalid,Op::Invalid,Op::Invalid, Op::Invalid,
        Op::Invalid,Op::Invalid,Op::PEXCH,   Op::PCPYH,   Op::Invalid,Op::Invalid,Op::PEXCW,   Op::Invalid,
    };
    return t[sub];
}

Op decodeMmi(uint32_t raw) {
    const uint32_t sub = (raw >> 6) & 31;
    switch (raw & 63) {
    case 0x00: return Op::MADD;
    case 0x01: return Op::MADDU;
    case 0x04: return Op::PLZCW;
    case 0x08: return decodeMmi0(sub);
    case 0x09: return decodeMmi2(sub);
    case 0x10: return Op::MFHI1;
    case 0x11: return Op::MTHI1;
    case 0x12: return Op::MFLO1;
    case 0x13: return Op::MTLO1;
    case 0x18: return Op::MULT1;
    case 0x19: return Op::MULTU1;
    case 0x1A: return Op::DIV1;
    case 0x1B: return Op::DIVU1;
    case 0x20: return Op::MADD1;
    case 0x21: return Op::MADDU1;
    case 0x28: return decodeMmi1(sub);
    case 0x29: return decodeMmi3(sub);
    case 0x30:
        switch (sub) {
        case 0: return Op::PMFHL_LW;
        case 1: return Op::PMFHL_UW;
        case 2: return Op::PMFHL_SLW;
        case 3: return Op::PMFHL_LH;
        case 4: return Op::PMFHL_SH;
        default: return Op::Invalid;
        }
    case 0x31: return sub == 0 ? Op::PMTHL_LW : Op::Invalid;
    case 0x34: return Op::PSLLH;
    case 0x36: return Op::PSRLH;
    case 0x37: return Op::PSRAH;
    case 0x3C: return Op::PSLLW;
    case 0x3E: return Op::PSRLW;
    case 0x3F: return Op::PSRAW;
    default: return Op::Invalid;
    }
}

Op decodeCop0(uint32_t raw) {
    const uint32_t rs = (raw >> 21) & 31;
    const uint32_t rd = (raw >> 11) & 31;
    switch (rs) {
    case 0x00:
        if (rd == 25) return (raw & 1) ? Op::MFPC : Op::MFPS;
        return Op::MFC0;
    case 0x04:
        if (rd == 25) return (raw & 1) ? Op::MTPC : Op::MTPS;
        return Op::MTC0;
    case 0x08:
        switch ((raw >> 16) & 31) {
        case 0: return Op::BC0F;
        case 1: return Op::BC0T;
        case 2: return Op::BC0FL;
        case 3: return Op::BC0TL;
        default: return Op::Invalid;
        }
    case 0x10:
        switch (raw & 63) {
        case 0x01: return Op::TLBR;
        case 0x02: return Op::TLBWI;
        case 0x06: return Op::TLBWR;
        case 0x08: return Op::TLBP;
        case 0x18: return Op::ERET;
        case 0x38: return Op::EI;
        case 0x39: return Op::DI;
        default: return Op::Invalid;
        }
    default: return Op::Invalid;
    }
}

Op decodeCop1(uint32_t raw) {
    const uint32_t rs = (raw >> 21) & 31;
    switch (rs) {
    case 0x00: return Op::MFC1;
    case 0x02: return Op::CFC1;
    case 0x04: return Op::MTC1;
    case 0x06: return Op::CTC1;
    case 0x08:
        switch ((raw >> 16) & 31) {
        case 0: return Op::BC1F;
        case 1: return Op::BC1T;
        case 2: return Op::BC1FL;
        case 3: return Op::BC1TL;
        default: return Op::Invalid;
        }
    case 0x10:
        switch (raw & 63) {
        case 0x00: return Op::ADD_S;
        case 0x01: return Op::SUB_S;
        case 0x02: return Op::MUL_S;
        case 0x03: return Op::DIV_S;
        case 0x04: return Op::SQRT_S;
        case 0x05: return Op::ABS_S;
        case 0x06: return Op::MOV_S;
        case 0x07: return Op::NEG_S;
        case 0x16: return Op::RSQRT_S;
        case 0x18: return Op::ADDA_S;
        case 0x19: return Op::SUBA_S;
        case 0x1A: return Op::MULA_S;
        case 0x1C: return Op::MADD_S;
        case 0x1D: return Op::MSUB_S;
        case 0x1E: return Op::MADDA_S;
        case 0x1F: return Op::MSUBA_S;
        case 0x24: return Op::CVT_W_S;
        case 0x28: return Op::MAX_S;
        case 0x29: return Op::MIN_S;
        case 0x30: return Op::C_F_S;
        case 0x32: return Op::C_EQ_S;
        case 0x34: return Op::C_LT_S;
        case 0x36: return Op::C_LE_S;
        default: return Op::Invalid;
        }
    case 0x14:
        return (raw & 63) == 0x20 ? Op::CVT_S_W : Op::Invalid;
    default: return Op::Invalid;
    }
}

Op decodeVuSpecial2(uint32_t raw) {
    const uint32_t idx = (((raw >> 6) & 31) << 2) | (raw & 3);
    switch (idx) {
    case 0x00: case 0x01: case 0x02: case 0x03: return Op::VADDAbc;
    case 0x04: case 0x05: case 0x06: case 0x07: return Op::VSUBAbc;
    case 0x08: case 0x09: case 0x0A: case 0x0B: return Op::VMADDAbc;
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: return Op::VMSUBAbc;
    case 0x10: return Op::VITOF0;
    case 0x11: return Op::VITOF4;
    case 0x12: return Op::VITOF12;
    case 0x13: return Op::VITOF15;
    case 0x14: return Op::VFTOI0;
    case 0x15: return Op::VFTOI4;
    case 0x16: return Op::VFTOI12;
    case 0x17: return Op::VFTOI15;
    case 0x18: case 0x19: case 0x1A: case 0x1B: return Op::VMULAbc;
    case 0x1C: return Op::VMULAq;
    case 0x1D: return Op::VABS;
    case 0x1E: return Op::VMULAi;
    case 0x1F: return Op::VCLIPw;
    case 0x20: return Op::VADDAq;
    case 0x21: return Op::VMADDAq;
    case 0x22: return Op::VADDAi;
    case 0x23: return Op::VMADDAi;
    case 0x24: return Op::VSUBAq;
    case 0x25: return Op::VMSUBAq;
    case 0x26: return Op::VSUBAi;
    case 0x27: return Op::VMSUBAi;
    case 0x28: return Op::VADDA;
    case 0x29: return Op::VMADDA;
    case 0x2A: return Op::VMULA;
    case 0x2C: return Op::VSUBA;
    case 0x2D: return Op::VMSUBA;
    case 0x2E: return Op::VOPMULA;
    case 0x2F: return Op::VNOP;
    case 0x30: return Op::VMOVE;
    case 0x31: return Op::VMR32;
    case 0x34: return Op::VLQI;
    case 0x35: return Op::VSQI;
    case 0x36: return Op::VLQD;
    case 0x37: return Op::VSQD;
    case 0x38: return Op::VDIV;
    case 0x39: return Op::VSQRT;
    case 0x3A: return Op::VRSQRT;
    case 0x3B: return Op::VWAITQ;
    case 0x3C: return Op::VMTIR;
    case 0x3D: return Op::VMFIR;
    case 0x3E: return Op::VILWR;
    case 0x3F: return Op::VISWR;
    case 0x40: return Op::VRNEXT;
    case 0x41: return Op::VRGET;
    case 0x42: return Op::VRINIT;
    case 0x43: return Op::VRXOR;
    default: return Op::Invalid;
    }
}

Op decodeVuSpecial1(uint32_t raw) {
    const uint32_t f = raw & 63;
    if (f < 0x1C) {
        static constexpr Op bcOps[7] = {Op::VADDbc, Op::VSUBbc, Op::VMADDbc, Op::VMSUBbc,
                                        Op::VMAXbc, Op::VMINIbc, Op::VMULbc};
        return bcOps[f >> 2];
    }
    switch (f) {
    case 0x1C: return Op::VMULq;
    case 0x1D: return Op::VMAXi;
    case 0x1E: return Op::VMULi;
    case 0x1F: return Op::VMINIi;
    case 0x20: return Op::VADDq;
    case 0x21: return Op::VMADDq;
    case 0x22: return Op::VADDi;
    case 0x23: return Op::VMADDi;
    case 0x24: return Op::VSUBq;
    case 0x25: return Op::VMSUBq;
    case 0x26: return Op::VSUBi;
    case 0x27: return Op::VMSUBi;
    case 0x28: return Op::VADD;
    case 0x29: return Op::VMADD;
    case 0x2A: return Op::VMUL;
    case 0x2B: return Op::VMAX;
    case 0x2C: return Op::VSUB;
    case 0x2D: return Op::VMSUB;
    case 0x2E: return Op::VOPMSUB;
    case 0x2F: return Op::VMINI;
    case 0x30: return Op::VIADD;
    case 0x31: return Op::VISUB;
    case 0x32: return Op::VIADDI;
    case 0x34: return Op::VIAND;
    case 0x35: return Op::VIOR;
    case 0x38: return Op::VCALLMS;
    case 0x39: return Op::VCALLMSR;
    case 0x3C: case 0x3D: case 0x3E: case 0x3F: return decodeVuSpecial2(raw);
    default: return Op::Invalid;
    }
}

Op decodeCop2(uint32_t raw) {
    if (raw & (1u << 25)) return decodeVuSpecial1(raw);
    switch ((raw >> 21) & 31) {
    case 0x01: return Op::QMFC2;
    case 0x02: return Op::CFC2;
    case 0x05: return Op::QMTC2;
    case 0x06: return Op::CTC2;
    case 0x08:
        switch ((raw >> 16) & 31) {
        case 0: return Op::BC2F;
        case 1: return Op::BC2T;
        case 2: return Op::BC2FL;
        case 3: return Op::BC2TL;
        default: return Op::Invalid;
        }
    default: return Op::Invalid;
    }
}

} // namespace

const OpInfo& info(Op op) {
    const auto i = static_cast<size_t>(op);
    return i < static_cast<size_t>(Op::Count) ? kInfo[i] : kInfo[0];
}

Op decodeOp(uint32_t raw) {
    switch (raw >> 26) {
    case 0x00: return decodeSpecial(raw);
    case 0x01: return decodeRegimm(raw);
    case 0x02: return Op::J;
    case 0x03: return Op::JAL;
    case 0x04: return Op::BEQ;
    case 0x05: return Op::BNE;
    case 0x06: return Op::BLEZ;
    case 0x07: return Op::BGTZ;
    case 0x08: return Op::ADDI;
    case 0x09: return Op::ADDIU;
    case 0x0A: return Op::SLTI;
    case 0x0B: return Op::SLTIU;
    case 0x0C: return Op::ANDI;
    case 0x0D: return Op::ORI;
    case 0x0E: return Op::XORI;
    case 0x0F: return Op::LUI;
    case 0x10: return decodeCop0(raw);
    case 0x11: return decodeCop1(raw);
    case 0x12: return decodeCop2(raw);
    case 0x14: return Op::BEQL;
    case 0x15: return Op::BNEL;
    case 0x16: return Op::BLEZL;
    case 0x17: return Op::BGTZL;
    case 0x18: return Op::DADDI;
    case 0x19: return Op::DADDIU;
    case 0x1A: return Op::LDL;
    case 0x1B: return Op::LDR;
    case 0x1C: return decodeMmi(raw);
    case 0x1E: return Op::LQ;
    case 0x1F: return Op::SQ;
    case 0x20: return Op::LB;
    case 0x21: return Op::LH;
    case 0x22: return Op::LWL;
    case 0x23: return Op::LW;
    case 0x24: return Op::LBU;
    case 0x25: return Op::LHU;
    case 0x26: return Op::LWR;
    case 0x27: return Op::LWU;
    case 0x28: return Op::SB;
    case 0x29: return Op::SH;
    case 0x2A: return Op::SWL;
    case 0x2B: return Op::SW;
    case 0x2C: return Op::SDL;
    case 0x2D: return Op::SDR;
    case 0x2E: return Op::SWR;
    case 0x2F: return Op::CACHE;
    case 0x31: return Op::LWC1;
    case 0x33: return Op::PREF;
    case 0x36: return Op::LQC2;
    case 0x37: return Op::LD;
    case 0x39: return Op::SWC1;
    case 0x3E: return Op::SQC2;
    case 0x3F: return Op::SD;
    default: return Op::Invalid;   // COP3, LL/SC, LDC1/SDC1, LLD/SCD: not on the EE
    }
}

bool Insn::isUnconditionalBranch() const {
    switch (op) {
    case Op::BEQ:
    case Op::BEQL:
        return rs() == rt();
    case Op::BGEZ:
    case Op::BGEZL:
    case Op::BGEZAL:
    case Op::BGEZALL:
    case Op::BLEZ:
    case Op::BLEZL:
        return rs() == 0;
    default:
        return false;
    }
}

bool Insn::endsBlock() const {
    if (op == Op::J || op == Op::JR) return true;
    if (op == Op::ERET) return true;
    return isUnconditionalBranch() && !isCall();
}

bool Insn::writesGpr(uint32_t r) const {
    if (r == 0) return false;
    const uint32_t f = flags();
    if ((f & F_WRITES_RT) && rt() == r) return true;
    if ((f & F_WRITES_RD) && rd() == r) {
        // R5900 MULT/MULTU/MADD... only write rd when it is non-zero, which
        // the r != 0 check above already covers.
        return true;
    }
    if (op == Op::JAL || op == Op::BLTZAL || op == Op::BGEZAL || op == Op::BLTZALL || op == Op::BGEZALL)
        return r == 31;
    if (op == Op::JALR) return rd() == r;
    return false;
}

namespace {
constexpr const char* kGprNames[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0",   "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0",   "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8",   "t9", "k0", "k1", "gp", "sp", "fp", "ra",
};
constexpr const char* kCop0Names[32] = {
    "Index",    "Random",  "EntryLo0", "EntryLo1", "Context", "PageMask", "Wired",   nullptr,
    "BadVAddr", "Count",   "EntryHi",  "Compare",  "Status",  "Cause",    "EPC",     "PRId",
    "Config",   nullptr,   nullptr,    nullptr,    nullptr,   nullptr,    nullptr,   "BadPAddr",
    "Debug",    "Perf",    nullptr,    nullptr,    "TagLo",   "TagHi",    "ErrorEPC", nullptr,
};
} // namespace

const char* gprName(uint32_t r) { return kGprNames[r & 31]; }
const char* cop0Name(uint32_t r) { return kCop0Names[r & 31]; }

const char* vuControlName(uint32_t r) {
    switch (r & 31) {
    case 16: return "Status";
    case 17: return "MAC";
    case 18: return "Clip";
    case 20: return "R";
    case 21: return "I";
    case 22: return "Q";
    case 26: return "TPC";
    case 27: return "CMSAR0";
    case 28: return "FBRST";
    case 29: return "VPU_STAT";
    case 31: return "CMSAR1";
    default: return nullptr;
    }
}

} // namespace ps2n::r5900
