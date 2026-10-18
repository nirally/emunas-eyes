#include "core/arm_solve.h"
#include "tests/check.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace mecvr;
using arm_solve::Bone;
using vm::Quat;
using vm::Vec3;

namespace {

// A synthetic Faith-like skeleton in bind space (T-pose, arms along +-x), animated with random joint rotations.
// Each bone's skinning matrix S_b = S_parent o (T(pivot) R T(-pivot)) maps bind space to the world, like the game's.
struct Rig {
    struct B { int parent; Vec3 pivot; float range; int seg; int side; };  // seg/side: what the solver should say
    std::vector<B> bones;
    std::vector<int> sloppy;  // fingertips whose matrices jitter ~3 mm (real ones fit only 2-5 mm)
    int add(int parent, Vec3 pivot, float range, int seg = -1, int side = -1) {
        bones.push_back({parent, pivot, range, seg, side});
        return int(bones.size()) - 1;
    }
    Rig() {
        const int chest = add(-1, {0, 1.4f, 0}, 0.3f);
        add(chest, {0, 1.38f, 0.08f}, 0.2f);  // chest helper
        for (int s = 0; s < 2; ++s) {
            const float x = s ? -1.f : 1.f;
            const int clav = add(chest, {0.02f * x, 1.45f, 0}, 0.25f);
            const int upper = add(clav, {0.18f * x, 1.45f, 0}, 0.8f, arm_solve::kUpper, s);
            add(clav, {0.18f * x, 1.45f, 0}, 0.4f, arm_solve::kUpper, s);           // upper-arm twist
            add(upper, {0.40f * x, 1.45f, 0.01f}, 0.3f, arm_solve::kUpper, s);      // elbow helper, above the elbow
            const int fore = add(upper, {0.46f * x, 1.45f, 0}, 0.9f, arm_solve::kFore, s);
            add(fore, {0.54f * x, 1.45f, 0}, 0.4f, arm_solve::kFore, s);            // forearm twist
            const int hand = add(fore, {0.70f * x, 1.45f, 0}, 0.7f, arm_solve::kHand, s);
            for (int f = 0; f < 5; ++f) {
                const Vec3 root{(0.74f + 0.01f * f) * x, 1.45f, -0.04f + 0.02f * f};
                const int a = add(hand, root, 0.5f, arm_solve::kHand, s);
                const int b = add(a, root + Vec3{0.04f * x, 0, 0}, 0.5f, arm_solve::kHand, s);
                if (f == 2) sloppy.push_back(add(b, root + Vec3{0.07f * x, 0, 0}, 0.5f, arm_solve::kHand, s));
            }
        }
    }
};

struct Xf { Quat q; Vec3 t; };  // world = q * x + t
Xf compose(const Xf& a, const Xf& b) { return {a.q * b.q, a.q.rotate(b.t) + a.t}; }

uint32_t g_seed = 12345;
float rnd() { g_seed = g_seed * 1664525u + 1013904223u; return float(g_seed >> 8) / float(1 << 24) * 2.f - 1.f; }

std::vector<Bone> frame(const Rig& rig) {
    std::vector<Xf> xf(rig.bones.size());
    std::vector<Bone> out(rig.bones.size());
    for (size_t i = 0; i < rig.bones.size(); ++i) {
        const auto& b = rig.bones[i];
        const Quat r = Quat::axis_angle({rnd(), rnd(), rnd()}, b.range * rnd());
        Xf local{r, b.pivot - r.rotate(b.pivot)};  // rotate about the pivot
        if (b.parent < 0) local.t = local.t + Vec3{550.f + rnd() * 0.5f, 66.f, 22.f + rnd() * 0.5f};
        xf[i] = b.parent < 0 ? local : compose(xf[b.parent], local);
        const Vec3 ex = xf[i].q.rotate({1, 0, 0}), ey = xf[i].q.rotate({0, 1, 0}), ez = xf[i].q.rotate({0, 0, 1});
        const float m[12] = {ex.x, ey.x, ez.x, xf[i].t.x, ex.y, ey.y, ez.y, xf[i].t.y, ex.z, ey.z, ez.z, xf[i].t.z};
        for (int k = 0; k < 12; ++k) out[i].m[k] = m[k];
    }
    for (int s : rig.sloppy)
        for (int k : {3, 7, 11}) out[s].m[k] += 0.004f * rnd();
    return out;
}

}  // namespace

TEST(arm_solve_finds_both_arms_and_segments) {
    Rig rig;
    arm_solve::Learner l;
    l.reset(int(rig.bones.size()));
    for (int f = 0; f < 120; ++f) l.add(frame(rig).data());
    const auto r = l.solve();
    CHECK(r.ok);
    if (!r.ok) return;
    // The arm found as 0 is either side; match by the shoulder's x.
    const int right = r.arm[0].shoulder.x > 0 ? 0 : 1;
    for (int a = 0; a < 2; ++a) {
        const float x = a == right ? 1.f : -1.f;
        CHECK((r.arm[a].shoulder - Vec3{0.18f * x, 1.45f, 0}).length() < 0.005f);
        CHECK((r.arm[a].elbow - Vec3{0.46f * x, 1.45f, 0}).length() < 0.005f);
        CHECK((r.arm[a].wrist - Vec3{0.70f * x, 1.45f, 0}).length() < 0.005f);
        const int hb = r.arm[a].hand_bone;
        CHECK(hb >= 0 && r.segment[hb] == arm_solve::kHand && r.side[hb] == a);
        CHECK(r.arm[a].hand_joints.size() >= 10);  // finger roots and knuckles
    }
    int wrong = 0;
    for (size_t b = 0; b < rig.bones.size(); ++b) {
        const auto& want = rig.bones[b];
        const int side = want.side < 0 ? -1 : (want.side == 0) == (right == 0) ? 0 : 1;
        if (r.segment[b] != want.seg || (want.seg >= 0 && r.side[b] != side)) {
            ++wrong;
            printf("    bone %zu: want seg %d side %d, got seg %d side %d\n", b, want.seg, side, r.segment[b], r.side[b]);
        }
    }
    CHECK(wrong == 0);
}

TEST(arm_solve_needs_motion) {
    Rig rig;
    arm_solve::Learner l;
    l.reset(int(rig.bones.size()));
    const auto still = frame(rig);
    for (int f = 0; f < 120; ++f) l.add(still.data());  // the same pose every frame: nothing can be told apart
    CHECK(!l.solve().ok);
}
