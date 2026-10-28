#include "core/finger_rig.h"
#include "tests/check.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace mecvr;
using vm::Quat;
using vm::Vec3;

namespace {

bool near3(Vec3 a, Vec3 b, float tol) { return (a - b).length() < tol; }

// Two hands in Faith's bind pose (A-pose, facing +z, palms to the thighs, fingers down): per hand a hand bone
// (palm skin), four fingers of three bones (index at the front), a two-bone thumb at the front and inwards. Skin
// blends half and half within 6 mm of each knuckle.
struct Mesh {
    arm_solve::Result arms;
    std::vector<finger_rig::Vertex> verts;
    int hand[2], finger[2][4][3], thumb[2][2];
    Vec3 mcp[2][4], pip[2][4], dip[2][4];
    int n = 0;
    int bone(int side, int seg) {
        arms.side.push_back(side);
        arms.segment.push_back(seg);
        return n++;
    }
    void vert(Vec3 p, int a, int b, float wa) {
        finger_rig::Vertex v{};
        v.pos = p;
        v.bone[0] = uint8_t(a);
        v.weight[0] = uint8_t(std::lround(wa * 255));
        v.bone[1] = uint8_t(b);
        v.weight[1] = uint8_t(255 - v.weight[0]);
        verts.push_back(v);
    }
    // A segment from p0 to p1 skinned to bone b, blending with `before` near p0 and `after` near p1 (-1: none).
    void segment(Vec3 p0, Vec3 p1, int b, int before, int after, Vec3 side_dir) {
        const Vec3 d = p1 - p0;
        const Vec3 u = side_dir.normalized(), w = d.normalized().cross(u);
        for (int i = 0; i <= 10; ++i) {
            const Vec3 c = p0 + d * (i / 10.f);
            for (int k = 0; k < 4; ++k) {
                const float ang = k * 1.5707963f;
                const Vec3 p = c + u * (0.007f * std::cos(ang)) + w * (0.007f * std::sin(ang));
                if (before >= 0 && (c - p0).length() < 0.006f) vert(p, b, before, 0.5f);
                else if (after >= 0 && (c - p1).length() < 0.006f) vert(p, b, after, 0.5f);
                else vert(p, b, b, 1.f);
            }
        }
    }
    Mesh() {
        arms.ok = true;
        const int pre = bone(-1, arm_solve::kNone);  // row 0, the identity root
        (void)pre;
        for (int a = 0; a < 2; ++a) {
            const float sx = a == 0 ? -1.f : 1.f;  // arm 0 at -x (her right: she faces +z)
            auto& arm = arms.arm[a];
            arm.shoulder = {0.15f * sx, 1.38f, -0.02f};
            arm.elbow = {0.34f * sx, 1.17f, 0.f};
            arm.wrist = {0.46f * sx, 1.03f, 0.17f};
            hand[a] = bone(a, arm_solve::kHand);
            arm.hand_bone = hand[a];
            const Vec3 in{-sx, 0, 0};  // the palm: towards the other hand
            for (int f = 0; f < 4; ++f) {
                const float z = 0.17f + 0.03f - 0.02f * f;  // index at the front
                const Vec3 base{0.47f * sx, 1.03f - 0.05f, z}, side{0, 0, 1};  // knuckles 5 cm below the wrist
                mcp[a][f] = base;
                pip[a][f] = base + Vec3{0, -0.04f, 0};
                dip[a][f] = base + Vec3{0, -0.065f, 0};
                const Vec3 tip = base + Vec3{0, -0.085f, 0};
                for (int j = 0; j < 3; ++j) finger[a][f][j] = bone(a, arm_solve::kHand);
                segment(arm.wrist + Vec3{0, 0, z - 0.17f}, base, hand[a], -1, finger[a][f][0], side);  // palm
                segment(base, pip[a][f], finger[a][f][0], hand[a], finger[a][f][1], side);
                segment(pip[a][f], dip[a][f], finger[a][f][1], finger[a][f][0], finger[a][f][2], side);
                segment(dip[a][f], tip, finger[a][f][2], finger[a][f][1], -1, side);
            }
            for (int j = 0; j < 2; ++j) thumb[a][j] = bone(a, arm_solve::kHand);
            const Vec3 t0 = arm.wrist + Vec3{0, -0.02f, 0.05f} + in * 0.01f, t1 = t0 + Vec3{0, -0.035f, 0.01f} + in * 0.015f,
                       t2 = t1 + Vec3{0, -0.03f, 0} + in * 0.01f;
            segment(t0, t1, thumb[a][0], hand[a], thumb[a][1], {1, 0, 0});
            segment(t1, t2, thumb[a][1], thumb[a][0], -1, {1, 0, 0});
        }
    }
};

const Vec3 kFront{0, 0, 1};

}  // namespace

