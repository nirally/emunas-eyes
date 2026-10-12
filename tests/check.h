#pragma once
#include <cstdio>
#include <vector>

namespace mecvr::test {

struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int g_failures = 0;
struct Register { Register(const char* n, void (*f)()) { registry().push_back({n, f}); } };

}  // namespace mecvr::test

#define TEST(name)                                                   \
    static void name();                                              \
    static ::mecvr::test::Register name##_reg(#name, &name);         \
    static void name()

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("    %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);     \
            ++::mecvr::test::g_failures;                                                 \
        }                                                                                \
    } while (0)
