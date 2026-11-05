#include "axiom.h"

#include "hw/hw.h"
#include "ps2/runtime.h"
#include "ps2n/util.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma comment(lib, "advapi32")
#else
#include <cerrno>
#include <spawn.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace ps2::axiom {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

std::chrono::milliseconds g_sendEvery{10000};    // between requests
std::chrono::milliseconds g_speedEvery{10000};   // between vblank rates
constexpr auto kRetryAfter = std::chrono::seconds(30);
constexpr size_t kMaxQueued = 5000;       // events waiting to go; more are dropped and counted
constexpr uint64_t kMaxLines = 20000;     // log lines a run sends
constexpr size_t kMaxMessage = 4000;      // bytes of one message

int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool writeFile(const fs::path& path, std::string_view text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

std::string readFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::string firstLine(std::string_view s) {
    const size_t end = s.find_first_of("\r\n");
    return std::string(s.substr(0, end));
}

// The value of `"key":` in a flat JSON object: a number or a string ("" when absent).
std::string jsonField(std::string_view json, std::string_view key) {
    const std::string pattern = "\"" + std::string(key) + "\":";
    size_t at = json.find(pattern);
    if (at == std::string_view::npos) return {};
    at += pattern.size();
    while (at < json.size() && json[at] == ' ') ++at;
    std::string out;
    if (at < json.size() && json[at] == '"') {
        for (size_t i = at + 1; i < json.size() && json[i] != '"'; ++i) {
            if (json[i] == '\\' && i + 1 < json.size()) ++i;
            out += json[i];
        }
        return out;
    }
    while (at < json.size() && json[at] >= '0' && json[at] <= '9') out += json[at++];
    return out;
}

// Letters, digits and `extra`, at least one.
bool plain(std::string_view s, std::string_view extra) {
    if (s.empty()) return false;
    for (const char c : s) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && extra.find(c) == std::string_view::npos) return false;
    }
    return true;
}

// ---- what the start event says about the host ----

std::string homeFolder() {
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return home ? home : "";
}

std::string osName() {
#if defined(_WIN32)
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto get = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (get && get(&v) == 0)
            return "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "." +
                   std::to_string(v.dwBuildNumber);
    }
    return "Windows";
#else
    std::string name;
    std::ifstream release("/etc/os-release");
    for (std::string line; std::getline(release, line);) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        name = line.substr(12);
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
    }
    utsname u{};
    if (uname(&u) == 0) {
        const std::string kernel = std::string(u.sysname) + " " + u.release + " " + u.machine;
        name = name.empty() ? kernel : name + "; " + kernel;
    }
    return name;
#endif
}

std::string cpuName() {
#if defined(_WIN32)
    wchar_t buf[128] = {};
    DWORD size = sizeof buf;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? size_t(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), n, nullptr, nullptr);
    if (!out.empty()) out.pop_back();   // the terminator
    return std::string(ps2n::trim(out));
#else
    std::ifstream info("/proc/cpuinfo");
    for (std::string line; std::getline(info, line);) {
        if (line.rfind("model name", 0) != 0) continue;
        const size_t colon = line.find(':');
        if (colon != std::string::npos) return std::string(ps2n::trim(std::string_view(line).substr(colon + 1)));
    }
    return {};
#endif
}

// X11 or Wayland on Linux (SDL behaves differently on each); empty elsewhere.
std::string displayKind() {
#if defined(_WIN32) || defined(__APPLE__)
    return {};
#else
    const char* kind = std::getenv("XDG_SESSION_TYPE");
    return kind ? kind : "";
#endif
}

std::string newSessionId() {
    std::random_device rd;
    const uint64_t v = ((uint64_t(rd()) << 32) | rd()) ^ uint64_t(nowMillis());
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

// ---- running curl ----

#if defined(_WIN32)
std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}
#endif

// Runs curl with `args` and waits for it (its --max-time bounds the wait).
// False, with `error`, when it cannot start.
bool runCurl(const std::vector<std::string>& args, int& exitCode, std::string& error) {
#if defined(_WIN32)
    wchar_t system[MAX_PATH];
    const UINT n = GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring exe = std::wstring(system, n) + L"\\curl.exe";
    if (n == 0 || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = "curl.exe is not in the system folder (Windows has it since version 1803)";
        return false;
    }
    std::string line = quoteForWindows("curl.exe");
    for (const std::string& a : args) line += " " + quoteForWindows(a);
    std::wstring wide = widen(line);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), wide.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        error = "cannot start curl.exe (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    if (WaitForSingleObject(pi.hProcess, 60000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    exitCode = int(code);
    return true;
