// The runtime's settings file, ps2n.ini: controller bindings, their tuning
// and hotkeys. It uses the INI layout PCSX2 does ([Pad1], [Hotkeys], ...;
// see docs/input.md), so a PCSX2 user's sections carry over.
//
// Where it lives: --settings FILE; else ps2n.ini next to the executable when
// there is one (a portable set-up); else the per-user configuration folder
// (%APPDATA%\PS2Native on Windows, ~/Library/Application Support/PS2Native
// on macOS, $XDG_CONFIG_HOME/PS2Native or ~/.config/PS2Native elsewhere),
// shared by every recompiled game.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ps2::settings {

// An INI file: sections in order, each a list of `key = value` lines. A key
// may appear more than once: PCSX2 stores several bindings for one input
// that way. Section and key names compare without regard to case.
class Ini {
public:
    void parse(std::string_view text);
    std::string text() const;
    bool load(const std::filesystem::path& path);   // false (and empty) when it cannot be read
    bool save(const std::filesystem::path& path) const;

    bool hasSection(std::string_view section) const;
    std::vector<std::pair<std::string, std::string>> entries(std::string_view section) const;   // in file order
    std::vector<std::string> values(std::string_view section, std::string_view key) const;
    std::optional<std::string> value(std::string_view section, std::string_view key) const;   // the first
    std::string getString(std::string_view section, std::string_view key, std::string_view fallback) const;
    float getFloat(std::string_view section, std::string_view key, float fallback) const;
    bool getBool(std::string_view section, std::string_view key, bool fallback) const;

    // Replace every line of `key` (none: remove the key).
    void setValues(std::string_view section, std::string_view key, const std::vector<std::string>& values);
    void set(std::string_view section, std::string_view key, std::string_view value);
    void setFloat(std::string_view section, std::string_view key, float value);
    void setBool(std::string_view section, std::string_view key, bool value);
    void removeSection(std::string_view section);

private:
    struct Entry {
        std::string key, value;
    };
    struct Section {
        std::string name;
        std::vector<Entry> entries;
    };
    const Section* find(std::string_view section) const;
    Section& findOrAdd(std::string_view section);
    std::vector<Section> sections_;
};

// The file this run reads and writes (see the top of this file).
std::filesystem::path locate(const std::filesystem::path& exeDir, const std::filesystem::path& override);

} // namespace ps2::settings
