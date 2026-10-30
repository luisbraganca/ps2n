// R3000A interpreter and the IOP memory map.
#include "iop/cpu.h"

#include "internal.h"
#include "iop/iop.h"
#include "iop/kernel.h"
#include "iop/spu2.h"
#include "log.h"

#include <array>
#include <cstring>
#include <vector>

namespace ps2::iop {

Cpu g_cpu;

namespace {

std::array<uint8_t, mem::kRamSize> g_ram;
std::array<uint8_t, 1024> g_scratchpad;
std::array<uint8_t, 0x10000> g_ioRegs;     // 0x1F800000-0x1F80FFFF beyond the scratchpad: last written values

struct Trap {
    HleFn fn;
    std::string name;
};
std::vector<Trap> g_traps;
uint32_t g_returnTrap = 0;   // where nested calls return to
uint32_t g_currentTrap = 0;
uint64_t g_instructions = 0;
int g_nesting = 0;
bool g_nestedDone = false;

constexpr uint32_t kBiosBase = 0x1FC00000u, kBiosSize = 4u << 20;
constexpr uint32_t kRomverOffset = 0x7FF52;

uint32_t load32le(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void reportIo(const char* what, uint32_t addr, uint32_t value) {
    char key[64];
    std::snprintf(key, sizeof key, "IOP register %s 0x%08x", what, addr & ~0xFu);
    PS2_UNIMPL("iop", key, "IOP %s of unmodelled register 0x%08x (value 0x%x, pc 0x%08x)", what, addr, value, g_cpu.pc);
}

// Root counters: 0-2 at 0x1F801100 + 0x10*n, 3-5 at 0x1F801480 + 0x10*(n-3).
// Only the count is modelled, at the system clock.
uint32_t timerCount(uint32_t phys) {
    const uint64_t t = kernel::systemTime();
    const bool wide = phys >= 0x1F801480u;
    return wide ? static_cast<uint32_t>(t) : static_cast<uint32_t>(t & 0xFFFF);
}

uint32_t slowRead(uint32_t addr, int size) {
    if (addr >= 0xFFFE0000u) return 0;   // cache control
    const uint32_t p = addr & 0x1FFFFFFFu;
    auto pick = [&](const uint8_t* base) -> uint32_t {
        uint32_t v = 0;
        std::memcpy(&v, base, static_cast<size_t>(size));
        return v;
    };
    if (p >= 0x1F800000u && p < 0x1F800400u) return pick(&g_scratchpad[p & 0x3FF]);
    if (p >= 0x1F801070u && p < 0x1F801080u) return kernel::intcRead(p);
    if ((p >= 0x1F801100u && p < 0x1F801130u && (p & 0xF) == 0) || (p >= 0x1F801480u && p < 0x1F8014B0u && (p & 0xF) == 0))
        return timerCount(p);
    if (p >= 0x1D000000u && p < 0x1D000100u) return sifRegReadIop(p);
    if (p >= 0x1F801000u && p < 0x1F810000u) {
        const uint32_t v = pick(&g_ioRegs[p & 0xFFFF]);
        reportIo("read", p, v);
        return v;
    }
    if (p >= 0x1F900000u && p < 0x1F900800u) {   // the SPU2's 16-bit registers
        uint32_t v = spu2::read16(p & ~1u);
        if (size == 4) v |= static_cast<uint32_t>(spu2::read16((p & ~1u) + 2)) << 16;
        else if (size == 1) v = (v >> ((p & 1) * 8)) & 0xFF;
        return v;
    }
    if (p >= kBiosBase && p < kBiosBase + kBiosSize) {
        const std::string_view romver = romVersion();
        uint32_t v = 0;
        for (int i = 0; i < size; ++i) {
            const uint32_t off = p - kBiosBase + static_cast<uint32_t>(i) - kRomverOffset;
            if (off < romver.size()) v |= static_cast<uint32_t>(static_cast<uint8_t>(romver[off])) << (8 * i);
        }
        return v;
    }
    reportIo("read", p, 0);
    return 0;
}

void slowWrite(uint32_t addr, uint32_t v, int size) {
    if (addr >= 0xFFFE0000u) return;
    const uint32_t p = addr & 0x1FFFFFFFu;
    auto put = [&](uint8_t* base) { std::memcpy(base, &v, static_cast<size_t>(size)); };
    if (p >= 0x1F800000u && p < 0x1F800400u) return put(&g_scratchpad[p & 0x3FF]);
    if (p >= 0x1F801070u && p < 0x1F801080u) return kernel::intcWrite(p, v);
    if (p >= 0x1D000000u && p < 0x1D000100u) return sifRegWriteIop(p, v);
    if (p >= 0x1F801000u && p < 0x1F810000u) {
        put(&g_ioRegs[p & 0xFFFF]);
        return reportIo("write", p, v);
    }
    if (p >= 0x1F900000u && p < 0x1F900800u && size != 1) {
        spu2::write16(p, static_cast<uint16_t>(v));
        if (size == 4) spu2::write16(p + 2, static_cast<uint16_t>(v >> 16));
        return;
    }
    reportIo("write", p, v);
}

void exception(const char* what, uint32_t pc, uint32_t insn) {
    char key[80];
    std::snprintf(key, sizeof key, "IOP %s", what);
    PS2_UNIMPL("iop", key, "IOP %s at 0x%08x (instruction 0x%08x); continuing", what, pc, insn);
}

inline void setReg(uint32_t r, uint32_t v) {
    g_cpu.gpr[r] = v;
}

inline void branch(bool taken, uint32_t pc, int32_t simm) {
    if (taken) g_cpu.npc = pc + 4 + (static_cast<uint32_t>(simm) << 2);
}

void runTrap(uint32_t pc) {
    const uint32_t idx = ((pc & 0x1FFFFFFFu) - kHleBase) >> 3;
    const uint32_t ra = g_cpu.gpr[R_RA];
    g_cpu.pc = ra;
    g_cpu.npc = ra + 4;
    ++g_instructions;
    if (idx < g_traps.size() && g_traps[idx].fn) {
        g_currentTrap = pc;
        g_traps[idx].fn();
    } else {
        PS2_ERROR("iop", "jump to unassigned trap address 0x%08x", pc);
        kernel::faultCurrent();
    }
}

inline void exec(uint32_t in, uint32_t pc) {
    uint32_t* r = g_cpu.gpr;
    const uint32_t rs = (in >> 21) & 31, rt = (in >> 16) & 31, rd = (in >> 11) & 31, sa = (in >> 6) & 31;
    const int32_t simm = static_cast<int16_t>(in & 0xFFFF);
    const uint32_t uimm = in & 0xFFFF;
    switch (in >> 26) {
    case 0x00:
        switch (in & 63) {
        case 0x00: setReg(rd, r[rt] << sa); break;
        case 0x02: setReg(rd, r[rt] >> sa); break;
        case 0x03: setReg(rd, static_cast<uint32_t>(static_cast<int32_t>(r[rt]) >> sa)); break;
        case 0x04: setReg(rd, r[rt] << (r[rs] & 31)); break;
        case 0x06: setReg(rd, r[rt] >> (r[rs] & 31)); break;
        case 0x07: setReg(rd, static_cast<uint32_t>(static_cast<int32_t>(r[rt]) >> (r[rs] & 31))); break;
        case 0x08: g_cpu.npc = r[rs]; break;
        case 0x09: {
            const uint32_t target = r[rs];
            setReg(rd, pc + 8);
            g_cpu.npc = target;
            break;
        }
        case 0x0C: kernel::syscallInstruction(pc, in); break;
        case 0x0D: exception("BREAK", pc, in); break;
        case 0x10: setReg(rd, g_cpu.hi); break;
        case 0x11: g_cpu.hi = r[rs]; break;
        case 0x12: setReg(rd, g_cpu.lo); break;
        case 0x13: g_cpu.lo = r[rs]; break;
        case 0x18: {
            const int64_t p = static_cast<int64_t>(static_cast<int32_t>(r[rs])) * static_cast<int32_t>(r[rt]);
            g_cpu.lo = static_cast<uint32_t>(p);
            g_cpu.hi = static_cast<uint32_t>(static_cast<uint64_t>(p) >> 32);
            break;
        }
        case 0x19: {
            const uint64_t p = static_cast<uint64_t>(r[rs]) * r[rt];
            g_cpu.lo = static_cast<uint32_t>(p);
            g_cpu.hi = static_cast<uint32_t>(p >> 32);
            break;
        }
        case 0x1A: {
            const auto n = static_cast<int32_t>(r[rs]), d = static_cast<int32_t>(r[rt]);
            if (d == 0) {
                g_cpu.lo = n >= 0 ? 0xFFFFFFFFu : 1u;
                g_cpu.hi = static_cast<uint32_t>(n);
            } else if (n == INT32_MIN && d == -1) {
                g_cpu.lo = 0x80000000u;
                g_cpu.hi = 0;
            } else {
                g_cpu.lo = static_cast<uint32_t>(n / d);
                g_cpu.hi = static_cast<uint32_t>(n % d);
            }
            break;
        }
        case 0x1B:
            if (r[rt] == 0) {
                g_cpu.lo = 0xFFFFFFFFu;
                g_cpu.hi = r[rs];
            } else {
                g_cpu.lo = r[rs] / r[rt];
                g_cpu.hi = r[rs] % r[rt];
            }
            break;
        case 0x20: {
            const int64_t s = static_cast<int64_t>(static_cast<int32_t>(r[rs])) + static_cast<int32_t>(r[rt]);
            if (s != static_cast<int32_t>(s)) exception("integer overflow (ADD)", pc, in);
            else setReg(rd, static_cast<uint32_t>(s));
            break;
        }
        case 0x21: setReg(rd, r[rs] + r[rt]); break;
        case 0x22: {
            const int64_t s = static_cast<int64_t>(static_cast<int32_t>(r[rs])) - static_cast<int32_t>(r[rt]);
            if (s != static_cast<int32_t>(s)) exception("integer overflow (SUB)", pc, in);
            else setReg(rd, static_cast<uint32_t>(s));
            break;
        }
        case 0x23: setReg(rd, r[rs] - r[rt]); break;
        case 0x24: setReg(rd, r[rs] & r[rt]); break;
        case 0x25: setReg(rd, r[rs] | r[rt]); break;
        case 0x26: setReg(rd, r[rs] ^ r[rt]); break;
        case 0x27: setReg(rd, ~(r[rs] | r[rt])); break;
        case 0x2A: setReg(rd, static_cast<int32_t>(r[rs]) < static_cast<int32_t>(r[rt]) ? 1 : 0); break;
        case 0x2B: setReg(rd, r[rs] < r[rt] ? 1 : 0); break;
        default: exception("reserved instruction", pc, in); break;
        }
        break;
    case 0x01: {
        const bool ge = (rt & 1) != 0;
        const bool taken = ge ? static_cast<int32_t>(r[rs]) >= 0 : static_cast<int32_t>(r[rs]) < 0;
        if ((rt & 0x1E) == 0x10) setReg(R_RA, pc + 8);   // BLTZAL / BGEZAL link even when not taken
        branch(taken, pc, simm);
        break;
    }
    case 0x02: g_cpu.npc = ((pc + 4) & 0xF0000000u) | ((in & 0x03FFFFFFu) << 2); break;
    case 0x03:
        setReg(R_RA, pc + 8);
        g_cpu.npc = ((pc + 4) & 0xF0000000u) | ((in & 0x03FFFFFFu) << 2);
        break;
    case 0x04: branch(r[rs] == r[rt], pc, simm); break;
    case 0x05: branch(r[rs] != r[rt], pc, simm); break;
    case 0x06: branch(static_cast<int32_t>(r[rs]) <= 0, pc, simm); break;
    case 0x07: branch(static_cast<int32_t>(r[rs]) > 0, pc, simm); break;
    case 0x08: {
        const int64_t s = static_cast<int64_t>(static_cast<int32_t>(r[rs])) + simm;
        if (s != static_cast<int32_t>(s)) exception("integer overflow (ADDI)", pc, in);
        else setReg(rt, static_cast<uint32_t>(s));
        break;
    }
    case 0x09: setReg(rt, r[rs] + static_cast<uint32_t>(simm)); break;
    case 0x0A: setReg(rt, static_cast<int32_t>(r[rs]) < simm ? 1 : 0); break;
    case 0x0B: setReg(rt, r[rs] < static_cast<uint32_t>(simm) ? 1 : 0); break;
    case 0x0C: setReg(rt, r[rs] & uimm); break;
    case 0x0D: setReg(rt, r[rs] | uimm); break;
    case 0x0E: setReg(rt, r[rs] ^ uimm); break;
    case 0x0F: setReg(rt, uimm << 16); break;
    case 0x10:   // COP0
        if (rs == 0x00) {
            setReg(rt, rd == 12 ? g_cpu.sr : rd == 13 ? g_cpu.cause : rd == 14 ? g_cpu.epc : rd == 15 ? 0x1Fu : 0);
        } else if (rs == 0x04) {
            if (rd == 12) kernel::setStatusRegister(r[rt]);
            else if (rd == 13) g_cpu.cause = r[rt];
            else if (rd == 14) g_cpu.epc = r[rt];
        } else if (rs == 0x10 && (in & 63) == 0x10) {   // RFE
            g_cpu.sr = (g_cpu.sr & ~0xFu) | ((g_cpu.sr >> 2) & 0xFu);
        } else {
            exception("COP0 instruction", pc, in);
        }
        break;
    case 0x20: setReg(rt, static_cast<uint32_t>(static_cast<int8_t>(mem::read8(r[rs] + static_cast<uint32_t>(simm))))); break;
    case 0x21: setReg(rt, static_cast<uint32_t>(static_cast<int16_t>(mem::read16(r[rs] + static_cast<uint32_t>(simm))))); break;
    case 0x22: {
        const uint32_t a = r[rs] + static_cast<uint32_t>(simm), sh = (a & 3) * 8;
        const uint32_t w = mem::read32(a & ~3u);
        setReg(rt, (r[rt] & (0x00FFFFFFu >> sh)) | (w << (24 - sh)));
        break;
    }
    case 0x23: setReg(rt, mem::read32(r[rs] + static_cast<uint32_t>(simm))); break;
    case 0x24: setReg(rt, mem::read8(r[rs] + static_cast<uint32_t>(simm))); break;
    case 0x25: setReg(rt, mem::read16(r[rs] + static_cast<uint32_t>(simm))); break;
    case 0x26: {
        const uint32_t a = r[rs] + static_cast<uint32_t>(simm), sh = (a & 3) * 8;
        const uint32_t w = mem::read32(a & ~3u);
        setReg(rt, (r[rt] & (0xFFFFFF00u << (24 - sh))) | (w >> sh));
        break;
    }
    case 0x28: mem::write8(r[rs] + static_cast<uint32_t>(simm), static_cast<uint8_t>(r[rt])); break;
    case 0x29: mem::write16(r[rs] + static_cast<uint32_t>(simm), static_cast<uint16_t>(r[rt])); break;
    case 0x2A: {
        const uint32_t a = r[rs] + static_cast<uint32_t>(simm), sh = (a & 3) * 8;
        const uint32_t w = mem::read32(a & ~3u);
        mem::write32(a & ~3u, (w & (0xFFFFFF00u << sh)) | (r[rt] >> (24 - sh)));
        break;
    }
    case 0x2B: mem::write32(r[rs] + static_cast<uint32_t>(simm), r[rt]); break;
    case 0x2E: {
        const uint32_t a = r[rs] + static_cast<uint32_t>(simm), sh = (a & 3) * 8;
        const uint32_t w = mem::read32(a & ~3u);
        mem::write32(a & ~3u, (w & (0x00FFFFFFu >> (24 - sh))) | (r[rt] << sh));
        break;
    }
    default: exception("reserved instruction", pc, in); break;
    }
    r[0] = 0;
}

inline uint32_t fetch(uint32_t pc) {
    const uint32_t p = pc & 0x1FFFFFFFu;
    if (p < 0x00800000u && (pc & 3) == 0) return load32le(&g_ram[p & (mem::kRamSize - 1)]);
    return mem::read32(pc);
}

inline void step() {
    const uint32_t pc = g_cpu.pc;
    if ((pc & 0x1FFFFFFFu) - kHleBase < kHleSize) {
        runTrap(pc);
        return;
    }
    const uint32_t in = fetch(pc);
    g_cpu.pc = g_cpu.npc;
    g_cpu.npc += 4;
    ++g_instructions;
    exec(in, pc);
}

void nestedReturn() { g_nestedDone = true; }

} // namespace

// ---- memory ----------------------------------------------------------------------------------
namespace mem {

uint8_t* ram() { return g_ram.data(); }

uint8_t read8(uint32_t addr) {
    const uint32_t p = addr & 0x1FFFFFFFu;
    if (p < 0x00800000u) return g_ram[p & (kRamSize - 1)];
    return static_cast<uint8_t>(slowRead(addr, 1));
}
uint16_t read16(uint32_t addr) {
    const uint32_t p = addr & 0x1FFFFFFFu;
    if (p < 0x00800000u) {
        uint16_t v;
        std::memcpy(&v, &g_ram[p & (kRamSize - 2)], 2);
        return v;
    }
    return static_cast<uint16_t>(slowRead(addr, 2));
}
uint32_t read32(uint32_t addr) {
    const uint32_t p = addr & 0x1FFFFFFFu;
    if (p < 0x00800000u) return load32le(&g_ram[p & (kRamSize - 4)]);
    return slowRead(addr, 4);
}
void write8(uint32_t addr, uint8_t v) {
    const uint32_t p = addr & 0x1FFFFFFFu;
    if (p < 0x00800000u) {
        g_ram[p & (kRamSize - 1)] = v;
        return;
    }
    slowWrite(addr, v, 1);
}
void write16(uint32_t addr, uint16_t v) {
    const uint32_t p = addr & 0x1FFFFFFFu;
    if (p < 0x00800000u) {
        std::memcpy(&g_ram[p & (kRamSize - 2)], &v, 2);
        return;
    }
    slowWrite(addr, v, 2);
}
void write32(uint32_t addr, uint32_t v) {
    const uint32_t p = addr & 0x1FFFFFFFu;
    if (p < 0x00800000u) {
        std::memcpy(&g_ram[p & (kRamSize - 4)], &v, 4);
        return;
    }
    slowWrite(addr, v, 4);
}
bool inRam(uint32_t addr, uint32_t size) {
    const uint32_t p = addr & 0x1FFFFFFFu;
    return p < 0x00800000u && (p & (kRamSize - 1)) + size <= kRamSize;
}
std::string readString(uint32_t addr, size_t max) {
    std::string s;
    for (size_t i = 0; i < max; ++i) {
        const char c = static_cast<char>(read8(addr + static_cast<uint32_t>(i)));
        if (!c) break;
        s.push_back(c);
    }
    return s;
}
void readBlock(uint32_t addr, void* dst, uint32_t size) {
    if (inRam(addr, size)) {
        std::memcpy(dst, &g_ram[addr & 0x1FFFFFFFu & (kRamSize - 1)], size);
        return;
    }
    auto* d = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < size; ++i) d[i] = read8(addr + i);
}
void writeBlock(uint32_t addr, const void* src, uint32_t size) {
    if (inRam(addr, size)) {
        std::memcpy(&g_ram[addr & 0x1FFFFFFFu & (kRamSize - 1)], src, size);
        return;
    }
    const auto* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < size; ++i) write8(addr + i, s[i]);
}
void fill(uint32_t addr, uint8_t value, uint32_t size) {
    if (inRam(addr, size)) {
        std::memset(&g_ram[addr & 0x1FFFFFFFu & (kRamSize - 1)], value, size);
        return;
    }
    for (uint32_t i = 0; i < size; ++i) write8(addr + i, value);
}

} // namespace mem

