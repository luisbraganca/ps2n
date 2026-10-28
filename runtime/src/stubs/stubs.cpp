// Host implementations of guest functions (see ps2/stub_list.inc).
//
// A stub runs in place of the guest function: arguments come from
// $a0-$a3/$t0-$t3, the result goes to $v0, and the generated wrapper has
// already set cpu.pc to the return address.
#include "iop/iop.h"
#include "log.h"
#include "ps2/recomp.h"
#include "stubs/format.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace ps2::stub {

namespace {
using namespace regs;
uint32_t a(int i) {
    static const uint32_t r[8] = {A0, A1, A2, A3, T0, T1, T2, T3};
    return ru32(r[i]);
}
void ret(int64_t v) { cpu.gpr[V0].d[0] = static_cast<uint64_t>(v); }
void retU32(uint32_t v) { setU32(V0, v); }

uint32_t guestStrlen(uint32_t s) {
    uint32_t n = 0;
    while (read8(s + n)) ++n;
    return n;
}
} // namespace

void ret0_() { ret(0); }
void ret1_() { ret(1); }
void retm1_() { ret(-1); }
void nop_() {}

void printf_() {
    const std::string s = formatGuestString(a(0), 1);
    hostPrint(s);
    ret(static_cast<int64_t>(s.size()));
}
void scePrintf_() { printf_(); }
void puts_() {
    hostPrint(readCString(a(0)) + "\n");
    ret(1);
}
void putchar_() {
    hostPrint(std::string(1, static_cast<char>(a(0))));
    ret(static_cast<int32_t>(a(0) & 0xFF));
}
void sprintf_() {
    const std::string s = formatGuestString(a(1), 2);
    writeBytes(a(0), s.c_str(), static_cast<uint32_t>(s.size() + 1));
    ret(static_cast<int64_t>(s.size()));
}
void snprintf_() {
    const std::string s = formatGuestString(a(2), 3);
    const uint32_t n = a(1);
    if (n) {
        const uint32_t copy = std::min<uint32_t>(n - 1, static_cast<uint32_t>(s.size()));
        writeBytes(a(0), s.data(), copy);
        write8(a(0) + copy, 0);
    }
    ret(static_cast<int64_t>(s.size()));
}

void memcpy_() {
    const uint32_t dst = a(0), src = a(1), n = a(2);
    std::vector<uint8_t> tmp(n);
    readBytes(src, tmp.data(), n);
    writeBytes(dst, tmp.data(), n);
    retU32(dst);
}
void memmove_() { memcpy_(); }   // the temporary copy makes overlap safe
void memset_() {
    fillBytes(a(0), static_cast<uint8_t>(a(1)), a(2));
    retU32(a(0));
}
void memcmp_() {
    const uint32_t x = a(0), y = a(1), n = a(2);
    for (uint32_t i = 0; i < n; ++i) {
        const int d = static_cast<int>(read8(x + i)) - static_cast<int>(read8(y + i));
        if (d) return ret(d);
    }
    ret(0);
}
void strlen_() { retU32(guestStrlen(a(0))); }
void strcpy_() {
    const uint32_t dst = a(0), src = a(1);
    uint32_t i = 0;
    for (;; ++i) {
        const uint8_t c = read8(src + i);
        write8(dst + i, c);
        if (!c) break;
    }
    retU32(dst);
}
void strncpy_() {
    const uint32_t dst = a(0), src = a(1), n = a(2);
    uint32_t i = 0;
    for (; i < n; ++i) {
        const uint8_t c = read8(src + i);
        write8(dst + i, c);
        if (!c) break;
    }
    for (; i < n; ++i) write8(dst + i, 0);
    retU32(dst);
}
void strcmp_() {
    const uint32_t x = a(0), y = a(1);
    for (uint32_t i = 0;; ++i) {
        const uint8_t c1 = read8(x + i), c2 = read8(y + i);
        if (c1 != c2 || !c1) return ret(static_cast<int>(c1) - static_cast<int>(c2));
    }
}
void strncmp_() {
    const uint32_t x = a(0), y = a(1), n = a(2);
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t c1 = read8(x + i), c2 = read8(y + i);
        if (c1 != c2 || !c1) return ret(static_cast<int>(c1) - static_cast<int>(c2));
    }
    ret(0);
}
void strcat_() {
    const uint32_t dst = a(0), src = a(1);
    uint32_t d = dst + guestStrlen(dst);
    for (uint32_t i = 0;; ++i) {
        const uint8_t c = read8(src + i);
        write8(d + i, c);
        if (!c) break;
    }
    retU32(dst);
}
void strchr_() {
    const uint32_t s = a(0);
    const auto c = static_cast<uint8_t>(a(1));
    for (uint32_t i = 0;; ++i) {
        const uint8_t ch = read8(s + i);
        if (ch == c) return retU32(s + i);
        if (!ch) return ret(0);
    }
}
void strrchr_() {
    const uint32_t s = a(0);
    const auto c = static_cast<uint8_t>(a(1));
    uint32_t found = 0;
    for (uint32_t i = 0;; ++i) {
        const uint8_t ch = read8(s + i);
        if (ch == c) found = s + i;
        if (!ch) break;
    }
    retU32(found);
}

void sceSifLoadModule_() {
    const std::string path = readCString(a(0));
    const uint32_t argLen = std::min<uint32_t>(a(1), 4096);
    std::string args(argLen, '\0');   // NUL-separated, so not a C string
    if (argLen) readBytes(a(2), args.data(), argLen);
    ret(iop::loadModuleSync(path, args));
}
void SifLoadModule_() { sceSifLoadModule_(); }

} // namespace ps2::stub
