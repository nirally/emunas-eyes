#include "core/room_model.h"
#include "tests/check.h"

using namespace mecvr::room_model;

static bool near(float a, float b, float eps = 0.02f) { return std::fabs(a - b) < eps; }

// Fake Faith: moves along the stick at speed * |stick| m/s (body frame), unless blocked.
struct FakeFaith {
    V2 pos;
    float speed = 2.0f;
    V2 step(Stick s, float dt, bool blocked = false) {
        if (blocked) return {};
        const V2 d{s.x * speed * dt, -s.y * speed * dt};
        pos = pos + d;
        return d;
    }
};

TEST(room_model_small_moves_dont_walk) {
    Model m;
    V2 view;
    m.update({0, 0}, {}, false, view);
    const Stick s = m.update({0.05f, 0.f}, {}, false, view);
    CHECK(s.x == 0 && s.y == 0);
    CHECK(near(view.x, 0.05f, 1e-4f));  // the head moves the view as before
}

TEST(room_model_step_forward_faith_follows_view_stays_on_head) {
    Model m;
    FakeFaith f;
    V2 view, moved;
    m.update({0, 0}, {}, false, view);
    const V2 head{0.f, -0.5f};  // stepped 50 cm forward
    for (int i = 0; i < 300; ++i) {
        const Stick s = m.update(head, moved, false, view);
        if (i == 0) CHECK(s.y > 0.7f && std::fabs(s.x) < 1e-4f);  // walk forward
        moved = f.step(s, 0.01f);
    }
    CHECK(near(f.pos.z, -0.5f, 0.09f));          // Faith walked over
    CHECK(view.len() < 0.09f);                     // her camera now under the head: small offset left
    // what the player sees: Faith's position + view offset = real head
    CHECK(near(f.pos.z + view.z, -0.5f, 0.01f));
}

TEST(room_model_user_stick_takes_priority) {
    Model m;
    V2 view;
    m.update({0, 0}, {}, false, view);
    const Stick s = m.update({0.3f, 0.f}, {}, true, view);
    CHECK(s.x == 0 && s.y == 0);
    CHECK(near(view.x, 0.3f, 1e-4f));
}

TEST(room_model_wall_caps_the_view) {
    Model m;
    FakeFaith f;
    V2 view, moved;
    m.update({0, 0}, {}, false, view);
    for (int i = 0; i < 200; ++i) moved = f.step(m.update({0.f, -1.0f}, moved, false, view), 0.01f, true);  // blocked
    CHECK(near(view.len(), 0.5f, 0.01f));  // stays 0.5 m from Faith, never through the wall
}

TEST(room_model_unrequested_motion_is_not_absorbed) {
    Model m;
    V2 view;
    m.update({0, 0}, {}, false, view);
    m.update({0.f, 0.f}, {0.3f, 0.f}, false, view);  // Faith pushed sideways by a hit; nothing requested
    CHECK(near(view.len(), 0.f, 1e-4f));             // view offset unchanged: the view moves with Faith
}

TEST(room_model_recentre_starts_fresh) {
    Model m;
    V2 view;
    m.update({0.4f, 0.f}, {}, false, view);
    m.update({2.0f, 0.f}, {}, false, view);  // jump > 1 m
    CHECK(near(view.len(), 0.f, 1e-4f));
}
