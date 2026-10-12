#include <windows.h>

#include <cstdint>

#include "core/sigscan.h"
#include "tests/check.h"

using namespace mecvr::sigscan;

__declspec(noinline) int sigscan_marker() {
    static volatile int x = 42;
    return x * 3;
}

TEST(sigscan_parse) {
    auto p = parse("48 8B ?? 05 ? C3");
    CHECK(p.has_value());
    CHECK(p->bytes.size() == 6);
    CHECK(p->mask[0] && !p->mask[2] && !p->mask[4] && p->mask[5]);
    CHECK(!parse("4G").has_value());
    CHECK(!parse("").has_value());
}

TEST(sigscan_find_with_wildcards) {
    const uint8_t data[] = {0x90, 0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0xC3};
    auto p = parse("48 8B 05 ?? ?? ?? ?? C3");
    CHECK(find(data, sizeof(data), *p) == data + 1);
    auto miss = parse("48 8B 06");
    CHECK(find(data, sizeof(data), *miss) == nullptr);
}

TEST(sigscan_rip_relative) {
    // mov rax, [rip+0x10] : 48 8B 05 10 00 00 00 -> target = insn + 7 + 0x10
    const uint8_t insn[] = {0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00};
    CHECK(rip_relative(insn, 3, 7) == insn + 7 + 0x10);
}

TEST(sigscan_find_in_own_module) {
    // Scan this test exe for a pattern we know is present: its own PE code bytes.
    const uint8_t* fn = reinterpret_cast<const uint8_t*>(&sigscan_marker);
    Pattern p;
    for (int i = 0; i < 8; ++i) { p.bytes.push_back(fn[i]); p.mask.push_back(true); }
    void* base = reinterpret_cast<void*>(GetModuleHandleA(nullptr));
    CHECK(find_in_module(base, p) != nullptr);
}
