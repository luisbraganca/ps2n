// Logging for the runtime.
//
// Unimplemented paths report through PS2_UNIMPL / PS2_FALLBACK: the first few
// hits print immediately, every hit is counted, and the counts are printed in
// a summary when the program exits, so a run doubles as a to-do list.
#pragma once

#include "ps2/compiler.h"

#include <cstdint>
#include <string>

namespace ps2 {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3, Trace = 4 };

extern LogLevel g_logLevel;

void logf(LogLevel level, const char* category, const char* fmt, ...) PS2_PRINTF(3, 4);
// Counts one occurrence of `key` for the exit summary and returns the number of
// times it has been seen, including this one. A `gap` (emulation that is
// missing or wrong) stops the run under --strict.
uint64_t noteIssue(const std::string& key, bool gap = true);
void printIssueSummary();

} // namespace ps2

#define PS2_ERROR(cat, ...) ::ps2::logf(::ps2::LogLevel::Error, cat, __VA_ARGS__)
#define PS2_WARN(cat, ...) ::ps2::logf(::ps2::LogLevel::Warn, cat, __VA_ARGS__)
#define PS2_INFO(cat, ...) ::ps2::logf(::ps2::LogLevel::Info, cat, __VA_ARGS__)
#define PS2_DEBUG(cat, ...)                                                    \
    do {                                                                       \
        if (::ps2::g_logLevel >= ::ps2::LogLevel::Debug)                       \
            ::ps2::logf(::ps2::LogLevel::Debug, cat, __VA_ARGS__);             \
    } while (0)
#define PS2_TRACE(cat, ...)                                                    \
    do {                                                                       \
        if (::ps2::g_logLevel >= ::ps2::LogLevel::Trace)                       \
            ::ps2::logf(::ps2::LogLevel::Trace, cat, __VA_ARGS__);             \
    } while (0)

// An emulation gap: records `key` for the summary, prints the warning the
// first 3 times, and stops the run under --strict.
#define PS2_UNIMPL(cat, key, ...)                                              \
    do {                                                                       \
        if (::ps2::noteIssue(key) <= 3)                                        \
            ::ps2::logf(::ps2::LogLevel::Warn, cat, __VA_ARGS__);              \
    } while (0)

// A path that still runs correctly but is worth knowing about (interpreted
// code, a function skipped by the config): reported like PS2_UNIMPL, never
// stops the run.
#define PS2_FALLBACK(cat, key, ...)                                            \
    do {                                                                       \
        if (::ps2::noteIssue(key, false) <= 3)                                 \
            ::ps2::logf(::ps2::LogLevel::Warn, cat, __VA_ARGS__);              \
    } while (0)
