// Guest memory access.
//
// RDRAM (32 MB at 0x00000000) and the scratchpad (16 KB at 0x70000000) take an
// inline fast path; every other address (kseg0/kseg1 and the uncached
// mirrors, hardware registers, VU and IOP memory, the BIOS window) goes through
// an out-of-line slow path that routes to the right device.
//
// The PS2 is little-endian, like every supported host.
#pragma once

#include "ps2/compiler.h"

#include <cstdint>
#include <cstring>
#include <string>

static_assert(sizeof(void*) == 8, "ps2uc runtime requires a 64-bit host");

namespace ps2 {

constexpr uint32_t kRamSize = 32u << 20;
constexpr uint32_t kSprBase = 0x70000000u;
constexpr uint32_t kSprSize = 16u << 10;

// Backing stores. They are padded at the end so an unaligned access that
// straddles the last byte cannot run off the array.
extern uint8_t ram[];
extern uint8_t spr[];

PS2_COLD uint8_t memRead8Slow(uint32_t addr);
PS2_COLD uint16_t memRead16Slow(uint32_t addr);
PS2_COLD uint32_t memRead32Slow(uint32_t addr);
PS2_COLD uint64_t memRead64Slow(uint32_t addr);
PS2_COLD void memRead128Slow(uint32_t addr, uint64_t out[2]);
PS2_COLD void memWrite8Slow(uint32_t addr, uint8_t v);
PS2_COLD void memWrite16Slow(uint32_t addr, uint16_t v);
PS2_COLD void memWrite32Slow(uint32_t addr, uint32_t v);
PS2_COLD void memWrite64Slow(uint32_t addr, uint64_t v);
PS2_COLD void memWrite128Slow(uint32_t addr, const uint64_t v[2]);

#define PS2_MEM_FAST_RAM(addr, size) ((addr) <= kRamSize - (size))
#define PS2_MEM_FAST_SPR(addr, size) (((addr) & ~(kSprSize - 1)) == kSprBase && ((addr) & (kSprSize - 1)) <= kSprSize - (size))

inline uint8_t read8(uint32_t addr) {
    if (PS2_MEM_FAST_RAM(addr, 1)) [[likely]] return ram[addr];
    if (PS2_MEM_FAST_SPR(addr, 1)) return spr[addr & (kSprSize - 1)];
    return memRead8Slow(addr);
}
inline uint16_t read16(uint32_t addr) {
    uint16_t v;
    if (PS2_MEM_FAST_RAM(addr, 2)) [[likely]] { std::memcpy(&v, ram + addr, 2); return v; }
    if (PS2_MEM_FAST_SPR(addr, 2)) { std::memcpy(&v, spr + (addr & (kSprSize - 1)), 2); return v; }
    return memRead16Slow(addr);
}
inline uint32_t read32(uint32_t addr) {
    uint32_t v;
    if (PS2_MEM_FAST_RAM(addr, 4)) [[likely]] { std::memcpy(&v, ram + addr, 4); return v; }
    if (PS2_MEM_FAST_SPR(addr, 4)) { std::memcpy(&v, spr + (addr & (kSprSize - 1)), 4); return v; }
    return memRead32Slow(addr);
}
inline uint64_t read64(uint32_t addr) {
    uint64_t v;
    if (PS2_MEM_FAST_RAM(addr, 8)) [[likely]] { std::memcpy(&v, ram + addr, 8); return v; }
    if (PS2_MEM_FAST_SPR(addr, 8)) { std::memcpy(&v, spr + (addr & (kSprSize - 1)), 8); return v; }
    return memRead64Slow(addr);
}
// 128-bit accesses ignore the low four address bits, as LQ/SQ do.
inline void read128(uint32_t addr, uint64_t out[2]) {
    addr &= ~15u;
    if (PS2_MEM_FAST_RAM(addr, 16)) [[likely]] { std::memcpy(out, ram + addr, 16); return; }
    if (PS2_MEM_FAST_SPR(addr, 16)) { std::memcpy(out, spr + (addr & (kSprSize - 1)), 16); return; }
    memRead128Slow(addr, out);
}

inline void write8(uint32_t addr, uint8_t v) {
    if (PS2_MEM_FAST_RAM(addr, 1)) [[likely]] { ram[addr] = v; return; }
    if (PS2_MEM_FAST_SPR(addr, 1)) { spr[addr & (kSprSize - 1)] = v; return; }
    memWrite8Slow(addr, v);
}
inline void write16(uint32_t addr, uint16_t v) {
    if (PS2_MEM_FAST_RAM(addr, 2)) [[likely]] { std::memcpy(ram + addr, &v, 2); return; }
    if (PS2_MEM_FAST_SPR(addr, 2)) { std::memcpy(spr + (addr & (kSprSize - 1)), &v, 2); return; }
    memWrite16Slow(addr, v);
}
inline void write32(uint32_t addr, uint32_t v) {
    if (PS2_MEM_FAST_RAM(addr, 4)) [[likely]] { std::memcpy(ram + addr, &v, 4); return; }
    if (PS2_MEM_FAST_SPR(addr, 4)) { std::memcpy(spr + (addr & (kSprSize - 1)), &v, 4); return; }
    memWrite32Slow(addr, v);
}
inline void write64(uint32_t addr, uint64_t v) {
    if (PS2_MEM_FAST_RAM(addr, 8)) [[likely]] { std::memcpy(ram + addr, &v, 8); return; }
    if (PS2_MEM_FAST_SPR(addr, 8)) { std::memcpy(spr + (addr & (kSprSize - 1)), &v, 8); return; }
    memWrite64Slow(addr, v);
}
inline void write128(uint32_t addr, const uint64_t v[2]) {
    addr &= ~15u;
    if (PS2_MEM_FAST_RAM(addr, 16)) [[likely]] { std::memcpy(ram + addr, v, 16); return; }
    if (PS2_MEM_FAST_SPR(addr, 16)) { std::memcpy(spr + (addr & (kSprSize - 1)), v, 16); return; }
    memWrite128Slow(addr, v);
}

#undef PS2_MEM_FAST_RAM
#undef PS2_MEM_FAST_SPR

// Host-side helpers for runtime code (stubs, syscalls, DMA).
// Pointer to `size` contiguous bytes of RAM-like guest memory, or nullptr if
// the range is not plain memory (hardware registers, unmapped, or straddling).
uint8_t* hostPtr(uint32_t addr, uint32_t size);
std::string readCString(uint32_t addr, size_t maxLen = 4096);
void readBytes(uint32_t addr, void* dst, uint32_t size);
void writeBytes(uint32_t addr, const void* src, uint32_t size);
void fillBytes(uint32_t addr, uint8_t value, uint32_t size);

// Physical address of a kseg0/kseg1/uncached-mirror virtual address.
uint32_t toPhysical(uint32_t addr);

} // namespace ps2
