#include "ps2n/util.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ps2n {

std::string vstrprintf(const char* fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    const int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    std::vsnprintf(out.data(), out.size() + 1, fmt, ap);
    return out;
}

std::string strprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string s = vstrprintf(fmt, ap);
    va_end(ap);
    return s;
}

std::string hex32(uint32_t v) { return strprintf("0x%08x", v); }

std::string_view trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::string toLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string toUpper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::optional<int64_t> parseInt(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    bool neg = false;
    if (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        s.remove_prefix(1);
    }
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s.remove_prefix(2);
    }
    if (s.empty()) return std::nullopt;
    uint64_t v = 0;
    for (char c : s) {
        if (c == '_') continue;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return std::nullopt;
        if (d >= base) return std::nullopt;
        v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
    }
    const auto sv = static_cast<int64_t>(v);
    return neg ? -sv : sv;
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("cannot open " + path.string());
    f.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    if (size && !f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)))
        throw Error("cannot read " + path.string());
    return data;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("cannot open " + path.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFileIfChanged(const std::filesystem::path& path, std::string_view content) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        if (ss.str() == content) return false;
    }
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw Error("cannot write " + path.string());
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) throw Error("cannot write " + path.string());
    return true;
}

uint32_t crc32(const uint8_t* data, size_t size) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

} // namespace ps2n