#else
    std::vector<std::string> all = {"curl"};
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : all) argv.push_back(a.data());
    argv.push_back(nullptr);
    // A process group of its own: Ctrl+C in the terminal stops the game, which
    // then sends what is left, not the request already on its way.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, static_cast<short>(POSIX_SPAWN_SETPGROUP));
    posix_spawnattr_setpgroup(&attr, 0);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, "curl", nullptr, &attr, argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        error = rc == ENOENT ? "the curl program is not installed" : std::string("cannot start curl: ") + std::strerror(rc);
        return false;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            error = std::string("cannot wait for curl: ") + std::strerror(errno);
            return false;
        }
    }
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (exitCode == 127) {   // the shell's "not found", from a C library that execs after forking
        error = "the curl program is not installed";
        return false;
    }
    return true;
#endif
}

// The default transport: curl, with its files in `dir`.
Delivery sendWithCurl(const Config& cfg, const fs::path& dir, const std::string& body) {
    const fs::path bodyFile = dir / "batch.ndjson", response = dir / "response.json", curlLog = dir / "curl.log";
    std::error_code ec;
    fs::remove(response, ec);
    fs::remove(curlLog, ec);
    if (!writeFile(bodyFile, body)) return {Delivery::Outcome::Retry, "cannot write " + utf8(bodyFile)};
    int code = 0;
    std::string error;
    if (!runCurl(curlArguments(cfg, utf8(dir / "headers.txt"), utf8(bodyFile), utf8(response), utf8(curlLog)), code, error))
        return {Delivery::Outcome::GiveUp, error};
    const std::string log = readFile(curlLog);
    if (code != 0) return {Delivery::Outcome::Retry, log.empty() ? "curl exited with " + std::to_string(code) : firstLine(log)};
    int status = 0;
    if (const size_t at = log.rfind("HTTP "); at != std::string::npos) status = std::atoi(log.c_str() + at + 5);
    return judge(status, readFile(response));
}

// ---- events ----

// One event: a flat JSON object. Every event has its time, the session, its
// kind and the game.
class Event {
public:
    Event(std::string_view session, std::string_view game, std::string_view kind, int64_t millis = nowMillis()) {
        text_ = "{\"_time\":" + jsonString(isoTime(millis));
        str("session", session).str("kind", kind).str("game", game);
    }
    Event& str(std::string_view key, std::string_view value) {
        text_ += ",\"";
        text_ += key;
        text_ += "\":";
        text_ += jsonString(value);
        return *this;
    }
    Event& num(std::string_view key, int64_t value) {
        text_ += ",\"";
        text_ += key;
        text_ += "\":";
        text_ += std::to_string(value);
        return *this;
    }
    Event& real(std::string_view key, double value) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f", value);
        text_ += ",\"";
        text_ += key;
        text_ += "\":";
        text_ += buf;
        return *this;
    }
    std::string done() { return text_ + "}"; }

private:
    std::string text_;
};

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

// ---- the session ----

std::mutex g_mutex;   // the session and everything in it; taken after the log's lock, never before
std::atomic<bool> g_active{false};
RendererInfo g_renderer{"software", 1, ""};   // describeRenderer, before start

struct Session {
    Config cfg;
    Transport transport;
    std::string id, game, home;
    fs::path dir;   // curl's files, removed at the end
    Clock::time_point started = Clock::now();
    std::thread thread;
    std::condition_variable wake;

    // Under g_mutex.
    std::deque<std::string> queue;
    bool soon = false;       // a warning: send within a second
    bool stopping = false;   // finish(): send what is left, then end
    uint64_t lines = 0, dropped = 0;

    Event event(std::string_view kind) const { return Event(id, game, kind); }

    void push(std::string e) {
        if (queue.size() >= kMaxQueued) {
            ++dropped;
            return;
        }
        queue.push_back(std::move(e));
    }

    void run();
};

Session* g_session = nullptr;

