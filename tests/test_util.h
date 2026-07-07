#pragma once

// A tiny dependency-free test harness shared across the test translation units.
// Counters live in inline function-local statics so every TU sees the same
// totals without a separate .cpp to link.

#include <cmath>
#include <cstdio>

namespace lumena::test {

inline int& checkCount() {
    static int count = 0;
    return count;
}

inline int& failureCount() {
    static int count = 0;
    return count;
}

inline void reportCheck(bool condition, const char* expr, const char* file,
                        int line) {
    ++checkCount();
    if (!condition) {
        ++failureCount();
        std::printf("  [FAIL] %s:%d: %s\n", file, line, expr);
    }
}

/// Absolute-tolerance float comparison for brightness values.
inline bool approxEqual(float a, float b, float epsilon = 1e-4f) {
    return std::fabs(a - b) <= epsilon;
}

} // namespace lumena::test

#define CHECK(cond) \
    ::lumena::test::reportCheck((cond), #cond, __FILE__, __LINE__)

#define CHECK_APPROX(a, b)                                              \
    ::lumena::test::reportCheck(::lumena::test::approxEqual((a), (b)),  \
                                #a " ~= " #b, __FILE__, __LINE__)
