// Small string and file helpers shared by the recompiler and its tests.
#pragma once

#include <cstdarg>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ps2n {

#if defined(__GNUC__) || defined(__clang__)
#define PS2N_PRINTF(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define PS2N_PRINTF(fmt_idx, args_idx)
#endif

std::string strprintf(const char* fmt, ...) PS2N_PRINTF(1, 2);
std::string vstrprintf(const char* fmt, va_list ap);

std::string hex32(uint32_t v);            // "0x0012ab40"
std::string_view trim(std::string_view s);
std::vector<std::string> split(std::string_view s, char sep);
std::string toLower(std::string_view s);
std::string toUpper(std::string_view s);
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);

// Accepts decimal, 0x-prefixed hex and an optional leading '-'.
std::optional<int64_t> parseInt(std::string_view s);

std::vector<uint8_t> readFile(const std::filesystem::path& path);
std::string readTextFile(const std::filesystem::path& path);
// Writes only when the content differs, so incremental builds stay incremental.
bool writeFileIfChanged(const std::filesystem::path& path, std::string_view content);

uint32_t crc32(const uint8_t* data, size_t size);

// Fingerprint of a function's instruction words (FNV-1a over 32-bit words).
// The recompiler stores one per function and the runtime compares it with
// RAM, to tell whether that function's code is the one in memory.
struct CodeChecksum {
    uint32_t value = 2166136261u;
    void add(uint32_t word) { value = (value ^ word) * 16777619u; }
};

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace ps2n