TEST(finger_rig_parse_reads_the_game_layout) {
    // Two vertices of the game's 60-byte layout: bone v is matrix row v + 1.
    unsigned char buf[120] = {};
    for (int v = 0; v < 2; ++v) {
        const float pos[3] = {0.1f * v, 1.f, 0.2f};
        memcpy(buf + v * 60, pos, 12);
        buf[v * 60 + 20] = 3;
        buf[v * 60 + 21] = 7;
        buf[v * 60 + 28] = 200;
        buf[v * 60 + 29] = 55;
    }
    std::vector<finger_rig::Vertex> out;
    CHECK(finger_rig::parse(buf, sizeof(buf), 60, 0, 2, 81, out));
    CHECK(out.size() == 2);
    CHECK(out[1].bone[0] == 4 && out[1].bone[1] == 8 && out[1].weight[0] == 200);
    CHECK(near3(out[1].pos, {0.1f, 1.f, 0.2f}, 1e-6f));
    CHECK(!finger_rig::parse(buf, sizeof(buf), 56, 0, 2, 81, out));  // another stride
    buf[29] = 20;                                                       // weights no longer sum to 255
    CHECK(!finger_rig::parse(buf, sizeof(buf), 60, 0, 2, 81, out));
}

TEST(finger_rig_finds_four_fingers_index_first) {
    Mesh m;
    for (int a = 0; a < 2; ++a) {
        const auto h = finger_rig::build(m.verts, m.arms, a, kFront, nullptr, 5);
        CHECK(h.ok);
        if (!h.ok) { printf("    arm %d: %s\n", a, h.why); continue; }
        for (int f = 0; f < 4; ++f) {
            for (int j = 0; j < 3; ++j) CHECK(h.finger[f].bone[j] == m.finger[a][f][j]);  // the thumb is left out
            CHECK(near3(h.finger[f].pivot[0], m.mcp[a][f], 0.008f));
            CHECK(near3(h.finger[f].pivot[1], m.pip[a][f], 0.008f));
            CHECK(near3(h.finger[f].pivot[2], m.dip[a][f], 0.008f));
            CHECK(std::fabs(h.finger[f].axis.dot({0, 1, 0})) < 0.05f);  // square to the finger
        }
    }
}

TEST(finger_rig_takes_finger_bones_the_solve_put_on_the_forearm) {
    Mesh m;
    m.arms.segment[m.finger[0][2][1]] = arm_solve::kFore;  // desktop, standing: finger bones on the forearm
    m.arms.segment[m.finger[0][3][0]] = arm_solve::kFore;
    const auto h = finger_rig::build(m.verts, m.arms, 0, kFront, nullptr, 5);
    CHECK(h.ok);
    CHECK(h.finger[2].bone[1] == m.finger[0][2][1] && h.finger[3].bone[0] == m.finger[0][3][0]);
}

TEST(finger_rig_prefers_the_matrices_knuckle) {
    Mesh m;
    finger_rig::Fits fits;
    fits.at.assign(m.n, {});
    fits.rms.assign(m.n, -1);
    const int p = m.finger[0][1][0];
    fits.at[p] = m.mcp[0][1] + Vec3{0, 0.01f, 0};  // a fit 1 cm nearer the wrist than the skin's knuckle
    fits.rms[p] = 0.0005;
    const auto h = finger_rig::build(m.verts, m.arms, 0, kFront, &fits, 5);
    CHECK(h.ok);
    CHECK(near3(h.finger[1].pivot[0], fits.at[p], 1e-5f));
    fits.rms[p] = 0.01;  // a poor fit: the skin's
    const auto h2 = finger_rig::build(m.verts, m.arms, 0, kFront, &fits, 5);
    CHECK(near3(h2.finger[1].pivot[0], m.mcp[0][1], 0.008f));
}

TEST(finger_rig_curl_bends_to_the_palm_and_keeps_joints) {
    Mesh m;
    for (int a = 0; a < 2; ++a) {
        const auto h = finger_rig::build(m.verts, m.arms, a, kFront, nullptr, 5);
        CHECK(h.ok);
        if (!h.ok) continue;
        const Vec3 palm{a == 0 ? 1.f : -1.f, 0, 0};
        const auto& fg = h.finger[0];
        const Vec3 tip = m.mcp[a][0] + Vec3{0, -0.08f, 0};
        arm_ik::Rigid zero[3], closed[3];
        finger_rig::Angles none;
        for (int j = 0; j < 3; ++j) none.open[j] = none.closed[j] = 0;
        finger_rig::pose(fg, 0.5f, none, zero);
        for (int j = 0; j < 3; ++j) CHECK(near3(zero[j].apply(tip), tip, 1e-5f));  // no angles: the bind pose
        finger_rig::pose(fg, 1.f, {}, closed);
        CHECK((closed[2].apply(tip) - tip).dot(palm) > 0.03f);   // the tip goes to the palm
        CHECK(closed[2].apply(tip).y > tip.y + 0.03f);           // and up towards the wrist
        // Each knuckle stays where the bone before it put it.
        CHECK(near3(closed[0].apply(fg.pivot[1]), closed[1].apply(fg.pivot[1]), 1e-5f));
        CHECK(near3(closed[1].apply(fg.pivot[2]), closed[2].apply(fg.pivot[2]), 1e-5f));
        CHECK(near3(closed[0].apply(fg.pivot[0]), fg.pivot[0], 1e-5f));  // the first one doesn't move
    }
}

