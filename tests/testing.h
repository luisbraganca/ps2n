// A minimal test harness (no external dependencies).
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace testing {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline int& failures() {
    static int n = 0;
    return n;
}

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::fprintf(stderr, "%s:%d: %s\n", file, line, msg.c_str());
}

template <typename T>
void print(std::ostringstream& ss, const T& v) {
    if constexpr (std::is_enum_v<T>) ss << static_cast<long long>(v);
    else ss << v;
}

template <typename A, typename B>
std::string describe(const A& a, const B& b) {
    std::ostringstream ss;
    ss << "expected ";
    print(ss, b);
    ss << ", got ";
    print(ss, a);
    return ss.str();
}

inline int runAll(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (const auto& c : registry()) {
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos) continue;
        const int before = failures();
        try {
            c.fn();
        } catch (const std::exception& e) {
            fail(c.name, 0, std::string("unexpected exception: ") + e.what());
        }
        ++ran;
        std::printf("%s %s\n", failures() == before ? "[ ok ]" : "[FAIL]", c.name);
    }
    std::printf("%d test(s), %d failure(s)\n", ran, failures());
    return failures() ? 1 : 0;
}

} // namespace testing

#define TEST_CASE(name)                                                     \
    static void name();                                                     \
    static ::testing::Registrar name##_registrar(#name, name);              \
    static void name()

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) ::testing::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                      \
    do {                                                                    \
        const auto va_ = (a);                                               \
        const auto vb_ = (b);                                               \
        if (!(va_ == vb_))                                                  \
            ::testing::fail(__FILE__, __LINE__, std::string(#a " == " #b ": ") + ::testing::describe(va_, vb_)); \
    } while (0)

#define CHECK_THROWS(expr)                                                  \
    do {                                                                    \
        bool threw_ = false;                                                \
        try {                                                               \
            (void)(expr);                                                   \
        } catch (...) {                                                     \
            threw_ = true;                                                  \
        }                                                                   \
        if (!threw_) ::testing::fail(__FILE__, __LINE__, "expected an exception from " #expr); \
    } while (0)
