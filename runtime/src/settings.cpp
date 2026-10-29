#include "settings.h"

#include "ps2n/util.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace ps2::settings {

namespace {

bool same(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

} // namespace

void Ini::parse(std::string_view text) {
    sections_.clear();
    Section* current = nullptr;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = ps2n::trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            current = &findOrAdd(ps2n::trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos || !current) continue;
        current->entries.push_back({std::string(ps2n::trim(line.substr(0, eq))), std::string(ps2n::trim(line.substr(eq + 1)))});
    }
}

std::string Ini::text() const {
    std::string out;
    // An empty section stays: [Hotkeys] with every hotkey cleared is not the
    // same as no [Hotkeys] (which means the defaults).
    for (const Section& s : sections_) {
        if (!out.empty()) out += '\n';
        out += '[' + s.name + "]\n";
        for (const Entry& e : s.entries) out += e.key + " = " + e.value + '\n';
    }
    return out;
}

bool Ini::load(const std::filesystem::path& path) {
    sections_.clear();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    try {
        parse(ps2n::readTextFile(path));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool Ini::save(const std::filesystem::path& path) const {
    try {
        ps2n::writeFileIfChanged(path, text());
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

const Ini::Section* Ini::find(std::string_view section) const {
    for (const Section& s : sections_)
        if (same(s.name, section)) return &s;
    return nullptr;
}

Ini::Section& Ini::findOrAdd(std::string_view section) {
    for (Section& s : sections_)
        if (same(s.name, section)) return s;
    sections_.push_back({std::string(section), {}});
    return sections_.back();
}

bool Ini::hasSection(std::string_view section) const { return find(section) != nullptr; }

std::vector<std::pair<std::string, std::string>> Ini::entries(std::string_view section) const {
    std::vector<std::pair<std::string, std::string>> out;
    if (const Section* s = find(section))
        for (const Entry& e : s->entries) out.emplace_back(e.key, e.value);
    return out;
}

std::vector<std::string> Ini::values(std::string_view section, std::string_view key) const {
    std::vector<std::string> out;
    if (const Section* s = find(section))
        for (const Entry& e : s->entries)
            if (same(e.key, key)) out.push_back(e.value);
    return out;
}

std::optional<std::string> Ini::value(std::string_view section, std::string_view key) const {
    if (const Section* s = find(section))
        for (const Entry& e : s->entries)
            if (same(e.key, key)) return e.value;
    return std::nullopt;
}

std::string Ini::getString(std::string_view section, std::string_view key, std::string_view fallback) const {
    const auto v = value(section, key);
    return v ? *v : std::string(fallback);
}

float Ini::getFloat(std::string_view section, std::string_view key, float fallback) const {
    const auto v = value(section, key);
    if (!v || v->empty()) return fallback;
    char* end = nullptr;
    const float f = std::strtof(v->c_str(), &end);
    return end && *end == '\0' ? f : fallback;
}

bool Ini::getBool(std::string_view section, std::string_view key, bool fallback) const {
    const auto v = value(section, key);
    if (!v) return fallback;
    const std::string l = ps2n::toLower(*v);
    if (l == "true" || l == "1" || l == "yes" || l == "on") return true;
    if (l == "false" || l == "0" || l == "no" || l == "off") return false;
    return fallback;
}

void Ini::setValues(std::string_view section, std::string_view key, const std::vector<std::string>& values) {
    Section& s = findOrAdd(section);
    // The new lines go where the first old one was, so the file keeps its order.
    size_t at = s.entries.size();
    for (size_t i = 0; i < s.entries.size();) {
        if (same(s.entries[i].key, key)) {
            at = std::min(at, i);
            s.entries.erase(s.entries.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (const std::string& v : values) s.entries.insert(s.entries.begin() + static_cast<std::ptrdiff_t>(at++), {std::string(key), v});
}

void Ini::set(std::string_view section, std::string_view key, std::string_view value) {
    setValues(section, key, {std::string(value)});
}

void Ini::setFloat(std::string_view section, std::string_view key, float value) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(value));
    set(section, key, buf);
}

void Ini::setBool(std::string_view section, std::string_view key, bool value) { set(section, key, value ? "true" : "false"); }

void Ini::removeSection(std::string_view section) {
    for (auto it = sections_.begin(); it != sections_.end(); ++it) {
        if (same(it->name, section)) {
            sections_.erase(it);
            return;
        }
    }
}

std::filesystem::path locate(const std::filesystem::path& exeDir, const std::filesystem::path& override) {
    namespace fs = std::filesystem;
    if (!override.empty()) return override;
    std::error_code ec;
    if (!exeDir.empty() && fs::exists(exeDir / "ps2n.ini", ec)) return exeDir / "ps2n.ini";
#if defined(_WIN32)
    if (const wchar_t* appData = _wgetenv(L"APPDATA"); appData && *appData) return fs::path(appData) / "PS2Native" / "ps2n.ini";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / "Library/Application Support/PS2Native/ps2n.ini";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return fs::path(xdg) / "PS2Native" / "ps2n.ini";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".config/PS2Native/ps2n.ini";
#endif
    return "ps2n.ini";
}

} // namespace ps2::settings
