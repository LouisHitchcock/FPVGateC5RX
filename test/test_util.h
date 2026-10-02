// A minimal test framework for the PC unit tests. No dependencies.
#ifndef C5RX_TEST_UTIL_H
#define C5RX_TEST_UTIL_H

#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace c5rxtest {
static int g_checks = 0;
static int g_fails = 0;
static const char* g_suite = "";

inline void suite(const char* name) { g_suite = name; }

inline void check(bool cond, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!cond) {
        ++g_fails;
        std::printf("  FAIL [%s] %s  (%s:%d)\n", g_suite, expr, file, line);
    }
}

inline void check_eq_i(long got, long want, const char* expr,
                       const char* file, int line) {
    ++g_checks;
    if (got != want) {
        ++g_fails;
        std::printf("  FAIL [%s] %s : got %ld want %ld  (%s:%d)\n",
                    g_suite, expr, got, want, file, line);
    }
}

inline void check_near(double got, double want, double tol, const char* expr,
                       const char* file, int line) {
    ++g_checks;
    if (std::fabs(got - want) > tol) {
        ++g_fails;
        std::printf("  FAIL [%s] %s : got %.4f want %.4f (+-%.4f)  (%s:%d)\n",
                    g_suite, expr, got, want, tol, file, line);
    }
}

inline int report() {
    std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
} // namespace c5rxtest

#define CHECK(c)            c5rxtest::check((c), #c, __FILE__, __LINE__)
#define CHECK_EQ(a,b)       c5rxtest::check_eq_i((long)(a),(long)(b), #a"=="#b, __FILE__, __LINE__)
#define CHECK_NEAR(a,b,t)   c5rxtest::check_near((double)(a),(double)(b),(t), #a"~="#b, __FILE__, __LINE__)

#endif // C5RX_TEST_UTIL_H