TEST(finger_rig_twist_shares_grow_from_elbow_to_wrist) {
    // BUG-037: an upper arm, a forearm of three bones (a third each, blending half and half at their borders) and a
    // hand. Each forearm bone should take about the twist of the middle of its skin, so the elbow's skin barely turns.
    arm_solve::Result arms;
    arms.ok = true;
    auto bone = [&](int side, int seg) { arms.side.push_back(side); arms.segment.push_back(seg); return int(arms.side.size()) - 1; };
    bone(-1, arm_solve::kNone);
    const int up = bone(0, arm_solve::kUpper), f[3] = {bone(0, arm_solve::kFore), bone(0, arm_solve::kFore), bone(0, arm_solve::kFore)},
              hand = bone(0, arm_solve::kHand), other = bone(1, arm_solve::kFore);
    arms.arm[0].elbow = {0.34f, 1.17f, 0.f};
    arms.arm[0].wrist = {0.46f, 1.03f, 0.17f};
    const Vec3 e = arms.arm[0].elbow, d = arms.arm[0].wrist - e;
    std::vector<finger_rig::Vertex> verts;
    auto vert = [&](float t, int a, int b, float wa) {
        finger_rig::Vertex v{};
        v.pos = e + d * t + Vec3{0, 0.02f, 0};
        v.bone[0] = uint8_t(a);
        v.weight[0] = uint8_t(std::lround(wa * 255));
        v.bone[1] = uint8_t(b);
        v.weight[1] = uint8_t(255 - v.weight[0]);
        verts.push_back(v);
    };
    for (int i = -5; i <= 105; ++i) {
        const float t = i / 100.f;
        const int seg = t < 0 ? 0 : t >= 1 ? 2 : int(t * 3);
        const float into = t * 3 - seg;  // 0..1 within the bone's third
        if (t < 0.03f) vert(t, up, f[0], 0.5f);
        else if (t > 0.97f) vert(t, f[2], hand, 0.5f);
        else if (into < 0.05f && seg > 0) vert(t, f[seg - 1], f[seg], 0.5f);
        else vert(t, f[seg], f[seg], 1.f);
    }
    const auto s = finger_rig::twist_shares(verts, arms, 0);
    CHECK(s.size() == arms.side.size());
    CHECK(s[up] < 0 && s[hand] < 0 && s[other] < 0);  // only arm 0's forearm bones
    CHECK(s[f[0]] < s[f[1]] && s[f[1]] < s[f[2]]);
    CHECK(std::fabs(s[f[0]] - 1 / 6.f) < 0.08f);
    CHECK(std::fabs(s[f[1]] - 0.5f) < 0.08f);
    CHECK(std::fabs(s[f[2]] - 5 / 6.f) < 0.08f);
    // The skin at the elbow turns by under a fifth of the hand's twist (one share for all: half).
    CHECK(0.5f * s[f[0]] < 0.2f);
    arms.arm[1] = arms.arm[0];
    CHECK(std::fabs(finger_rig::twist_shares(verts, arms, 1)[other] - 0.5f) < 1e-4f);  // a bone with no skin: mid-forearm
}

TEST(finger_rig_compose_is_matrix_after_transform) {
    const Quat q = Quat::axis_angle(Vec3{0.3f, 1, -0.2f}.normalized(), 0.8f), r = Quat::axis_angle({1, 0, 0}, -0.5f);
    const Vec3 t{550.f, 67.f, 23.f};
    const Vec3 cx = q.rotate({1, 0, 0}), cy = q.rotate({0, 1, 0}), cz = q.rotate({0, 0, 1});
    const float m[12] = {cx.x, cy.x, cz.x, t.x, cx.y, cy.y, cz.y, t.y, cx.z, cy.z, cz.z, t.z};
    const arm_ik::Rigid T{r, {0.01f, -0.02f, 0.03f}};
    float out[12];
    finger_rig::compose(m, T, out);
    const Vec3 x{0.4f, 1.0f, 0.2f}, y = T.apply(x);
    const Vec3 want = q.rotate(y) + t;
    const Vec3 got{out[0] * x.x + out[1] * x.y + out[2] * x.z + out[3], out[4] * x.x + out[5] * x.y + out[6] * x.z + out[7],
                   out[8] * x.x + out[9] * x.y + out[10] * x.z + out[11]};
    CHECK(near3(got, want, 1e-3f));
}
