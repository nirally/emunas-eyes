#include <cstdio>
#include <cstdlib>

#include "core/room_model.h"
#include "tests/check.h"

using namespace mecvr::room_model;

static bool near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) < eps; }

// Fake Faith: steady speed from the measured stick curve, reached with a 0.25 s lag; can be blocked.
struct FakeFaith {
    SpeedCurve curve;
    V2 pos, vel;
    V2 step(Stick s, float dt, bool blocked = false) {
        const float mag = std::sqrt(s.x * s.x + s.y * s.y);
        V2 target;
        if (mag > 0) {
            const float v = curve.speed_of(mag);
            target = V2{s.x / mag * v, -s.y / mag * v};
        }
        vel = vel + (target - vel) * (dt / (0.25f + dt));
        if (blocked) { vel = {}; return {}; }
        const V2 d = vel * dt;
        pos = pos + d;
        return d;
    }
};

TEST(room_model_speed_curve_round_trip) {
    SpeedCurve c;
    for (float v : {0.16f, 0.5f, 0.93f, 1.2f}) CHECK(near(c.speed_of(c.stick_for(v)), v, 1e-3f));
    CHECK(c.speed_of(0.5f) == 0);  // inside the game's deadzone
}

TEST(room_model_tiny_shift_doesnt_walk) {
    Model m;
    V2 view;
    m.update({0, 0}, {}, false, 0.01f, view);
    Stick s;
    for (int i = 0; i < 50; ++i) s = m.update({0.03f, 0.f}, {}, false, 0.01f, view);  // settled 3 cm away
    CHECK(s.x == 0 && s.y == 0);
    CHECK(near(view.x, 0.03f, 1e-4f));
}

TEST(room_model_faith_moves_with_the_step) {
    Model m;
    FakeFaith f;
    V2 view, moved, head;
    float worst_gap = 0;
    m.update(head, {}, false, 0.01f, view);
    for (int i = 0; i < 300; ++i) {
        if (i < 60) head.z -= 0.8f * 0.01f;  // a 0.48 m step forward at 0.8 m/s
        const Stick s = m.update(head, moved, false, 0.01f, view);
        if (i == 10) CHECK(s.y > 0.7f && std::fabs(s.x) < 1e-3f);  // already walking forward mid-step
        // What the player sees this frame: Faith's current position + the view offset = the real head.
        if (std::getenv("ROOM_TRACE") && i % 10 == 0)
            printf("    t=%.2f head %.3f faith %.3f gap %.3f stick %.2f\n", i * 0.01f, head.z, f.pos.z, view.z, s.y);
        CHECK(near(f.pos.z + view.z, head.z, 0.005f));
        if (view.len() > worst_gap) worst_gap = view.len();
        moved = f.step(s, 0.01f);
    }
    CHECK(worst_gap < 0.25f);         // her body never far behind during the step
    CHECK(view.len() < 0.06f);        // and under you once you stand still
}

TEST(room_model_user_stick_takes_priority) {
    Model m;
    V2 view;
    m.update({0, 0}, {}, false, 0.01f, view);
    const Stick s = m.update({0.3f, 0.f}, {}, true, 0.01f, view);
    CHECK(s.x == 0 && s.y == 0);
    CHECK(near(view.x, 0.3f, 1e-4f));
}

TEST(room_model_wall_caps_the_view) {
    Model m;
    FakeFaith f;
    V2 view, moved, head;
    m.update(head, {}, false, 0.01f, view);
    for (int i = 0; i < 200; ++i) {
        if (i < 100) head.z -= 0.01f;  // walk 1 m into a wall
        moved = f.step(m.update(head, moved, false, 0.01f, view), 0.01f, true);
    }
    CHECK(near(view.len(), 0.5f, 0.01f));  // stays 0.5 m from Faith, never through the wall
}

TEST(room_model_unrequested_motion_is_not_absorbed) {
    Model m;
    V2 view;
    m.update({0, 0}, {}, false, 0.01f, view);
    m.update({0.f, 0.f}, {0.3f, 0.f}, false, 0.01f, view);  // Faith pushed sideways by a hit
    CHECK(near(view.len(), 0.f, 1e-4f));                    // the view moves with Faith
}

TEST(room_model_recentre_starts_fresh) {
    Model m;
    V2 view;
    m.update({0.4f, 0.f}, {}, false, 0.01f, view);
    m.update({2.0f, 0.f}, {}, false, 0.01f, view);  // jump > 1 m
    CHECK(near(view.len(), 0.f, 1e-4f));
}
