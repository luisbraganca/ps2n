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

namespace ps2uc {

#if defined(__GNUC__) || defined(__clang__)
#define PS2UC_PRINTF(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define PS2UC_PRINTF(fmt_idx, args_idx)
#endif

std::string strprintf(const char* fmt, ...) PS2UC_PRINTF(1, 2);
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

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace ps2uc