// The sender: a batch every g_sendEvery, sooner after a warning, never
// sooner than kRetryAfter after a failed request, and the rest when the run
// ends. It logs without the session's lock (the log's lock comes first).
void Session::run() {
    uint64_t sent = 0, requests = 0, partial = 0;
    bool failing = false, gaveUp = false;
    const Clock::time_point begin = Clock::now();
    Clock::time_point nextSend = begin, retryAt = begin, nextSpeed = begin + g_speedEvery, speedTime = begin;
    uint64_t speedVsync = hw::vsyncCount();
    std::unique_lock<std::mutex> lock(g_mutex);
    for (;;) {
        const Clock::time_point now = Clock::now();
        if (!stopping && now >= nextSpeed) {
            const uint64_t v = hw::vsyncCount();
            const double seconds = std::chrono::duration<double>(now - speedTime).count();
            push(event("speed")
                     .num("vsync", int64_t(v))
                     .num("vblanks", int64_t(v - speedVsync))
                     .real("seconds", seconds)
                     .real("vblanks_per_second", seconds > 0 ? double(v - speedVsync) / seconds : 0.0)
                     .done());
            speedVsync = v;
            speedTime = now;
            nextSpeed = now + g_speedEvery;
        }
        const Clock::time_point sendAt = std::max(soon ? now : nextSend, retryAt);
        if (!stopping && now < sendAt) {
            wake.wait_until(lock, std::min(sendAt, nextSpeed));   // onLog's warnings and finish() wake it sooner
            continue;
        }
        const bool last = stopping;
        soon = false;
        nextSend = now + g_sendEvery;
        if (gaveUp) {
            dropped += queue.size();
            queue.clear();
        }
        if (!queue.empty()) {
            std::deque<std::string> batch;
            batch.swap(queue);
            std::string body;
            for (const std::string& e : batch) {
                body += e;
                body += '\n';
            }
            lock.unlock();
            const Delivery d = transport(body);
            ++requests;
            switch (d.outcome) {
            case Delivery::Outcome::Sent:
                sent += batch.size();
                if (!d.message.empty() && ++partial == 1) PS2_WARN("axiom", "Axiom refused part of the log: %s", d.message.c_str());
                if (failing) PS2_INFO("axiom", "the log reaches Axiom again");
                failing = false;
                break;
            case Delivery::Outcome::Retry:
                if (!failing && !last)
                    PS2_WARN("axiom", "cannot send the log to Axiom (%s); trying again every %lld s", d.message.c_str(),
                             static_cast<long long>(kRetryAfter.count()));
                failing = true;
                break;
            case Delivery::Outcome::GiveUp:
                PS2_WARN("axiom", "Axiom does not take the log (%s); nothing more is sent", d.message.c_str());
                gaveUp = true;
                break;
            }
            lock.lock();
            if (d.outcome == Delivery::Outcome::Retry && !last) {
                // Back in front of what came since, as far as there is room.
                while (!batch.empty() && queue.size() < kMaxQueued) {
                    queue.push_front(std::move(batch.back()));
                    batch.pop_back();
                }
                dropped += batch.size();
                retryAt = now + kRetryAfter;
            } else if (d.outcome != Delivery::Outcome::Sent) {
                dropped += batch.size();
            }
        }
        if (last) break;
    }
    lock.unlock();
    if (failing) PS2_WARN("axiom", "the end of the log did not reach Axiom");
    PS2_INFO("axiom", "sent %llu event(s) to Axiom in %llu request(s)", static_cast<unsigned long long>(sent),
             static_cast<unsigned long long>(requests));
}

} // namespace

// ---- the pieces ----

Config readConfig(const settings::Ini& ini) {
    Config c;
    if (!ini.hasSection("Axiom")) return c;
    c.token = std::string(ps2n::trim(ini.getString("Axiom", "Token", "")));
    c.dataset = std::string(ps2n::trim(ini.getString("Axiom", "Dataset", "")));
    c.domain = std::string(ps2n::trim(ini.getString("Axiom", "Domain", "")));
    if (!ini.getBool("Axiom", "Enabled", true)) return c;
    if (c.token.empty() || c.dataset.empty() || c.domain.empty()) {
        c.problem = "the settings file's [Axiom] section needs Token, Dataset and Domain; the log stays here";
        return c;
    }
    // What goes into a header, a path and a host name.
    if (!plain(c.token, "-_.")) c.problem = "[Axiom] Token is not an API token; the log stays here";
    else if (!plain(c.dataset, "-_.")) c.problem = "[Axiom] Dataset is not a dataset name; the log stays here";
    else if (!plain(c.domain, "-.")) c.problem = "[Axiom] Domain is not a host name (eu-central-1.aws.edge.axiom.co); the log stays here";
    c.enabled = c.problem.empty();
    return c;
}

