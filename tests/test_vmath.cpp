#include "core/vmath.h"
#include "tests/check.h"

using namespace mecvr::vm;

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
static constexpr float kPi = 3.14159265f;

TEST(vmath_identity_transform) {
    LinearTransform m{};
    m.set(Quat{}, {1, 2, 3});
    CHECK(near(m.right[0], 1) && near(m.up[1], 1) && near(m.back[2], 1));
    CHECK(near(m.trans[1], 2));
    CHECK(near(yaw_of(m), 0) && near(pitch_of(m), 0) && near(roll_of(m), 0));
}

TEST(vmath_yaw_round_trip) {
    for (float deg : {-170.f, -90.f, -30.f, 0.f, 45.f, 90.f, 135.f}) {
        LinearTransform m{};
        m.set(yaw_quat(deg * kPi / 180.f), {});
        CHECK(near(yaw_of(m), deg * kPi / 180.f, 1e-3f));
        CHECK(near(roll_of(m), 0, 1e-4f));
    }
}

TEST(vmath_yaw_left_is_positive) {
    // Turning left (CCW from above) from looking down -Z means looking toward -X.
    LinearTransform m{};
    m.set(yaw_quat(kPi / 2), {});
    Vec3 fwd = m.b() * -1.f;
    CHECK(near(fwd.x, -1) && near(fwd.z, 0));
}

TEST(vmath_pitch_and_roll_isolated) {
    Quat pitch = Quat::axis_angle({1, 0, 0}, 20.f * kPi / 180.f);   // look up 20 deg
    Quat roll = Quat::axis_angle({0, 0, 1}, 15.f * kPi / 180.f);    // roll 15 deg about view axis
    LinearTransform m{};
    m.set(yaw_quat(1.0f) * pitch * roll, {});
    CHECK(near(yaw_of(m), 1.0f, 1e-3f));
    CHECK(near(pitch_of(m), 20.f * kPi / 180.f, 1e-3f));
    CHECK(near(std::fabs(roll_of(m)), 15.f * kPi / 180.f, 1e-3f));
}

TEST(vmath_menu_camera_from_capture) {
    // Real rows from the main-menu capture (D-009): pure yaw, no pitch, no roll.
    LinearTransform m{{-0.2235f, 0, -0.9747f, 0}, {0, 1, 0, 0}, {0.9747f, 0, -0.2235f, 0}, {-66.46f, 62.6f, 30.15f, 1}};
    CHECK(near(pitch_of(m), 0, 1e-3f));
    CHECK(near(roll_of(m), 0, 1e-3f));
    // Rebuilding from the extracted yaw reproduces the basis.
    LinearTransform r{};
    r.set(yaw_quat(yaw_of(m)), m.t());
    CHECK(near(r.right[0], m.right[0], 1e-3f) && near(r.right[2], m.right[2], 1e-3f));
    CHECK(near(r.back[0], m.back[0], 1e-3f) && near(r.back[2], m.back[2], 1e-3f));
}

TEST(vmath_set_preserves_w_lanes) {
    LinearTransform m{{0, 0, 0, 7}, {0, 0, 0, 8}, {0, 0, 0, 9}, {0, 0, 0, 1}};
    m.set(yaw_quat(0.3f), {1, 1, 1});
    CHECK(m.right[3] == 7 && m.up[3] == 8 && m.back[3] == 9 && m.trans[3] == 1);
}

TEST(vmath_heading_survives_forward_roll) {
    // PLAYTEST-1c: during Faith's roll the game camera pitches through straight down and past.
    // Forward-based yaw flips 180 deg there; heading_of must stay put the whole way round.
    const float heading = 0.7f;
    for (float pitch_deg = 0; pitch_deg < 360; pitch_deg += 15) {
        LinearTransform m{};
        m.set(yaw_quat(heading) * Quat::axis_angle({1, 0, 0}, -pitch_deg * kPi / 180.f), {});
        CHECK(near(heading_of(m), heading, 1e-3f));
    }
}

