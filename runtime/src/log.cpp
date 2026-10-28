#include "log.h"

#include "internal.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
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
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::fprintf(stderr, "[%s:%s] ", levelName(level), category);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
    std::fflush(stderr);
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

void printIssueSummary() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    const auto& m = issues();
    if (m.empty()) return;
    std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::fprintf(stderr, "\n---- unimplemented / unexpected paths hit during this run ----\n");
    for (const auto& [key, count] : v) std::fprintf(stderr, "%10llu  %s\n", static_cast<unsigned long long>(count), key.c_str());
    std::fprintf(stderr, "---------------------------------------------------------------\n");
}

} // namespace ps2
