#pragma once
// Minimal test harness (hearth-style: no framework). TEST registers, CHECK records failures.
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace kiln::test {
    struct Case { const char* name; std::function<void()> fn; };
    inline std::vector<Case>& Cases() { static std::vector<Case> c; return c; }
    inline int& Failures() { static int f = 0; return f; }
    inline int& Checks() { static int c = 0; return c; }
    struct Register { Register(const char* n, std::function<void()> f) { Cases().push_back({ n, std::move(f) }); } };
}

#define KILN_CAT2(a, b) a##b
#define KILN_CAT(a, b) KILN_CAT2(a, b)
#define TEST(name) static void name(); static ::kiln::test::Register KILN_CAT(reg_, name)(#name, name); static void name()
#define CHECK(cond) do { ++::kiln::test::Checks(); if (!(cond)) { ++::kiln::test::Failures(); \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++::kiln::test::Checks(); auto _a = (a); auto _b = (b); if (!(_a == _b)) { \
    ++::kiln::test::Failures(); std::printf("  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)
#define CHECK_NEAR(a, b, eps) do { ++::kiln::test::Checks(); const double _a = double(a), _b = double(b); \
    if (!(std::fabs(_a - _b) <= double(eps))) { ++::kiln::test::Failures(); \
    std::printf("  FAIL %s:%d: %s (%g) near %s (%g)\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)
