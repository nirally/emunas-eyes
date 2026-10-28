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
    struct B { int parent; Vec3 pivot; float range; int seg; int side; Vec3 axis; };  // seg/side: what the solver should say; axis 0 = random
    std::vector<B> bones;
    std::vector<int> sloppy;  // fingertips whose matrices jitter ~3 mm (real ones fit only 2-5 mm)
    int add(int parent, Vec3 pivot, float range, int seg = -1, int side = -1, Vec3 axis = {}) {
        bones.push_back({parent, pivot, range, seg, side, axis});
        return int(bones.size()) - 1;
    }
    // no_sternum: the collarbones are roots (their joint with the chest isn't in the piece; live 7g: 28 joints).
    explicit Rig(bool no_sternum = false) {
        const int chest = add(-1, {0, 1.4f, 0}, 0.3f);
        add(chest, {0, 1.38f, 0.08f}, 0.2f);  // chest helper
        for (int s = 0; s < 2; ++s) {
            const float x = s ? -1.f : 1.f;
            const int clav = add(no_sternum ? -1 : chest, {0.02f * x, 1.45f, 0}, 0.25f);
            const int upper = add(clav, {0.18f * x, 1.45f, 0}, 0.8f, arm_solve::kUpper, s);
            add(upper, {0.18f * x, 1.45f, 0}, 0.6f, arm_solve::kUpper, s, {1, 0, 0});  // upper-arm twist (about the arm)
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
        const Quat r = Quat::axis_angle(b.axis.length() > 0 ? b.axis : Vec3{rnd(), rnd(), rnd()}, b.range * rnd());
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

TEST(arm_solve_leaves_a_collarbone_without_its_breastbone_joint) {
    // Only the shoulder joint shows for the collarbone: it must not ride with the upper arm (live 7g: Faith's whole
    // body swung with the arms).
    Rig rig(true);
    arm_solve::Learner l;
    l.reset(int(rig.bones.size()));
    for (int f = 0; f < 120; ++f) l.add(frame(rig).data());
    const auto r = l.solve();
    CHECK(r.ok);
    if (!r.ok) return;
    int wrong = 0;
    for (size_t b = 0; b < rig.bones.size(); ++b) {
        const auto& want = rig.bones[b];
        if ((want.seg < 0) != (r.segment[b] < 0)) {
            ++wrong;
            printf("    bone %zu: want seg %d, got seg %d side %d\n", b, want.seg, r.segment[b], r.side[b]);
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

TEST(arm_solve_adopt_takes_only_bones_left_off) {
    Rig rig;
    arm_solve::Learner l;
    l.reset(int(rig.bones.size()));
    for (int f = 0; f < 120; ++f) l.add(frame(rig).data());
    const auto full = l.solve();
    CHECK(full.ok);
    if (!full.ok) return;
    // The first solve missed three finger bones of one hand; a later one places them.
    auto cur = full;
    std::vector<int> missed;
    for (size_t b = 0; b < cur.side.size() && missed.size() < 3; ++b)
        if (cur.side[b] == 1 && cur.segment[b] == arm_solve::kHand && int(b) != cur.arm[1].hand_bone) {
            missed.push_back(int(b));
            cur.side[b] = -1;
            cur.segment[b] = arm_solve::kNone;
        }
    // The later solve numbered the arms the other way round, and put one bone elsewhere (must not move it).
    auto next = full;
    std::swap(next.arm[0], next.arm[1]);
    for (int& sd : next.side)
        if (sd >= 0) sd = 1 - sd;
    int moved = -1;
    for (size_t b = 0; b < next.side.size(); ++b)
        if (cur.side[b] == 0 && cur.segment[b] == arm_solve::kUpper) { moved = int(b); next.segment[b] = arm_solve::kFore; break; }
    CHECK(arm_solve::adopt_new_bones(cur, next) == 3);
    for (int b : missed) CHECK(cur.side[b] == 1 && cur.segment[b] == arm_solve::kHand);
    CHECK(moved >= 0 && cur.segment[moved] == arm_solve::kUpper);
    CHECK(arm_solve::adopt_new_bones(cur, next) == 0);  // nothing left to take
}

TEST(arm_solve_adopt_matches_arms_by_their_bones) {
    Rig rig;
    arm_solve::Learner l;
    l.reset(int(rig.bones.size()));
    for (int f = 0; f < 120; ++f) l.add(frame(rig).data());
    auto cur = l.solve();
    CHECK(cur.ok);
    if (!cur.ok) return;
    auto next = cur;
    next.arm[0].elbow = next.arm[0].elbow + Vec3{0, 0.03f, 0};  // joints a few cm off: still the same arms
    for (size_t b = 0; b < cur.side.size(); ++b)
        if (cur.segment[b] == arm_solve::kHand) { cur.side[b] = -1; cur.segment[b] = arm_solve::kNone; break; }
    auto other = next;  // a solve that put the forearms' bones on the upper arms: not the same arms
    for (size_t b = 0; b < other.side.size(); ++b)
        if (other.segment[b] == arm_solve::kFore || other.segment[b] == arm_solve::kHand) other.segment[b] = arm_solve::kUpper;
    CHECK(arm_solve::adopt_new_bones(cur, other) == -1);
    CHECK(arm_solve::adopt_new_bones(cur, next) == 1);
}

TEST(arm_solve_rejects_arms_that_cannot_be_hers) {
    Rig rig;
    arm_solve::Learner l;
    l.reset(int(rig.bones.size()));
    for (int f = 0; f < 120; ++f) l.add(frame(rig).data());
    const auto good = l.solve();
    CHECK(good.ok && arm_solve::implausible(good) == nullptr);
    auto r = good;  // one forearm 6 cm longer than the other
    r.arm[1].wrist = r.arm[1].wrist + (r.arm[1].wrist - r.arm[1].elbow).normalized() * 0.06f;
    CHECK(arm_solve::implausible(r) != nullptr);
    r = good;  // upper arm 40 cm
    r.arm[0].shoulder = r.arm[0].elbow + (r.arm[0].shoulder - r.arm[0].elbow).normalized() * 0.40f;
    CHECK(arm_solve::implausible(r) != nullptr);
    r = good;  // a hand without its bones
    for (size_t b = 0; b < r.side.size(); ++b)
        if (r.side[b] == 0 && r.segment[b] == arm_solve::kHand) r.side[b] = -1;
    CHECK(arm_solve::implausible(r) != nullptr);
}

TEST(arm_solve_merges_pieces_that_copy_bones) {
    // PLAYTEST-8n: her arms spread over pieces that each copy some of the skeleton's matrices.
    Rig rig;
    const int n = int(rig.bones.size());
    std::vector<std::vector<Bone>> all;
    for (int f = 0; f < 120; ++f) all.push_back(frame(rig));
    // Piece 0: the first half of the bones; piece 1: the second half plus copies of bones 0-4.
    std::vector<int> p0, p1;
    for (int b = 0; b < n; ++b) (b < n / 2 ? p0 : p1).push_back(b);
    for (int b = 0; b < 5; ++b) p1.push_back(b);
    std::vector<std::vector<Bone>> piece[2];
    std::vector<std::vector<const Bone*>> frames;
    for (auto& fr : all) {
        piece[0].emplace_back();
        piece[1].emplace_back();
        for (int b : p0) piece[0].back().push_back(fr[b]);
        for (int b : p1) piece[1].back().push_back(fr[b]);
    }
    for (size_t f = 0; f < all.size(); ++f) frames.push_back({piece[0][f].data(), piece[1][f].data()});
    const auto m = arm_solve::merge_pieces(frames, {int(p0.size()), int(p1.size())});
    CHECK(int(m.src.size()) == n);
    for (int b = 0; b < 5; ++b) CHECK(m.of[1][int(p1.size()) - 5 + b] == m.of[0][b]);
    // The merged skeleton solves like the one piece.
    arm_solve::Learner l;
    l.reset(int(m.src.size()));
    std::vector<Bone> g(m.src.size());
    for (size_t f = 0; f < all.size(); ++f) {
        for (size_t u = 0; u < m.src.size(); ++u) g[u] = frames[f][m.src[u].first][m.src[u].second];
        l.add(g.data());
    }
    CHECK(l.solve().ok);
}

TEST(arm_solve_leaves_off_bones_that_do_not_keep_the_arm_joints) {
    // PLAYTEST-8p: over all her pieces her hips and legs come in too; a joint of theirs can lie beside the arm in bind
    // space (here a hem bone hanging from the chest, pivot beside the right upper arm) without moving with it.
    Rig rig;
    const int hem = rig.add(0, {0.38f, 1.2f, 0}, 1.0f);
    arm_solve::Learner l;
    l.ride_check = true;  // D-072: on for her merged pieces only
    l.reset(int(rig.bones.size()));
    for (int f = 0; f < 120; ++f) l.add(frame(rig).data());
    const auto r = l.solve();
    CHECK(r.ok);
    CHECK(r.side[hem] == -1 && r.segment[hem] == arm_solve::kNone);
    int wrong = 0;  // the arms' own bones are all still on them
    for (size_t b = 0; b < rig.bones.size(); ++b)
        if (rig.bones[b].seg >= 0 && r.segment[b] != rig.bones[b].seg) ++wrong;
    CHECK(wrong == 0);
}
