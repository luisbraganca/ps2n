#include "function_map.h"

#include "ps2uc/util.h"

#include <sstream>

namespace ps2uc {

namespace {

// Splits one CSV line, honouring double quotes.
std::vector<std::string> csvFields(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                cur.push_back('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (c == ',' && !quoted) {
            out.emplace_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.emplace_back(trim(cur));
    return out;
}

std::optional<uint32_t> parseAddress(std::string s) {
    s = std::string(trim(s));
    if (auto colon = s.rfind(':'); colon != std::string::npos) s = s.substr(colon + 1);
    if (s.empty()) return std::nullopt;
    if (!startsWith(s, "0x") && !startsWith(s, "0X")) s = "0x" + s;
    auto v = parseInt(s);
    if (!v || *v < 0 || *v > 0xFFFFFFFFLL) return std::nullopt;
    return static_cast<uint32_t>(*v);
}

std::optional<uint32_t> parseSize(const std::string& s) {
    auto v = parseInt(s);
    if (!v || *v < 0 || *v > 0xFFFFFFFFLL) return std::nullopt;
    return static_cast<uint32_t>(*v);
}

} // namespace

std::vector<MapEntry> loadFunctionMap(const std::filesystem::path& file) {
    const std::string text = readTextFile(file);
    std::istringstream in(text);
    std::string line;
    int nameCol = -1, startCol = -1, endCol = -1, sizeCol = -1, typeCol = -1;
    bool haveHeader = false;
    std::vector<MapEntry> out;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t(trim(line));
        if (t.empty() || t[0] == '#') continue;
        const auto fields = csvFields(t);
        if (!haveHeader) {
            for (size_t i = 0; i < fields.size(); ++i) {
                const std::string h = toLower(fields[i]);
                if (h == "name" || h == "function" || h == "symbol" || h == "function name") nameCol = static_cast<int>(i);
                else if (h == "start" || h == "address" || h == "addr" || h == "location" || h == "entry" || h == "entry point")
                    startCol = static_cast<int>(i);
                else if (h == "end") endCol = static_cast<int>(i);
                else if (h == "size" || h == "function size" || h == "length" || h == "bytes") sizeCol = static_cast<int>(i);
                else if (h == "type") typeCol = static_cast<int>(i);
            }
            if (startCol < 0) throw Error(file.string() + ": cannot find a start/address/location column in the CSV header");
            haveHeader = true;
            continue;
        }
        auto field = [&](int col) -> std::string { return col >= 0 && col < static_cast<int>(fields.size()) ? fields[static_cast<size_t>(col)] : ""; };
        if (typeCol >= 0 && toLower(field(typeCol)) != "function") continue;
        const auto start = parseAddress(field(startCol));
        if (!start) throw Error(file.string() + ":" + std::to_string(lineNo) + ": bad address '" + field(startCol) + "'");
        MapEntry e;
        e.start = *start;
        e.name = field(nameCol);
        if (endCol >= 0) {
            if (auto end = parseAddress(field(endCol)); end && *end > e.start) e.end = *end;
        }
        if (!e.end && sizeCol >= 0) {
            if (auto size = parseSize(field(sizeCol)); size && *size > 0) e.end = e.start + *size;
        }
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<uint32_t> loadEntriesFile(const std::filesystem::path& file) {
    // Accepts the runtime's "entries = [ 0x..., ]" format; any hex literal on a
    // non-comment line counts, which keeps hand-edited lists working.
    std::vector<uint32_t> out;
    std::istringstream in(readTextFile(file));
    std::string line;
    while (std::getline(in, line)) {
        if (auto hash = line.find('#'); hash != std::string::npos) line = line.substr(0, hash);
        size_t pos = 0;
        while ((pos = line.find("0x", pos)) != std::string::npos) {
            size_t end = pos + 2;
            while (end < line.size() && std::isxdigit(static_cast<unsigned char>(line[end]))) ++end;
            if (auto v = parseInt(line.substr(pos, end - pos)); v && *v >= 0 && *v <= 0xFFFFFFFFLL) out.push_back(static_cast<uint32_t>(*v));
            pos = end;
        }
    }
    return out;
}

} // namespace ps2uc
