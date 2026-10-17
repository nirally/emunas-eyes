#include "core/gadget_model.h"
#include "tests/check.h"

using namespace mecvr;
using gadget_model::Throw;

namespace {
// Moves the hand along `vel` (m/s) for `secs` at 90 Hz; returns how many throws fired.
int move(Throw& t, vm::Vec3& pos, vm::Vec3 vel, float secs) {
    int n = 0;
    for (float s = 0; s < secs; s += 1.f / 90) {
        pos = pos + vel * (1.f / 90);
        n += t.update(pos, 1.f / 90);
    }
    return n;
}
}  // namespace

TEST(throw_up_and_forward_holds_until_the_arm_drops) {
    Throw t;
    vm::Vec3 p{0.2f, -0.35f, -0.2f};  // hand at the chest
    move(t, p, {}, 0.2f);
    CHECK(move(t, p, {0, 3.f, -2.f}, 0.2f) == 1);  // throw up-forward, ends ~0.25 m above the eyes
    CHECK(t.holding);
    move(t, p, {}, 1.0f);  // arm stays up: still holding
    CHECK(t.holding);
    move(t, p, {0, -2.f, 0}, 0.25f);  // arm comes down below the shoulder
    move(t, p, {}, 0.05f);
    CHECK(!t.holding);
}

TEST(throw_chest_punch_is_not_a_throw) {
    Throw t;
    vm::Vec3 p{0.2f, -0.3f, -0.15f};
    move(t, p, {}, 0.2f);
    CHECK(move(t, p, {0, 0.5f, -4.f}, 0.12f) == 0);  // the straight jab from the punch tests
    CHECK(!t.holding);
}

TEST(throw_slow_raise_is_not_a_throw) {
    Throw t;
    vm::Vec3 p{0.2f, -0.35f, -0.2f};
    move(t, p, {}, 0.2f);
    CHECK(move(t, p, {0, 1.2f, -0.5f}, 0.5f) == 0);  // reaching up for a ledge
}

TEST(throw_low_upswing_is_not_a_throw) {
    // An uppercut-style swing that stays below the chin doesn't fire.
    Throw t;
    vm::Vec3 p{0.2f, -0.7f, -0.2f};  // hand at the hip
    move(t, p, {}, 0.2f);
    CHECK(move(t, p, {0, 3.f, -1.f}, 0.1f) == 0);  // ends at -0.4 m
}

TEST(throw_hold_ends_after_max_hold) {
    Throw t;
    vm::Vec3 p{0.2f, -0.35f, -0.2f};
    move(t, p, {}, 0.2f);
    move(t, p, {0, 3.f, -2.f}, 0.2f);
    move(t, p, {}, 4.2f);
    CHECK(!t.holding);
    CHECK(move(t, p, {}, 0.5f) == 0);  // no second throw without a new swing
}

TEST(throw_quick_flick_holds_at_least_min_hold) {
    Throw t;
    vm::Vec3 p{0.2f, -0.35f, -0.2f};
    move(t, p, {}, 0.2f);
    move(t, p, {0, 3.f, -2.f}, 0.2f);
    move(t, p, {0, -9.f, 0}, 0.05f);  // straight back down
    CHECK(t.holding);  // still for the game to see the press
    move(t, p, {}, 0.15f);
    CHECK(!t.holding);
}

TEST(open_hand_needs_all_fingers_off) {
    CHECK(gadget_model::open_hand(0.f, 0.15f, 0.f));
    CHECK(!gadget_model::open_hand(0.35f, 0.15f, 0.f));  // finger on the trigger
    CHECK(!gadget_model::open_hand(0.f, 0.15f, 1.f));    // thumb resting
    CHECK(!gadget_model::open_hand(0.f, 0.9f, 0.f));     // gripping
    CHECK(!gadget_model::open_hand(0.f, 0.3f, 0.f));     // a relaxed fist (PLAYTEST-6h)
}

TEST(throw_hook_from_a_raised_guard_is_not_a_throw) {
    // PLAYTEST-6h: hooks from a guard at the chin, rising a little, fired the MAG Rope and held Up for seconds.
    Throw t;
    vm::Vec3 p{0.25f, -0.2f, -0.25f};
    move(t, p, {}, 0.2f);
    CHECK(move(t, p, {-3.f, 2.f, -0.5f}, 0.12f) == 0);  // up and across, ends ~5 cm above the eyes
    CHECK(!t.holding);
}

TEST(throw_right_after_a_hook_is_blocked) {
    Throw t;
    vm::Vec3 p{0.2f, -0.35f, -0.2f};
    move(t, p, {}, 0.2f);
    t.note_punch();
    CHECK(move(t, p, {0, 3.f, -2.f}, 0.2f) == 0);
    p = {0.2f, -0.35f, -0.2f};
    move(t, p, {}, 0.5f);
    CHECK(move(t, p, {0, 3.f, -2.f}, 0.2f) == 1);  // a real throw a moment later works
}

TEST(throw_guard_height_lets_go) {
    Throw t;
    vm::Vec3 p{0.2f, -0.35f, -0.2f};
    move(t, p, {}, 0.2f);
    move(t, p, {0, 3.f, -2.f}, 0.2f);
    move(t, p, {}, 0.3f);
    p.y = -0.1f;  // back at guard height (chin)
    move(t, p, {}, 0.02f);
    p.y = -0.1f;
    move(t, p, {}, 0.05f);
    CHECK(!t.holding);
}
