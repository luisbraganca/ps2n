// COP0 registers, performance counters, and the CPU exception paths that
// recompiled code reports (traps, BREAK, overflow, ERET, TLB ops).
#include "clock.h"
#include "hw/hw.h"
#include "internal.h"
#include "log.h"
#include "ps2/ops_cop0.h"
#include "ps2/ops_int.h"

#include <cfenv>
#include <cstdio>

namespace ps2 {

namespace {
uint64_t g_countBase = 0;    // nowCycles() when Count was last written
uint32_t g_countValue = 0;   // value written at that time
}

void initCop0() {
    for (auto& r : cpu.cop0) r = 0;
    cpu.cop0[COP0_STATUS] = 0x70018C01u;   // CU0-2, EIE, IM2/IM3/IM7, IE
    cpu.cop0[COP0_PRID] = 0x00002E20u;     // EE revision as reported by retail consoles
    cpu.cop0[COP0_CONFIG] = 0x00000440u;
    cpu.fcr31 = 0x01000001u;
    cpu.vf[0].c[3] = 0x3F800000u;          // vf0 = (0, 0, 0, 1)
    g_countBase = nowCycles();
    g_countValue = 0;
}

uint32_t cop0Read(uint32_t reg) {
    switch (reg & 31) {
    case COP0_COUNT: return g_countValue + static_cast<uint32_t>(nowCycles() - g_countBase);
    case COP0_RANDOM: return static_cast<uint32_t>(nowCycles() % 48);
    default: return cpu.cop0[reg & 31];
    }
}

void cop0Write(uint32_t reg, uint32_t value) {
    switch (reg & 31) {
    case COP0_COUNT:
        g_countValue = value;
        g_countBase = nowCycles();
        return;
    case COP0_COMPARE:
        cpu.cop0[COP0_COMPARE] = value;
        cpu.cop0[COP0_CAUSE] &= ~0x8000u;   // clears the timer interrupt (IP7)
        return;
    case COP0_PRID:
    case COP0_BADVADDR:
        return;   // read-only
    default:
        cpu.cop0[reg & 31] = value;
        return;
    }
}

uint32_t perfCounterRead(uint32_t reg) {
    const auto cycles = static_cast<uint32_t>(nowCycles());
    if (!(cpu.pccr & 0x80000000u)) return reg ? cpu.pcr1 : cpu.pcr0;
    return (reg ? cpu.pcr1 : cpu.pcr0) + cycles;
}

void perfCounterWrite(uint32_t reg, uint32_t value) {
    if (reg) cpu.pcr1 = value;
    else cpu.pcr0 = value;
}

void tlbOp(const char* name) {
    PS2_UNIMPL("cpu", std::string("TLB instruction ") + name, "%s at 0x%08x ignored: the default TLB layout is assumed", name, cpu.pc);
}

bool cpcond0() { return hw::dmacIdle(); }

void integerOverflow(uint32_t value) {
    PS2_UNIMPL("cpu", "integer overflow exception", "signed overflow in ADD/SUB/DADD (result 0x%08x, near pc 0x%08x): destination left unchanged",
               value, cpu.pc);
}

void trap(uint32_t pc, uint32_t code) {
    char key[64];
    std::snprintf(key, sizeof key, "trap instruction (code %u)", code);
    PS2_UNIMPL("cpu", key, "trap condition met at 0x%08x (code %u)%s", pc, code, code == 7 ? ": integer division by zero" : "");
}

void breakpoint(uint32_t pc, uint32_t code) {
    char key[64];
    std::snprintf(key, sizeof key, "BREAK %u", code);
    PS2_UNIMPL("cpu", key, "BREAK %u at 0x%08x%s", code, pc,
               code == 7 ? " (division by zero check)" : code == 6 ? " (overflow check)" : "");
}

void eret(uint32_t pc) {
    PS2_UNIMPL("cpu", "ERET", "ERET at 0x%08x: guest exception handlers are not supported; returning to EPC 0x%08x", pc,
               cpu.cop0[COP0_EPC]);
    cpu.pc = cpu.cop0[COP0_EPC];
}

void applyGuestFloatEnvironment() {
    if (g_opt.fpuRoundToZero) std::fesetround(FE_TOWARDZERO);
}

} // namespace ps2
