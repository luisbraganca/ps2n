// Guest memory: backing stores, the address map, and the slow paths.
#include "ps2/memory.h"
#include "ps2/ops_vu0.h"

#include "hw/hw.h"
#include "internal.h"
#include "iop/iop.h"
#include "log.h"

#include <cstring>

namespace ps2 {

alignas(4096) uint8_t ram[kRamSize + 64];
alignas(64) uint8_t spr[kSprSize + 64];
alignas(64) uint8_t vu0Mem[4096];
alignas(64) uint8_t vu0Micro[4096];
alignas(64) uint8_t vu1Mem[16384];
alignas(64) uint8_t vu1Micro[16384];

namespace {

constexpr uint32_t kBiosBase = 0x1FC00000u, kBiosSize = 4u << 20;
constexpr uint32_t kRomverOffset = 0x7FF52;   // where "ROMVER" points in retail BIOSes

enum class Region { Ram, Spr, VuMem, Hw, IopRam, Bios, Unmapped };

struct Target {
    Region region;
    uint32_t phys;
};

Target classify(uint32_t addr) {
    if ((addr & ~(kSprSize - 1)) == kSprBase) return {Region::Spr, addr & (kSprSize - 1)};
    uint32_t phys;
    if (addr >= 0x80000000u && addr < 0xC0000000u) {
        phys = addr & 0x1FFFFFFFu;           // kseg0 / kseg1
    } else if ((addr >= 0x20000000u && addr < 0x22000000u) || (addr >= 0x30000000u && addr < 0x32000000u)) {
        return {Region::Ram, addr & (kRamSize - 1)};   // uncached / uncached-accelerated RAM views
    } else if (addr < 0x20000000u) {
        phys = addr;                         // kuseg below 512 MB maps 1:1 under the default TLB setup
    } else {
        return {Region::Unmapped, addr};
    }
    if (phys < 0x10000000u) return {Region::Ram, phys & (kRamSize - 1)};   // RAM and its mirrors
    if ((phys >= 0x10000000u && phys < 0x10010000u) || (phys >= 0x12000000u && phys < 0x12002000u))
        return {Region::Hw, phys};
    if (phys >= 0x11000000u && phys < 0x11010000u) return {Region::VuMem, phys - 0x11000000u};
    if (phys >= 0x1C000000u && phys < 0x1C200000u) return {Region::IopRam, phys - 0x1C000000u};
    if (phys >= kBiosBase && phys < kBiosBase + kBiosSize) return {Region::Bios, phys - kBiosBase};
    return {Region::Unmapped, addr};
}

uint8_t* vuPtr(uint32_t off) {
    // 0x11000000 VU0 micro (4K), 0x11004000 VU0 data (4K), 0x11008000 VU1 micro (16K), 0x1100C000 VU1 data (16K)
    if (off < 0x4000) return vu0Micro + (off & 0xFFF);
    if (off < 0x8000) return vu0Mem + (off & 0xFFF);
    if (off < 0xC000) return vu1Micro + (off & 0x3FFF);
    return vu1Mem + (off & 0x3FFF);
}

uint8_t biosByte(uint32_t off) {
    const std::string_view romver = romVersion();
    if (off >= kRomverOffset && off < kRomverOffset + romver.size()) return static_cast<uint8_t>(romver[off - kRomverOffset]);
    return 0;
}

// Generic plain-memory accessors used by the slow path.
uint8_t* plainPtr(const Target& t) {
    switch (t.region) {
    case Region::Ram: return ram + t.phys;
    case Region::Spr: return spr + t.phys;
    case Region::VuMem: return vuPtr(t.phys);
    case Region::IopRam: return iop::ramPtr(t.phys);
    default: return nullptr;
    }
}

void badAccess(const char* what, uint32_t addr) {
    PS2_UNIMPL("mem", std::string("unmapped ") + what, "%s at unmapped address 0x%08x (pc 0x%08x)", what, addr, cpu.pc);
}

template <typename T>
T readSlow(uint32_t addr) {
    const Target t = classify(addr);
    if (uint8_t* p = plainPtr(t)) {
        T v;
        std::memcpy(&v, p, sizeof(T));
        return v;
    }
    switch (t.region) {
    case Region::Hw:
        if constexpr (sizeof(T) == 1) return hw::read8(t.phys);
        else if constexpr (sizeof(T) == 2) return hw::read16(t.phys);
        else if constexpr (sizeof(T) == 4) return hw::read32(t.phys);
        else return hw::read64(t.phys);
    case Region::Bios: {
        uint64_t v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v |= static_cast<uint64_t>(biosByte(t.phys + static_cast<uint32_t>(i))) << (8 * i);
        return static_cast<T>(v);
    }
    default:
        badAccess("read", addr);
        return 0;
    }
}

template <typename T>
void writeSlow(uint32_t addr, T v) {
    const Target t = classify(addr);
    if (uint8_t* p = plainPtr(t)) {
        std::memcpy(p, &v, sizeof(T));
        return;
    }
    switch (t.region) {
    case Region::Hw:
        if constexpr (sizeof(T) == 1) hw::write8(t.phys, v);
        else if constexpr (sizeof(T) == 2) hw::write16(t.phys, v);
        else if constexpr (sizeof(T) == 4) hw::write32(t.phys, v);
        else hw::write64(t.phys, v);
        return;
    case Region::Bios:
        return;   // ROM
    default:
        badAccess("write", addr);
        return;
    }
}

} // namespace

uint8_t memRead8Slow(uint32_t addr) { return readSlow<uint8_t>(addr); }
uint16_t memRead16Slow(uint32_t addr) { return readSlow<uint16_t>(addr); }
uint32_t memRead32Slow(uint32_t addr) { return readSlow<uint32_t>(addr); }
uint64_t memRead64Slow(uint32_t addr) { return readSlow<uint64_t>(addr); }
void memWrite8Slow(uint32_t addr, uint8_t v) { writeSlow(addr, v); }
void memWrite16Slow(uint32_t addr, uint16_t v) { writeSlow(addr, v); }
void memWrite32Slow(uint32_t addr, uint32_t v) { writeSlow(addr, v); }
void memWrite64Slow(uint32_t addr, uint64_t v) { writeSlow(addr, v); }

void memRead128Slow(uint32_t addr, uint64_t out[2]) {
    const Target t = classify(addr);
    if (uint8_t* p = plainPtr(t)) {
        std::memcpy(out, p, 16);
        return;
    }
    if (t.region == Region::Hw) return hw::read128(t.phys, out);
    out[0] = readSlow<uint64_t>(addr);
    out[1] = readSlow<uint64_t>(addr + 8);
}

void memWrite128Slow(uint32_t addr, const uint64_t v[2]) {
    const Target t = classify(addr);
    if (uint8_t* p = plainPtr(t)) {
        std::memcpy(p, v, 16);
        return;
    }
    if (t.region == Region::Hw) return hw::write128(t.phys, v);
    writeSlow<uint64_t>(addr, v[0]);
    writeSlow<uint64_t>(addr + 8, v[1]);
}

uint32_t toPhysical(uint32_t addr) {
    const Target t = classify(addr);
    switch (t.region) {
    case Region::Ram: return t.phys;
    case Region::Spr: return kSprBase + t.phys;
    case Region::VuMem: return 0x11000000u + t.phys;
    case Region::IopRam: return 0x1C000000u + t.phys;
    case Region::Bios: return kBiosBase + t.phys;
    default: return t.phys;
    }
}

uint8_t* hostPtr(uint32_t addr, uint32_t size) {
    const Target t = classify(addr);
    uint8_t* p = plainPtr(t);
    if (!p || size == 0) return p;
    const Target last = classify(addr + size - 1);
    if (last.region != t.region || plainPtr(last) != p + (size - 1)) return nullptr;   // straddles a boundary
    return p;
}

std::string readCString(uint32_t addr, size_t maxLen) {
    std::string s;
    for (size_t i = 0; i < maxLen; ++i) {
        const char c = static_cast<char>(read8(addr + static_cast<uint32_t>(i)));
        if (!c) break;
        s.push_back(c);
    }
    return s;
}

void readBytes(uint32_t addr, void* dst, uint32_t size) {
    if (uint8_t* p = hostPtr(addr, size)) {
        std::memcpy(dst, p, size);
        return;
    }
    auto* d = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < size; ++i) d[i] = read8(addr + i);
}

void writeBytes(uint32_t addr, const void* src, uint32_t size) {
    if (uint8_t* p = hostPtr(addr, size)) {
        std::memcpy(p, src, size);
        return;
    }
    const auto* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < size; ++i) write8(addr + i, s[i]);
}

void fillBytes(uint32_t addr, uint8_t value, uint32_t size) {
    if (uint8_t* p = hostPtr(addr, size)) {
        std::memset(p, value, size);
        return;
    }
    for (uint32_t i = 0; i < size; ++i) write8(addr + i, value);
}

} // namespace ps2