std::string jsonString(std::string_view s) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out = "\"";
    out.reserve(s.size() + 2);
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 15];
                } else {
                    out += char(c);
                }
            }
            ++i;
            continue;
        }
        // A UTF-8 sequence goes as it is; anything else is one U+FFFD per byte.
        size_t len = 0;
        unsigned char lo = 0x80, hi = 0xBF;   // the second byte's range
        if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c >= 0xE0 && c <= 0xEF) {
            len = 3;
            if (c == 0xE0) lo = 0xA0;   // not overlong
            if (c == 0xED) hi = 0x9F;   // not a surrogate
        } else if (c >= 0xF0 && c <= 0xF4) {
            len = 4;
            if (c == 0xF0) lo = 0x90;   // not overlong
            if (c == 0xF4) hi = 0x8F;   // not past U+10FFFF
        }
        bool valid = len != 0 && i + len <= s.size();
        for (size_t k = 1; valid && k < len; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            valid = k == 1 ? b >= lo && b <= hi : b >= 0x80 && b <= 0xBF;
        }
        if (valid) {
            out.append(s.substr(i, len));
            i += len;
        } else {
            out += "\\ufffd";
            ++i;
        }
    }
    out += '"';
    return out;
}

std::string withoutHome(std::string_view s, std::string_view home) {
    while (home.size() > 1 && (home.back() == '/' || home.back() == '\\')) home.remove_suffix(1);
    // "/" or "C:" would match too much.
    if (home.size() < 3) return std::string(s);
    auto pathChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
               c == '_' || static_cast<unsigned char>(c) >= 0x80;
    };
    auto sameSeparatorsAside = [](char a, char b) { return a == b || ((a == '/' || a == '\\') && (b == '/' || b == '\\')); };
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        bool match = i + home.size() <= s.size() && (i == 0 || (!pathChar(s[i - 1]) && s[i - 1] != '/' && s[i - 1] != '\\'));
        for (size_t k = 0; match && k < home.size(); ++k) match = sameSeparatorsAside(s[i + k], home[k]);
        if (match && i + home.size() < s.size() && pathChar(s[i + home.size()])) match = false;
        if (match) {
            out += '~';
            i += home.size();
        } else {
            out += s[i++];
        }
    }
    return out;
}

std::string isoTime(int64_t unixMillis) {
    // Days since 1970-01-01 to a civil date (the proleptic Gregorian calendar).
    int64_t days = unixMillis >= 0 ? unixMillis / 86400000 : -((-unixMillis + 86399999) / 86400000);
    const int64_t ms = unixMillis - days * 86400000;
    days += 719468;   // from 0000-03-01
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t dayOfEra = days - era * 146097;
    const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t mp = (5 * dayOfYear + 2) / 153;   // March = 0
    const int64_t day = dayOfYear - (153 * mp + 2) / 5 + 1;
    const int64_t month = mp < 10 ? mp + 3 : mp - 9;
    const int64_t year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03lldZ", static_cast<long long>(year),
                  static_cast<long long>(month), static_cast<long long>(day), static_cast<long long>(ms / 3600000),
                  static_cast<long long>(ms / 60000 % 60), static_cast<long long>(ms / 1000 % 60),
                  static_cast<long long>(ms % 1000));
    return buf;
}

