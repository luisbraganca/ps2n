#include "log.h"

#include "axiom.h"
#include "internal.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

namespace ps2 {

LogLevel g_logLevel = LogLevel::Info;

namespace {
std::mutex g_logMutex;
std::map<std::string, uint64_t>& issues() {
    static std::map<std::string, uint64_t> m;
    return m;
}
const char* levelName(LogLevel l) {
    switch (l) {
    case LogLevel::Error: return "error";
    case LogLevel::Warn: return "warn";
    case LogLevel::Info: return "info";
    case LogLevel::Debug: return "debug";
    case LogLevel::Trace: return "trace";
    }
    return "?";
}
} // namespace

void logf(LogLevel level, const char* category, const char* fmt, ...) {
    if (level > g_logLevel) return;
    // Formatted once, for stderr and for Axiom.
    char small[512];
    std::string large;
    std::string_view text;
    va_list ap, again;
    va_start(ap, fmt);
    va_copy(again, ap);
    const int n = std::vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n < 0) {
        text = fmt;
    } else if (size_t(n) < sizeof small) {
        text = std::string_view(small, size_t(n));
    } else {
        large.resize(size_t(n) + 1);
        std::vsnprintf(large.data(), large.size(), fmt, again);
        large.resize(size_t(n));
        text = large;
    }
    va_end(again);
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::fprintf(stderr, "[%s:%s] %.*s\n", levelName(level), category, int(text.size()), text.data());
    std::fflush(stderr);
    axiom::onLog(level, category, text);
}

uint64_t noteIssue(const std::string& key, bool gap) {
    uint64_t n = 0;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        n = ++issues()[key];
    }
    if (gap && g_opt.strict && !g_exitRequested) {
        logf(LogLevel::Error, "strict", "stopping at the first unimplemented path: %s", key.c_str());
        requestExit(4);
    }
    return n;
}

std::vector<std::pair<std::string, uint64_t>> issueCounts() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    const auto& m = issues();
    std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
    std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return v;
}

void printIssueSummary() {
    const auto v = issueCounts();
    if (v.empty()) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::fprintf(stderr, "\n---- unimplemented / unexpected paths hit during this run ----\n");
    for (const auto& [key, count] : v) std::fprintf(stderr, "%10llu  %s\n", static_cast<unsigned long long>(count), key.c_str());
    std::fprintf(stderr, "---------------------------------------------------------------\n");
}

} // namespace ps2