TEST(vmath_heading_matches_yaw_when_level_or_tilted) {
    for (float deg : {-150.f, -45.f, 0.f, 30.f, 120.f}) {
        LinearTransform m{};
        // wall-run style sideways tilt of 20 deg must not change heading
        m.set(yaw_quat(deg * kPi / 180.f) * Quat::axis_angle({0, 0, 1}, 20.f * kPi / 180.f), {});
        CHECK(near(heading_of(m), deg * kPi / 180.f, 1e-3f));
    }
}

TEST(vmath_quat_of_round_trip) {
    Quat cases[] = {Quat{}, yaw_quat(2.5f), Quat::axis_angle({1, 0, 0}, -2.8f),
                    yaw_quat(0.7f) * Quat::axis_angle({1, 0, 0}, 3.0f) * Quat::axis_angle({0, 0, 1}, 0.4f),
                    Quat::axis_angle({0.3f, -1, 0.5f}, 1.9f)};
    for (Quat q : cases) {
        LinearTransform m{};
        m.set(q, {});
        Quat r = quat_of(m);
        // q and -q are the same rotation
        float d = std::fabs(q.x * r.x + q.y * r.y + q.z * r.z + q.w * r.w);
        CHECK(near(d, 1.f, 1e-4f));
    }
}

TEST(vmath_game_motion_strips_heading_and_look_pitch) {
    // Same formula as camera_override (D-014): motion = pitch(-baseline) * yaw(-heading) * game.
    // For game = yaw(h) * pitch(p) * roll(r), the result must be pitch(p - baseline) * roll(r).
    const float h = 1.2f, p = -0.5f, r = 0.3f, baseline = -0.2f;
    LinearTransform game{};
    game.set(yaw_quat(h) * Quat::axis_angle({1, 0, 0}, p) * Quat::axis_angle({0, 0, 1}, r), {});
    Quat motion = Quat::axis_angle({1, 0, 0}, -baseline) * yaw_quat(-heading_of(game)) * quat_of(game);
    Quat expect = Quat::axis_angle({1, 0, 0}, p - baseline) * Quat::axis_angle({0, 0, 1}, r);
    float d = std::fabs(motion.x * expect.x + motion.y * expect.y + motion.z * expect.z + motion.w * expect.w);
    CHECK(near(d, 1.f, 1e-3f));
}

TEST(vmath_invert4_game_view_projection) {
    // Current (unjittered) view-projection from a real view-constant dump (docs/recon/taa/viewcb_1.txt, 72..87).
    const float vp[16] = {0.523161f, 0.000126f, 0.206660f, 4.546785f,  0.001131f, 0.999993f, -0.003473f, -47.414555f,
                          -0.000001f, 0.0f,      0.000002f, 0.059927f,  0.367394f, -0.003646f, -0.930058f, 30.860641f};
    float inv[16];
    CHECK(invert4(vp, inv));
    float id[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += vp[r * 4 + k] * inv[k * 4 + c];
            id[r * 4 + c] = s;
        }
    for (int i = 0; i < 16; ++i) CHECK(near(id[i], (i % 5) ? 0.f : 1.f, 1e-3f));
    // A world point 4 m in front of the camera round-trips through clip space.
    const float cam[3] = {-18.743696f, 47.524940f, 25.590939f};
    // forward = -(view row 2) = (0.367394, -0.003646, -0.930058)
    const float p[4] = {cam[0] + 0.367394f * 4, cam[1] - 0.003646f * 4, cam[2] - 0.930058f * 4, 1};
    float clip[4], back[4];
    mul4(vp, p, clip);
    CHECK(clip[3] > 3.9f && clip[3] < 4.1f);  // w = distance in front
    mul4(inv, clip, back);
    for (int i = 0; i < 3; ++i) CHECK(near(back[i] / back[3], p[i], 1e-2f));
}

TEST(vmath_invert4_singular) {
    const float z[16] = {};
    float out[16];
    CHECK(!invert4(z, out));
}
