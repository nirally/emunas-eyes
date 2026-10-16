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
    CHECK(near(view.x, 0.3f, 0.005f));  // only the gentle slide back (0.25 m/s, PLAYTEST-6e)
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

TEST(room_model_game_lunge_moves_the_view) {
    // PLAYTEST-5m: during/after our walking command, an attack lunge or zipline launch along the same axis
    // was absorbed, so Faith's body left the view behind. Movement faster than walking is the game's own.
    Model m;
    FakeFaith f;
    V2 view, moved, head;
    m.update(head, {}, false, 0.01f, view);
    for (int i = 0; i < 40; ++i) {  // lean forward: we start walking her forward
        if (i < 20) head.z -= 0.01f;
        moved = f.step(m.update(head, moved, false, 0.01f, view), 0.01f);
    }
    const V2 before = view;
    for (int i = 0; i < 30; ++i) m.update(head, {0.f, -0.06f}, false, 0.01f, view);  // 6 m/s lunge forward, 1.8 m
    CHECK(near(view.z, before.z, 0.08f));  // view stays on her body (only the first frame, 6 cm, absorbed)
}

TEST(room_model_attack_steps_never_carry_her_past_the_head) {
    // PLAYTEST-6b report: slow attack steps (under the lunge speed) along our walking command were absorbed
    // beyond the gap, so the view ended up behind her body.
    Model m;
    V2 view, head;
    m.update(head, {}, false, 0.01f, view);
    head.z = -0.10f;
    m.update(head, {}, false, 0.01f, view);  // lean 10 cm: walking forward
    for (int i = 0; i < 30; ++i) m.update(head, {0.f, -0.02f}, false, 0.01f, view);  // attack step, 2 m/s, 0.6 m
    if (std::getenv("ROOM_TRACE")) printf("    after attack step: view %.3f\n", view.z);
    CHECK(view.len() < 0.12f);  // only what our small command explains is absorbed (was 0.5 m: the cap)
}

TEST(room_model_stalled_walk_pushes_harder) {
    // PLAYTEST-6d: in fights the game ignored the small walking stick (Faith 0.00 m/s at 0.75 for seconds).
    // Fake a combat stance whose deadzone ends at 0.79 instead of 0.62: the stall push must still close the gap.
    Model m;
    FakeFaith f;
    for (int i = 0; i < SpeedCurve::kN; ++i) f.curve.stick[i] += 0.17f;  // nothing below 0.79
    V2 view, moved, head{-0.15f, 0.f};
    m.update({}, {}, false, 0.01f, view);
    for (int i = 0; i < 300; ++i) moved = f.step(m.update(head, moved, false, 0.01f, view), 0.01f);  // 3 s
    CHECK(view.len() < 0.06f);  // under you again (was stuck at 0.15 m)
    CHECK(m.boost <= m.boost_max);
}

TEST(room_model_gap_bleeds_while_player_drives) {
    // PLAYTEST-6e: steps and leans made while running with the stick added up to 0.34 m in a fight.
    Model m;
    V2 view;
    m.update({}, {}, true, 0.01f, view);
    m.update({0.3f, 0.f}, {}, true, 0.01f, view);  // stepped 30 cm while holding the stick
    for (int i = 0; i < 150; ++i) m.update({0.3f, 0.f}, {0.f, -0.04f}, true, 0.01f, view);  // running 1.5 s
    CHECK(view.len() < 0.05f);
    Model k;  // a small gap is left alone
    k.update({}, {}, true, 0.01f, view);
    for (int i = 0; i < 100; ++i) k.update({0.03f, 0.f}, {}, true, 0.01f, view);
    CHECK(near(view.x, 0.03f, 1e-4f));
}
