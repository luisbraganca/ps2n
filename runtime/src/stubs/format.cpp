#include "stubs/format.h"

#include "ps2/cpu.h"
#include "ps2/memory.h"

#include <bit>
#include <cfenv>
#include <cstdio>

namespace ps2 {

uint64_t GuestArgs::next() {
    static const uint32_t regsForSlot[8] = {regs::A0, regs::A1, regs::A2, regs::A3, regs::T0, regs::T1, regs::T2, regs::T3};
    const int s = slot_++;
    if (s < 8) return cpu.gpr[regsForSlot[s]].d[0];
    return read64(ru32(regs::SP) + static_cast<uint32_t>(s - 8) * 8);
}

double GuestArgs::nextDouble() { return std::bit_cast<double>(next()); }
std::string GuestArgs::readString(uint32_t addr) { return readCString(addr); }

std::string formatPrintf(const std::string& fmt, PrintfSource& args) {
    std::string out;
    size_t i = 0;
    while (i < fmt.size()) {
        const char c = fmt[i];
        if (c != '%') {
            out.push_back(c);
            ++i;
            continue;
        }
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
            out.push_back('%');
            i += 2;
            continue;
        }
        // Parse one conversion specification.
        std::string spec = "%";
        ++i;
        while (i < fmt.size() && std::string("-+ #0").find(fmt[i]) != std::string::npos) spec.push_back(fmt[i++]);
        auto number = [&](bool allowStar) {
            if (allowStar && i < fmt.size() && fmt[i] == '*') {
                spec += std::to_string(static_cast<int32_t>(args.nextInt(false)));
                ++i;
                return;
            }
            while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') spec.push_back(fmt[i++]);
        };
        number(true);
        if (i < fmt.size() && fmt[i] == '.') {
            spec.push_back(fmt[i++]);
            number(true);
        }
        int longness = 0;   // 0 int, 1 long (32-bit on the EE), 2 long long
        bool half = false;
        while (i < fmt.size() && std::string("hlLqjzt").find(fmt[i]) != std::string::npos) {
            const char m = fmt[i++];
            if (m == 'l') ++longness;
            else if (m == 'q' || m == 'L' || m == 'j') longness = 2;
            else if (m == 'h') half = true;
        }
        if (i >= fmt.size()) break;
        const char conv = fmt[i++];
        char buf[512];
        switch (conv) {
        case 'd':
        case 'i': {
            const uint64_t raw = args.nextInt(longness >= 2);
            spec += "lld";
            long long v = longness >= 2 ? static_cast<long long>(raw) : static_cast<long long>(static_cast<int32_t>(raw));
            if (half) v = static_cast<int16_t>(v);
            std::snprintf(buf, sizeof buf, spec.c_str(), v);
            out += buf;
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            const uint64_t raw = args.nextInt(longness >= 2);
            spec += "ll";
            spec.push_back(conv);
            unsigned long long v = longness >= 2 ? raw : static_cast<uint32_t>(raw);
            if (half) v = static_cast<uint16_t>(v);
            std::snprintf(buf, sizeof buf, spec.c_str(), v);
            out += buf;
            break;
        }
        case 'p': {
            std::snprintf(buf, sizeof buf, "0x%08x", static_cast<uint32_t>(args.nextInt(false)));
            out += buf;
            break;
        }
        case 'c': {
            spec.push_back('c');
            std::snprintf(buf, sizeof buf, spec.c_str(), static_cast<int>(static_cast<unsigned char>(args.nextInt(false))));
            out += buf;
            break;
        }
        case 's': {
            const auto addr = static_cast<uint32_t>(args.nextInt(false));
            const std::string s = addr ? args.readString(addr) : std::string("(null)");
            spec.push_back('s');
            const int n = std::snprintf(nullptr, 0, spec.c_str(), s.c_str());
            std::string tmp(static_cast<size_t>(n > 0 ? n : 0), '\0');
            std::snprintf(tmp.data(), tmp.size() + 1, spec.c_str(), s.c_str());
            out += tmp;
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            const double d = args.nextDouble();
            spec.push_back(conv);
            // The host FPU runs in round-toward-zero like the EE's, and host
            // printf honours that; the guest's libc rounds to nearest.
            const int mode = std::fegetround();
            std::fesetround(FE_TONEAREST);
            std::snprintf(buf, sizeof buf, spec.c_str(), d);
            std::fesetround(mode);
            out += buf;
            break;
        }
        case 'n':
            args.nextInt(false);   // never write through guest-provided pointers from a format string
            break;
        default:
            out += spec;
            out.push_back(conv);
            break;
        }
    }
    return out;
}

std::string formatGuest(const std::string& fmt, GuestArgs& args) { return formatPrintf(fmt, args); }

std::string formatGuestString(uint32_t fmtAddr, int firstArg) {
    GuestArgs args(firstArg);
    return formatGuest(readCString(fmtAddr), args);
}

void hostPrint(const std::string& s) {
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

} // namespace ps2
