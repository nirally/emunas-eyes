#include "core/punch_model.h"
#include "tests/check.h"

using namespace mecvr;
using punch_model::Hand;

namespace {
const vm::Vec3 kFwd{0, 0, -1};
// Moves the hand from `from` along `vel` (m/s) for `secs` at 90 Hz; returns how many punches fired.
int swing(Hand& h, vm::Vec3& pos, vm::Vec3 vel, float secs) {
    int n = 0;
    for (float t = 0; t < secs; t += 1.f / 90) {
        pos = pos + vel * (1.f / 90);
        n += h.update(pos, kFwd, 1.f / 90) != Hand::kNone;
    }
    return n;
}
}  // namespace

TEST(punch_straight_fires_once) {
    Hand h;
    vm::Vec3 p{0.2f, -0.3f, -0.15f};
    swing(h, p, {}, 0.2f);
    CHECK(swing(h, p, {0, 0.5f, -4.f}, 0.12f) == 1);  // 4 m/s forward jab, 0.5 m
    CHECK(swing(h, p, {}, 0.3f) == 0);
}

TEST(punch_slow_reach_is_not_a_punch) {
    Hand h;
    vm::Vec3 p{0.2f, -0.3f, -0.15f};
    swing(h, p, {}, 0.2f);
    CHECK(swing(h, p, {0, 0.3f, -0.9f}, 0.5f) == 0);  // reaching for a door handle
}

TEST(punch_pulling_back_is_not_a_punch) {
    Hand h;
    vm::Vec3 p{0.2f, -0.2f, -0.6f};
    swing(h, p, {}, 0.2f);
    CHECK(swing(h, p, {0, 0, 4.f}, 0.1f) == 0);  // fast retract towards the body
}

TEST(punch_hook_across_the_front) {
    Hand h;
    vm::Vec3 p{0.35f, -0.2f, -0.3f};
    swing(h, p, {}, 0.2f);
    CHECK(swing(h, p, {-4.f, 0, -1.f}, 0.12f) == 1);  // right hook swinging left
}

TEST(punch_combo_needs_the_hand_to_slow_down) {
    Hand h;
    vm::Vec3 p{0.2f, -0.3f, -0.15f};
    swing(h, p, {}, 0.2f);
    int n = swing(h, p, {0, 0, -4.f}, 0.1f);
    n += swing(h, p, {0, 0, 3.f}, 0.1f);   // pull back fast: no punch, no re-arm while fast
    n += swing(h, p, {}, 0.1f);            // stop: re-armed
    n += swing(h, p, {0, 0, -4.f}, 0.1f);  // second jab
    CHECK(n == 2);
}

TEST(punch_hand_at_the_side_does_not_count) {
    Hand h;
    vm::Vec3 p{0.5f, -0.4f, 0.1f};  // hand down by the hip, slightly behind
    swing(h, p, {}, 0.2f);
    CHECK(swing(h, p, {0, 0, -3.f}, 0.05f) == 0);  // arm swing while gesturing
}

TEST(punch_kinds_straight_vs_hook) {
    Hand h;
    vm::Vec3 p{0.2f, -0.3f, -0.15f};
    int kind = 0;
    for (int i = 0; i < 20; ++i) h.update(p, kFwd, 1.f / 90);
    for (int i = 0; i < 10 && !kind; ++i) kind = h.update(p = p + vm::Vec3{0, 0, -4.f / 90}, kFwd, 1.f / 90);
    CHECK(kind == Hand::kStraight);
    Hand k;
    vm::Vec3 q{0.35f, -0.2f, -0.3f};
    kind = 0;
    for (int i = 0; i < 20; ++i) k.update(q, kFwd, 1.f / 90);
    for (int i = 0; i < 10 && !kind; ++i) kind = k.update(q = q + vm::Vec3{-4.f / 90, 0, -1.f / 90}, kFwd, 1.f / 90);
    CHECK(kind == Hand::kHook);
}