std::string quoteForWindows(std::string_view arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string_view::npos) return std::string(arg);
    std::string out = "\"";
    size_t backslashes = 0;
    for (const char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        // Backslashes before a quote are doubled, and the quote escaped.
        out.append(c == '"' ? backslashes * 2 + 1 : backslashes, '\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, '\\');   // before the closing quote
    out += '"';
    return out;
}

std::string ingestUrl(const Config& cfg) {
    return "https://" + cfg.domain + "/v1/ingest/" + cfg.dataset + "?timestamp-field=_time";
}

std::vector<std::string> curlArguments(const Config& cfg, const std::string& headers, const std::string& body,
                                       const std::string& response, const std::string& curlLog) {
    return {"-sS",           "--proto",     "=https", "--connect-timeout", "5",      "--max-time", "20",
            "-H",            "@" + headers, "--data-binary", "@" + body,   "-o",     response,     "--stderr",
            curlLog,         "-w",          "%{stderr}HTTP %{http_code}\\n", ingestUrl(cfg)};
}

Delivery judge(int status, std::string_view body) {
    if (status >= 200 && status < 300) {
        const std::string failed = jsonField(body, "failed");
        if (!failed.empty() && failed != "0") {
            std::string why = jsonField(body, "error");
            return {Delivery::Outcome::Sent, failed + " event(s)" + (why.empty() ? "" : ": " + why)};
        }
        return {};
    }
    std::string message = "HTTP " + std::to_string(status);
    if (const std::string why = jsonField(body, "message"); !why.empty()) message += ": " + why;
    // No answer, too many requests, or the service's own trouble: later.
    if (status == 0 || status == 408 || status == 429 || status >= 500) return {Delivery::Outcome::Retry, message};
    return {Delivery::Outcome::GiveUp, message};
}

// ---- the session ----

void describeRenderer(const RendererInfo& info) { g_renderer = info; }

void start(const Config& cfg, const GameInfo& game, const std::vector<std::string>& args, std::string_view build,
           Transport transport) {
    if (!cfg.enabled || g_active) return;
    auto* s = new Session;
    s->cfg = cfg;
    s->id = newSessionId();
    s->game = game.name ? game.name : "";
    s->home = homeFolder();
    if (!transport) {
        std::error_code ec;
        const fs::path tmp = fs::temp_directory_path(ec);
#if defined(_WIN32)
        s->dir = tmp / ("ps2n-axiom-" + s->id);
        fs::create_directories(s->dir, ec);
        const bool made = !ec;
#else
        // Private: the headers file holds the token.
        std::string templ = utf8(tmp / "ps2n-axiom-XXXXXX");
        const bool made = !tmp.empty() && mkdtemp(templ.data()) != nullptr;
        if (made) s->dir = templ;
#endif
        if (!made || !writeFile(s->dir / "headers.txt", "Authorization: Bearer " + cfg.token +
                                                            "\nContent-Type: application/x-ndjson\n")) {
            PS2_WARN("axiom", "cannot make a folder for curl's files in %s; the log stays here", utf8(tmp).c_str());
            delete s;
            return;
        }
        transport = [cfg, dir = s->dir](const std::string& body) { return sendWithCurl(cfg, dir, body); };
    }
    s->transport = std::move(transport);

    std::string line;
    for (const std::string& a : args) {
        if (!line.empty()) line += ' ';
        line += a.find_first_of(" \t") == std::string::npos ? a : "\"" + a + "\"";
    }
    std::string e = s->event("start")
                        .str("build", build)
                        .str("generator", game.generator ? game.generator : "")
                        .num("functions", int64_t(game.functionCount))
                        .str("os", osName())
                        .str("cpu", cpuName())
                        .num("threads", int64_t(std::thread::hardware_concurrency()))
                        .str("display", displayKind())
                        .str("renderer", g_renderer.renderer)
                        .num("upscale", int64_t(g_renderer.upscale))
                        .str("gpu", g_renderer.gpu)
                        .str("args", withoutHome(line, s->home))
                        .done();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_session = s;
        s->push(std::move(e));
    }
    g_active = true;
    s->thread = std::thread([s] { s->run(); });
    PS2_INFO("axiom", "sending this run's log to Axiom: dataset %s, session %s", cfg.dataset.c_str(), s->id.c_str());
}

bool active() { return g_active; }

void onLog(LogLevel level, const char* category, std::string_view message) {
    if (!g_active || level > LogLevel::Info || std::strcmp(category, "axiom") == 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Session* s = g_session;
    if (!s || s->stopping) return;
    if (++s->lines > kMaxLines) {
        ++s->dropped;
        return;
    }
    if (message.size() > kMaxMessage) message = message.substr(0, kMaxMessage);
    s->push(s->event("log")
                .str("level", levelName(level))
                .str("category", category)
                .str("message", withoutHome(message, s->home))
                .num("vsync", int64_t(hw::vsyncCount()))
                .done());
    if (level <= LogLevel::Warn) {
        s->soon = true;
        s->wake.notify_one();
    }
}

void finish(int exitCode, const std::vector<std::pair<std::string, uint64_t>>& issues) {
    Session* s = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        s = g_session;
        if (!s) return;
        g_active = false;
        for (const auto& [key, count] : issues)
            s->push(s->event("issue").str("key", withoutHome(key, s->home)).num("count", int64_t(count)).done());
        const double seconds = std::chrono::duration<double>(Clock::now() - s->started).count();
        s->push(s->event("end")
                    .num("exit_code", exitCode)
                    .num("vsync", int64_t(hw::vsyncCount()))
                    .real("seconds", seconds)
                    .num("dropped", int64_t(s->dropped))
                    .done());
        s->stopping = true;
    }
    s->wake.notify_one();
    s->thread.join();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_session = nullptr;
    }
    if (!s->dir.empty()) {
        std::error_code ec;
        fs::remove_all(s->dir, ec);
    }
    delete s;
}

void setIntervalsForTests(std::chrono::milliseconds send, std::chrono::milliseconds speed) {
    g_sendEvery = send;
    g_speedEvery = speed;
}

} // namespace ps2::axiom
