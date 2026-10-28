// The IOP's CPU: an R3000A-compatible MIPS I core at 36.864 MHz, interpreted.
//
// IOP code (IRX modules) runs here; the IOP kernel does not. Calls into
// kernel libraries (thbase, sifcmd, sysclib, ...) are linked to trap
// addresses at kHleBase, and reaching one runs a host function instead of
// fetching an instruction. Interrupt handlers and other callbacks the kernel
// invokes run as nested calls (callGuest).
#pragma once

#include <cstdint>
#include <string>

namespace ps2::iop {

constexpr uint64_t kIopClockHz = 36864000;   // the EE clock divided by 8
constexpr uint32_t kEeCyclesPerIopCycle = 8;

// Trap addresses: [kHleBase, kHleBase + kHleSize), 8 bytes per entry. They
// sit in kuseg below 256 MB so `j` from module code reaches them, and outside
// anything the IOP maps.
constexpr uint32_t kHleBase = 0x0FF00000u;
constexpr uint32_t kHleSize = 0x00100000u;

struct Cpu {
    uint32_t gpr[32] = {};
    uint32_t hi = 0, lo = 0;
    uint32_t pc = 0;     // instruction being executed next
    uint32_t npc = 4;    // the one after it (a branch target while pc is a delay slot)
    uint32_t sr = 0;     // COP0 Status
    uint32_t cause = 0, epc = 0;
};

enum Reg : uint32_t {
    R_ZERO = 0, R_AT = 1, R_V0 = 2, R_V1 = 3, R_A0 = 4, R_A1 = 5, R_A2 = 6, R_A3 = 7,
    R_T0 = 8, R_S0 = 16, R_T8 = 24, R_T9 = 25, R_K0 = 26, R_K1 = 27, R_GP = 28, R_SP = 29, R_FP = 30, R_RA = 31,
};

// The registers of whatever is running: the current thread, or a nested call.
extern Cpu g_cpu;

// IOP physical memory and its kseg0/kseg1 mirrors.
namespace mem {
constexpr uint32_t kRamSize = 2u << 20;
uint8_t* ram();
uint8_t read8(uint32_t addr);
uint16_t read16(uint32_t addr);
uint32_t read32(uint32_t addr);
void write8(uint32_t addr, uint8_t v);
void write16(uint32_t addr, uint16_t v);
void write32(uint32_t addr, uint32_t v);
bool inRam(uint32_t addr, uint32_t size);   // whole range is plain RAM
std::string readString(uint32_t addr, size_t max = 1024);
void readBlock(uint32_t addr, void* dst, uint32_t size);
void writeBlock(uint32_t addr, const void* src, uint32_t size);
void fill(uint32_t addr, uint8_t value, uint32_t size);
} // namespace mem

using HleFn = void (*)();

// A trap address that runs `fn` when execution reaches it. `fn` sees the
// caller's registers in g_cpu; g_cpu.pc already points at the return address.
uint32_t addTrap(HleFn fn, const std::string& name);
const std::string& trapName(uint32_t addr);
uint32_t currentTrap();   // the trap address whose function is running

// Runs the current context for up to `budget` instructions; stops early when
// the context blocks or a reschedule is pending. Returns instructions run.
int64_t execute(int64_t budget);

// Calls guest code to completion on the current host stack: interrupt and
// alarm handlers, callbacks. Saves and restores g_cpu. `sp` is the stack to
// run on (0 keeps the current one). Returns $v0.
uint32_t callGuest(uint32_t addr, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t gp = 0,
                   uint32_t sp = 0);
int nestingDepth();   // > 0 while callGuest is running

// Total instructions executed (the IOP's notion of time while it is busy).
uint64_t instructionCount();

void initCpu();

} // namespace ps2::iop
