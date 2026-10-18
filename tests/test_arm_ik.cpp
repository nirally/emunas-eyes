#include "core/arm_ik.h"
#include "tests/check.h"

#include <cmath>

using namespace mecvr;
using vm::Quat;
using vm::Vec3;

namespace {

bool near3(Vec3 a, Vec3 b, float tol = 1e-3f) { return (a - b).length() < tol; }
float qangle(Quat q) { return 2.f * std::acos(std::fmin(1.f, std::fabs(q.normalized().w))); }

// Faith's right arm hanging, slightly bent (shoulder high, elbow 0.28 m below, wrist 0.245 m below and forward).
const Vec3 kS{0.18f, 1.45f, 0}, kE{0.20f, 1.17f, 0.02f}, kW{0.21f, 0.93f, -0.03f};
const Vec3 kPole = Vec3{0.6f, -1.f, 0.3f}.normalized();  // down, out, back

}  // namespace

TEST(arm_ik_reaches_target_keeping_lengths) {
    const Vec3 target{0.30f, 1.30f, -0.35f};
    const auto r = arm_ik::solve(kS, kE, kW, target, kPole);
    CHECK(!r.clamped);
    CHECK(near3(r.wrist, target));
    CHECK(std::fabs((r.elbow - kS).length() - (kE - kS).length()) < 1e-3f);
    CHECK(std::fabs((r.wrist - r.elbow).length() - (kW - kE).length()) < 1e-3f);
    // Each segment's correction carries its end joints to the new ones; the shoulder stays.
    CHECK(near3(r.upper.apply(kS), kS));
    CHECK(near3(r.upper.apply(kE), r.elbow));
    CHECK(near3(r.fore.apply(kE), r.elbow));
    CHECK(near3(r.fore.apply(kW), r.wrist));
    CHECK(near3(r.hand.apply(kW), r.wrist));
}

TEST(arm_ik_clamps_out_of_reach) {
    const Vec3 far{0.25f, 1.40f, -1.5f};
    const auto r = arm_ik::solve(kS, kE, kW, far, kPole);
    CHECK(r.clamped);
    const float reach = (kE - kS).length() + (kW - kE).length();
    CHECK(std::fabs((r.wrist - kS).length() - reach) < 0.005f);
    CHECK(((r.wrist - kS).normalized() - (far - kS).normalized()).length() < 1e-3f);  // stops short, on the line
    const auto in = arm_ik::solve(kS, kE, kW, kS + Vec3{0, 0, -0.001f}, kPole);  // into the shoulder
    CHECK(in.clamped);
    CHECK((in.wrist - kS).length() > 0.02f);
}

TEST(arm_ik_elbow_bends_towards_pole) {
    const Vec3 targets[3] = {{0.30f, 1.30f, -0.35f}, {0.05f, 1.20f, -0.30f}, {0.35f, 1.60f, -0.20f}};
    for (const Vec3& t : targets) {
        const auto r = arm_ik::solve(kS, kE, kW, t, kPole);
        const Vec3 n = (t - kS).normalized();
        const Vec3 off = (r.elbow - kS) - n * (r.elbow - kS).dot(n);
        CHECK(off.dot(kPole) > 0);
    }
}

TEST(arm_ik_identity_when_target_is_the_game_wrist) {
    // Pole in the game's own bend plane, on the elbow's side: nothing moves.
    const Vec3 n = (kW - kS).normalized();
    const Vec3 pole = ((kE - kS) - n * (kE - kS).dot(n)).normalized();
    const auto r = arm_ik::solve(kS, kE, kW, kW, pole);
    CHECK(near3(r.elbow, kE, 2e-3f));
    CHECK(qangle(r.upper.q) < 0.01f);
    CHECK(qangle(r.fore.q) < 0.01f);
}

TEST(arm_ik_upper_twist_follows_bend_plane) {
    // A point on the inside of the elbow (towards the bend) stays on the inside after the solve.
    const Vec3 target{0.10f, 1.35f, -0.30f};
    const auto r = arm_ik::solve(kS, kE, kW, target, kPole);
    const Vec3 x0 = (kE - kS).normalized(), u0 = (kW - kE).normalized();
    const Vec3 inside0 = u0 - x0 * u0.dot(x0);  // where the forearm folds to, seen from the upper arm
    const Vec3 x1 = (r.elbow - kS).normalized(), u1 = (r.wrist - r.elbow).normalized();
    const Vec3 inside1 = (u1 - x1 * u1.dot(x1)).normalized();
    CHECK(r.upper.q.rotate(inside0.normalized()).dot(inside1) > 0.98f);  // ~15 deg game bend: the pole easing costs a few deg
}

TEST(arm_ik_no_flip_near_straight_game_arm) {
    // The game's arm almost straight (the bend plane is noise): moving the target smoothly must turn the
    // corrections smoothly.
    const Vec3 s{0, 1.45f, 0}, e{0, 1.17f, 0.0005f}, w{0.0001f, 0.925f, 0.001f};
    Quat prev_up, prev_fore;
    for (int i = 0; i <= 40; ++i) {
        const float a = -1.f + 0.05f * i;
        const Vec3 t = s + Vec3{0.15f * std::sin(a), -0.25f, -0.30f * std::cos(a)};
        const auto r = arm_ik::solve(s, e, w, t, kPole);
        if (i) {
            CHECK(qangle(r.upper.q * prev_up.conjugate()) < 0.15f);
            CHECK(qangle(r.fore.q * prev_fore.conjugate()) < 0.15f);
        }
        prev_up = r.upper.q;
        prev_fore = r.fore.q;
    }
}

