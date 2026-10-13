// Minimal unit-check runner: no framework, just CHECK and a registry.
#include "tests/check.h"

#include <cstdio>
#include <exception>

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int failed = 0;
    for (auto& t : mecvr::test::registry()) {
        mecvr::test::g_failures = 0;
        try {
            t.fn();
        } catch (const std::exception& e) {
            std::printf("    exception: %s\n", e.what());
            ++mecvr::test::g_failures;
        }
        std::printf("%s %s\n", mecvr::test::g_failures ? "FAIL" : "ok  ", t.name);
        if (mecvr::test::g_failures) ++failed;
    }
    std::printf("%zu tests, %d failed\n", mecvr::test::registry().size(), failed);
    return failed ? 1 : 0;
}
