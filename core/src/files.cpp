// SPDX-License-Identifier: GPL-3.0-or-later
// File helpers of the build-time tools. They report failure by throwing
// ps2n::Error.
#include "ps2n/util.h"

#include <fstream>
#include <sstream>

namespace ps2n {

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

} // namespace ps2n