// ---- traps and execution -------------------------------------------------------------------
uint32_t addTrap(HleFn fn, const std::string& name) {
    const uint32_t addr = kHleBase + static_cast<uint32_t>(g_traps.size()) * 8;
    if (addr >= kHleBase + kHleSize) {
        PS2_ERROR("iop", "out of trap addresses");
        return kHleBase;
    }
    g_traps.push_back({fn, name});
    return addr;
}

const std::string& trapName(uint32_t addr) {
    static const std::string none;
    const uint32_t idx = ((addr & 0x1FFFFFFFu) - kHleBase) >> 3;
    return idx < g_traps.size() ? g_traps[idx].name : none;
}

int64_t execute(int64_t budget) {
    int64_t done = 0;
    while (done < budget && kernel::running()) {
        step();
        ++done;
    }
    return done;
}

uint32_t callGuest(uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t gp, uint32_t sp) {
    const Cpu saved = g_cpu;
    const bool savedDone = g_nestedDone;
    g_cpu.gpr[R_A0] = a0;
    g_cpu.gpr[R_A1] = a1;
    g_cpu.gpr[R_A2] = a2;
    g_cpu.gpr[R_A3] = a3;
    if (gp) g_cpu.gpr[R_GP] = gp;
    if (sp) g_cpu.gpr[R_SP] = sp;
    g_cpu.gpr[R_SP] -= 32;   // argument save area
    g_cpu.gpr[R_RA] = g_returnTrap;
    g_cpu.pc = addr;
    g_cpu.npc = addr + 4;
    g_nestedDone = false;
    ++g_nesting;
    uint64_t guard = 0;
    while (!g_nestedDone) {
        step();
        if (++guard == 200000000) {
            PS2_ERROR("iop", "nested call to 0x%08x has not returned after 200M instructions (pc 0x%08x); abandoning it", addr,
                      g_cpu.pc);
            break;
        }
    }
    --g_nesting;
    const uint32_t v0 = g_cpu.gpr[R_V0];
    g_cpu = saved;
    g_nestedDone = savedDone;
    return v0;
}

uint32_t currentTrap() { return g_currentTrap; }
int nestingDepth() { return g_nesting; }
uint64_t instructionCount() { return g_instructions; }

void initCpu() {
    g_cpu = Cpu{};
    g_ram.fill(0);
    g_scratchpad.fill(0);
    g_ioRegs.fill(0);
    g_traps.clear();
    g_traps.push_back({nullptr, "(null trap)"});   // trap 0 is never handed out
    g_returnTrap = addTrap(&nestedReturn, "(return from nested call)");
    g_instructions = 0;
    g_nesting = 0;
    g_nestedDone = false;
}

} // namespace ps2::iop
