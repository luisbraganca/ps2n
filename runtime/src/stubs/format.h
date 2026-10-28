// printf-style formatting of guest arguments, for the EE and the IOP.
//
// EE code passes the first eight integer arguments in $a0-$a3, $t0-$t3 (one
// 64-bit register each, also for variadic arguments) and the rest on the
// stack in 8-byte slots starting at $sp. Floats passed to variadic functions
// are promoted to double, which the EE has no hardware for, so they arrive as
// 64-bit soft-float values in integer registers.
//
// IOP code uses the o32 convention: 32-bit slots, the first four in $a0-$a3,
// the rest at 16($sp) onward; 64-bit values take an aligned pair of slots.
#pragma once

#include <cstdint>
#include <string>

namespace ps2 {

// Where the printf engine takes its arguments and strings from.
class PrintfSource {
public:
    virtual ~PrintfSource() = default;
    virtual uint64_t nextInt(bool wide) = 0;    // the next integer argument; `wide` for long long
    virtual double nextDouble() = 0;
    virtual std::string readString(uint32_t addr) = 0;
};

std::string formatPrintf(const std::string& fmt, PrintfSource& args);

// EE arguments, starting at argument slot `firstSlot`.
class GuestArgs final : public PrintfSource {
public:
    explicit GuestArgs(int firstSlot) : slot_(firstSlot) {}
    uint64_t next();   // next raw 64-bit argument slot
    uint32_t next32() { return static_cast<uint32_t>(next()); }
    uint64_t nextInt(bool) override { return next(); }
    double nextDouble() override;
    std::string readString(uint32_t addr) override;

private:
    int slot_;
};

// Formats `fmt` (host string) with EE arguments read from `args`.
std::string formatGuest(const std::string& fmt, GuestArgs& args);
// Reads the format string from guest memory; variadic arguments start at slot `firstArg`.
std::string formatGuestString(uint32_t fmtAddr, int firstArg);
// Writes guest output to the host's stdout, flushed (so it interleaves with logs).
void hostPrint(const std::string& s);

} // namespace ps2