TEST(arm_ik_apply_to_skinning_matrix) {
    // S maps bind space to the world; after apply, S'(x) = C(S(x)) for any point.
    const Quat rq = Quat::axis_angle({0.3f, 1, -0.2f}, 0.7f);
    float m[12];
    const Vec3 c0 = rq.rotate({1, 0, 0}), c1 = rq.rotate({0, 1, 0}), c2 = rq.rotate({0, 0, 1});
    const float rows[12] = {c0.x, c1.x, c2.x, 550.f, c0.y, c1.y, c2.y, 66.f, c0.z, c1.z, c2.z, 22.f};
    for (int k = 0; k < 12; ++k) m[k] = rows[k];
    auto S = [](const float* a, Vec3 x) {
        return Vec3{a[0] * x.x + a[1] * x.y + a[2] * x.z + a[3], a[4] * x.x + a[5] * x.y + a[6] * x.z + a[7],
                    a[8] * x.x + a[9] * x.y + a[10] * x.z + a[11]};
    };
    const arm_ik::Rigid c{Quat::axis_angle({1, 0.5f, 0}, -0.4f), {0.1f, -0.2f, 0.05f}};
    const Vec3 xs[2] = {{0.1f, 1.4f, 0}, {-0.3f, 0.2f, 0.5f}};
    Vec3 want[2];
    for (int i = 0; i < 2; ++i) want[i] = c.apply(S(m, xs[i]));
    arm_ik::apply(c, m);
    for (int i = 0; i < 2; ++i) CHECK(near3(S(m, xs[i]), want[i], 1e-3f));
}

TEST(arm_ik_hand_frame_from_bind_joints) {
    // A right hand in an A-pose facing -z: fingers out and down, knuckle row along z, thumb towards the front (-z).
    const Vec3 w{0.47f, 1.04f, 0.15f};
    const Vec3 f = Vec3{0.6f, -0.8f, 0}.normalized();
    std::vector<Vec3> joints = {w + Vec3{0, 0, -0.03f}};  // thumb root, near: ignored
    for (int k = 0; k < 4; ++k) joints.push_back(w + f * 0.08f + Vec3{0, 0, -0.03f + 0.02f * k});
    Quat q;
    CHECK(arm_ik::hand_frame(w, joints, {0, 0, -1}, q));
    CHECK(near3(q.rotate({0, 0, -1}), f, 0.02f));            // -Z along the fingers
    CHECK(near3(q.rotate({0, 1, 0}), {0, 0, -1}, 0.02f));    // +Y to the thumb side
    CHECK(q.rotate({1, 0, 0}).dot(Vec3{1, 0.75f, 0}.normalized()) > 0.99f);  // X away from the body: palm (-X) on the thigh
    CHECK(!arm_ik::hand_frame(w, {w + Vec3{0, 0, -0.03f}}, {0, 0, -1}, q));  // no finger joints
}

TEST(arm_ik_hand_to_turns_about_new_wrist) {
    const Quat now = Quat::axis_angle({0.2f, 1, 0.3f}, 0.9f), want = Quat::axis_angle({1, -0.4f, 0}, -1.2f);
    const Vec3 w0{0.2f, 0.9f, 0}, wt{0.3f, 1.3f, -0.35f};
    const auto c = arm_ik::hand_to(now, want, w0, wt);
    CHECK(near3(c.apply(w0), wt));
    for (const Vec3& ax : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}})
        CHECK(near3(c.q.rotate(now.rotate(ax)), want.rotate(ax)));
}

TEST(arm_ik_share_twist_moves_twist_into_forearm) {
    auto r = arm_ik::solve(kS, kE, kW, {0.30f, 1.30f, -0.35f}, kPole);
    const Vec3 u0 = (kW - kE).normalized();
    // A hand turned 150 deg about the forearm relative to the forearm's correction.
    r.hand.q = (r.fore.q * Quat::axis_angle(u0, 2.6f)).normalized();
    r.hand.t = r.wrist - r.hand.q.rotate(kW);
    const Quat hand = r.hand.q;
    arm_ik::share_twist(r, kE, kW, 0.5f);
    CHECK(std::fabs(arm_ik::twist_angle(r.fore.q.conjugate() * r.hand.q, u0) - 1.3f) < 0.01f);  // half left at the wrist
    CHECK(near3(r.fore.apply(kE), r.elbow));
    CHECK(near3(r.fore.apply(kW), r.wrist));
    CHECK(qangle(r.hand.q * hand.conjugate()) < 1e-3f);
    arm_ik::share_twist(r, kE, kW, 1.f);
    CHECK(std::fabs(arm_ik::twist_angle(r.fore.q.conjugate() * r.hand.q, u0)) < 0.01f);
}
