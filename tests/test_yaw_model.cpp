#include "core/yaw_model.h"
#include "tests/check.h"

using namespace mecvr::yaw_model;

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

// A fake game: applies injected counts one frame later; optional own turn per frame; can ignore mouse.
struct FakeGame {
    float heading = 0.5f;
    int in_flight = 0;
    void frame(int counts, float own_turn = 0, bool ignores_mouse = false, float k = -0.001691f) {
        if (!ignores_mouse) heading = wrap(heading + in_flight * k);
        heading = wrap(heading + own_turn);
        in_flight = counts;
    }
};

TEST(yaw_model_starts_without_a_jump) {
    Model m;
    FakeGame g;
    const int c = m.update(0, g.heading, 0.f, 0.f, true);
    CHECK(c == 0);
    CHECK(near(m.world, g.heading));
}

TEST(yaw_model_body_follows_the_head) {
    Model m;
    FakeGame g;
    double t = 0;
    float head = 0;
    for (int i = 0; i < 200; ++i, t += 0.01) {
        if (i == 10) head = 0.8f;  // the player looks 46 deg left
        g.frame(m.update(t, g.heading, head, 0.f, true));
    }
    CHECK(near(wrap(g.heading - (m.world + head)), 0.f, 0.01f));  // the game faces where the head looks
    CHECK(near(m.world, 0.5f, 0.01f));                            // the view did not turn by itself
}

TEST(yaw_model_stick_turn_turns_view_and_game) {
    Model m;
    FakeGame g;
    double t = 0;
    for (int i = 0; i < 100; ++i, t += 0.01) g.frame(m.update(t, g.heading, 0.f, i < 50 ? 0.02f : 0.f, true));
    CHECK(near(m.world, wrap(0.5f + 1.0f), 0.02f));                // 50 frames x 0.02 rad
    CHECK(near(wrap(g.heading - m.world), 0.f, 0.01f));
}

TEST(yaw_model_game_turns_pass_to_the_view) {
    Model m;
    FakeGame g;
    double t = 0;
    for (int i = 0; i < 60; ++i, t += 0.01) {
        const int c = m.update(t, g.heading, 0.f, 0.f, true);
        g.frame(c, (i >= 10 && i < 30) ? 0.157f : 0.f);  // quick turn: ~180 deg over 20 frames
    }
    CHECK(near(m.world, wrap(0.5f + 3.14f), 0.05f));  // the view turned with the game, no fight back
    CHECK(near(wrap(g.heading - m.world), 0.f, 0.02f));
}

TEST(yaw_model_ignored_injection_is_dropped) {
    Model m;
    FakeGame g;
    double t = 0;
    float head = 0.5f;
    for (int i = 0; i < 100; ++i, t += 0.01) g.frame(m.update(t, g.heading, head, 0.f, true), 0.f, true);  // climbing
    const float w = m.world;
    for (int i = 0; i < 100; ++i, t += 0.01) g.frame(m.update(t, g.heading, head, 0.f, true));  // mouse works again
    CHECK(near(m.world, w, 0.01f));                                // no burst of fake "game turns"
    CHECK(near(wrap(g.heading - (m.world + head)), 0.f, 0.01f));
}

TEST(yaw_model_no_steering_when_paused) {
    Model m;
    FakeGame g;
    CHECK(m.update(0, g.heading, 1.0f, 0.f, false) == 0);
}

TEST(yaw_model_snap_turn_once_per_flick) {
    Snap s;
    CHECK(near(s.update(0.9f, 0.785f), -0.785f));  // right flick turns right (negative yaw)
    CHECK(near(s.update(1.0f, 0.785f), 0.f));      // held: no repeat
    CHECK(near(s.update(0.1f, 0.785f), 0.f));      // back to centre re-arms
    CHECK(near(s.update(-0.8f, 0.785f), 0.785f));  // left flick
}
