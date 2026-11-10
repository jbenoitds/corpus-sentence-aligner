// SPDX-License-Identifier: MIT
// A deliberately tiny unit-test harness (no external dependencies).
#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace minitest {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void report(const char* file, int line, const std::string& msg) {
    ++failures();
    std::cerr << "  FAILED " << file << ":" << line << ": " << msg << "\n";
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& v) {
    os << "{";
    for (std::size_t i = 0; i < v.size(); ++i) os << (i ? ", " : "") << v[i];
    return os << "}";
}

template <typename A, typename B>
std::string describe(const A& a, const B& b) {
    std::ostringstream os;
    os << "expected [" << b << "] got [" << a << "]";
    return os.str();
}

}  // namespace minitest

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST(name)                                                                   \
    static void MT_CAT(mt_test_, name)();                                            \
    static minitest::Registrar MT_CAT(mt_reg_, name)(#name, &MT_CAT(mt_test_, name)); \
    static void MT_CAT(mt_test_, name)()

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) minitest::report(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(actual, expected)                                                               \
    do {                                                                                         \
        const auto& mt_a = (actual);                                                             \
        const auto& mt_b = (expected);                                                           \
        if (!(mt_a == mt_b))                                                                     \
            minitest::report(__FILE__, __LINE__, #actual " == " #expected ": " + minitest::describe(mt_a, mt_b)); \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                                          \
    do {                                                                                           \
        double mt_a = (actual), mt_b = (expected);                                                 \
        if (std::fabs(mt_a - mt_b) > (tol))                                                        \
            minitest::report(__FILE__, __LINE__, #actual " ~= " #expected ": " + minitest::describe(mt_a, mt_b)); \
    } while (0)

#define CHECK_THROWS(expr)                                                       \
    do {                                                                         \
        bool mt_threw = false;                                                   \
        try {                                                                    \
            (void)(expr);                                                        \
        } catch (...) {                                                          \
            mt_threw = true;                                                     \
        }                                                                        \
        if (!mt_threw) minitest::report(__FILE__, __LINE__, "expected exception: " #expr); \
    } while (0)
