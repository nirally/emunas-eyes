#pragma once
// Stage 7D: Faith's fingers from the arm piece's own mesh. Pure, unit-tested.
//
// Her finger bones don't pivot on clean joints in the bone matrices (fits of 2-5 mm, docs/recon/STAGE7D-RECON.md),
// so the fingers are found from her skin instead (the arm piece's vertex buffer, read back once): each bone's
// skin centroid in bind space. Per hand, the 12 hand-segment bones whose skin lies furthest out along the fingers
// are the four fingers, 3 bones each, in rows across the hand (index on her front, then middle, ring, pinky);
// along each row, proximal -> distal. A knuckle is where two bones share skin: the weighted mean of the vertices
// both move (min of the two weights); the first knuckle (MCP) is shared with the hand's other bones. Each finger
// curls about one axis through its knuckles, towards the palm (in her bind pose the palms face her thighs: towards
// the other hand). The thumb has no bone of its own in this piece and stays with the hand.
//
// Posing: the bind pose is a relaxed hand. A finger's bones get rigid bind-space transforms relative to the hand
// bone (bind: identity), each joint turned by its angle about its knuckle (forward kinematics); the skinning matrix
// is then hand bone * transform.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/arm_ik.h"
#include "core/arm_solve.h"
#include "core/vmath.h"

namespace mecvr::finger_rig {

struct Vertex {
    vm::Vec3 pos;  // bind space
    uint8_t bone[8];
    uint8_t weight[8];  // /255, summing to 255
};

// The arm piece's vertices: 60 bytes, float3 position at 0, 8 bone indices at 20, 8 weights at 28 (MESHDUMP,
// 2026-10-19). A vertex's bone v is the piece's matrix row v + 1 (row 0 is an identity; skinning the bind mesh with
// recorded frames: 33 of 17691 edges stretched past 50% with v + 1, 4663 with v). Vertices [first, first + count).
// False if the layout doesn't hold (stride, weights not summing to 255 on 95% of them, a bone past `bones`).
inline bool parse(const unsigned char* data, size_t bytes, unsigned stride, unsigned first, unsigned count, int bones,
                  std::vector<Vertex>& out) {
    out.clear();
    if (stride != 60 || count == 0 || (size_t(first) + count) * stride > bytes) return false;
    unsigned good = 0;
    for (unsigned v = first; v < first + count; ++v) {
        const unsigned char* p = data + size_t(v) * stride;
        Vertex x;
        memcpy(&x.pos, p, 12);
        for (int k = 0; k < 8; ++k) x.bone[k] = uint8_t(p[20 + k] + 1);  // vertex bone v skins with row v + 1
        memcpy(x.weight, p + 28, 8);
        int sum = 0;
        bool ok = std::isfinite(x.pos.x) && std::isfinite(x.pos.y) && std::isfinite(x.pos.z);
        for (int k = 0; k < 8; ++k) {
            sum += x.weight[k];
            if (x.weight[k] && x.bone[k] >= bones) ok = false;
        }
        if (ok && sum >= 254 && sum <= 256) ++good;
        out.push_back(x);
    }
    return good * 20 >= count * 19;
}

struct Finger {
    int bone[3] = {-1, -1, -1};  // proximal, middle, distal
    vm::Vec3 pivot[3];           // knuckles: MCP, PIP, DIP (bind space)
    vm::Vec3 axis;               // curl axis (unit, bind space): +angle bends the finger towards the palm
};

struct Hand {
    bool ok = false;
    const char* why = "not built";
    Finger finger[4];  // index, middle, ring, pinky
};

namespace detail {
struct Skin {
    std::vector<double> w;         // per bone: total weight
    std::vector<vm::Vec3> sum;     // per bone: weighted position sum
};
inline Skin skin(const std::vector<Vertex>& verts, int bones) {
    Skin s;
    s.w.assign(bones, 0);
    s.sum.assign(bones, {});
    for (const Vertex& v : verts)
        for (int k = 0; k < 8; ++k)
            if (v.weight[k] && v.bone[k] < bones) {
                const float w = v.weight[k] / 255.f;
                s.w[v.bone[k]] += w;
                s.sum[v.bone[k]] = s.sum[v.bone[k]] + v.pos * w;
            }
    return s;
}
// Where the bones of set a meet the bones of set b: weighted mean of the vertices both move. False if they share
// less than `min_w` of weight.
template <class A, class B>
bool shared(const std::vector<Vertex>& verts, A in_a, B in_b, vm::Vec3& out, double min_w = 0.5) {
    double tw = 0;
    vm::Vec3 sum;
    for (const Vertex& v : verts) {
        float wa = 0, wb = 0;
        for (int k = 0; k < 8; ++k) {
            if (!v.weight[k]) continue;
            if (in_a(v.bone[k])) wa += v.weight[k] / 255.f;
            if (in_b(v.bone[k])) wb += v.weight[k] / 255.f;
        }
        const float w = std::fmin(wa, wb);
        if (w <= 0) continue;
        tw += w;
        sum = sum + v.pos * w;
    }
    if (tw < min_w) return false;
    out = sum * float(1.0 / tw);
    return true;
}
}  // namespace detail

// Per bone: the bind-space point it shares with its arm's hand bone (arm_solve::Learner::joint), rms < 0 for none.
struct Fits {
    std::vector<vm::Vec3> at;
    std::vector<double> rms;
};

// The fingers of arm `a` (arms from arm_solve: side/segment per bone, wrist and elbow in bind space). `front`: her
// front in bind space (unit). `fits`: optional, for knuckles the skin doesn't show. `min_weight`: a bone needs this
// much skin to count (helpers have a few vertices).
inline Hand build(const std::vector<Vertex>& verts, const arm_solve::Result& arms, int a, vm::Vec3 front, const Fits* fits = nullptr,
                  double min_weight = 20) {
    using vm::Vec3;
    Hand h;
    const int bones = int(arms.side.size());
    if (!arms.ok || bones == 0 || a < 0 || a > 1) { h.why = "no arms"; return h; }
    const auto sk = detail::skin(verts, bones);
    const arm_solve::Arm& arm = arms.arm[a];
    auto centroid = [&](int b) { return sk.sum[b] * float(1.0 / sk.w[b]); };
    auto out = [&](int b) { return (centroid(b) - arm.wrist).length(); };
    // Candidates: her hand's bones with skin 6+ cm from the wrist (the palm's bones are nearer).
    std::vector<int> cand;
    for (int b = 0; b < bones; ++b)
        if (arms.side[b] == a && arms.segment[b] == arm_solve::kHand && sk.w[b] >= min_weight && out(b) > 0.06f) cand.push_back(b);
    // Shared skin between candidates, as a share of the smaller one's: a finger's bones share skin at their
    // knuckles; neighbouring fingers only a little at the web (live: 15-34% along a finger, 8-11% across).
    std::vector<double> share(size_t(bones) * bones, 0);
    for (const Vertex& v : verts)
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j)
                if (i != j && v.weight[i] && v.weight[j] && v.bone[i] < bones && v.bone[j] < bones)
                    share[size_t(v.bone[i]) * bones + v.bone[j]] += std::fmin(v.weight[i], v.weight[j]) / 255.0;
    auto ratio = [&](int x, int y) { return share[size_t(x) * bones + y] / std::fmin(sk.w[x], sk.w[y]); };
    // Chains from the tips inwards: the farthest unused bone, then the unused bone nearer the wrist it shares the
    // most skin with (5%+), three deep. Chains of three are fingers; the thumb's is shorter.
    std::sort(cand.begin(), cand.end(), [&](int x, int y) { return out(x) > out(y); });
    std::vector<bool> used(bones, false);
    std::vector<std::array<int, 3>> chains;
    for (int tip : cand) {
        if (used[tip]) continue;
        std::array<int, 3> c{tip, -1, -1};
        used[tip] = true;
        int n = 1;
        for (; n < 3; ++n) {
            int best = -1;
            for (int b : cand)
                if (!used[b] && out(b) < out(c[n - 1]) && ratio(b, c[n - 1]) >= 0.05 && (best < 0 || ratio(b, c[n - 1]) > ratio(best, c[n - 1])))
                    best = b;
            if (best < 0) break;
            c[n] = best;
            used[best] = true;
        }
        if (n == 3) chains.push_back({c[2], c[1], c[0]});  // proximal first
    }
    if (chains.size() != 4) { h.why = chains.size() < 4 ? "fewer than four fingers" : "more than four fingers"; return h; }
    // Along the fingers (wrist -> their skin), and across: her front, square to it. Index on her front.
    Vec3 along;
    for (const auto& c : chains)
        for (int b : c) along = along + (centroid(b) - arm.wrist);
    along = along.normalized();
    const Vec3 lat = (front - along * front.dot(along)).normalized();
    if (lat.length() < 0.5f) { h.why = "fingers along her front"; return h; }
    auto across = [&](const std::array<int, 3>& c) { return (centroid(c[0]) + centroid(c[1]) + centroid(c[2]) - arm.wrist * 3.f).dot(lat); };
    std::sort(chains.begin(), chains.end(), [&](const auto& x, const auto& y) { return across(x) > across(y); });
    // The palm faces the other hand.
    const Vec3 other = arms.arm[1 - a].wrist - arm.wrist;
    const Vec3 palm = (other - along * other.dot(along)).normalized();
    if (palm.length() < 0.5f) { h.why = "no palm direction"; return h; }
    auto is_finger = [&](int b) {
        for (const auto& c : chains)
            for (int x : c)
                if (x == b) return true;
        return false;
    };
    for (int f = 0; f < 4; ++f) {
        Finger& fg = h.finger[f];
        std::copy(chains[f].begin(), chains[f].end(), fg.bone);
        const int p = fg.bone[0], m = fg.bone[1], d = fg.bone[2];
        const Vec3 cp = centroid(p), cm = centroid(m), cd = centroid(d);
        // Knuckles from shared skin; halfway between the centroids where there's none.
        if (!detail::shared(verts, [&](int b) { return b == p; }, [&](int b) { return b == m; }, fg.pivot[1])) fg.pivot[1] = (cp + cm) * 0.5f;
        if (!detail::shared(verts, [&](int b) { return b == m; }, [&](int b) { return b == d; }, fg.pivot[2])) fg.pivot[2] = (cm + cd) * 0.5f;
        // The first knuckle: the proximal's joint with the hand bone from the matrices where they fit it (3 mm), else
        // skin shared with the hand, else as far before the proximal's skin as the PIP is past it. (Her right hand's
        // proximals have their knuckle skin nearly to themselves - 0.5 shared vs 22 on the left - and that skin put
        // the knuckles 2 cm out; the matrices fit 0.1-1.4 mm there. On her left the matrices don't turn some
        // proximals against the hand, and the skin is shared.)
        const bool fit_ok = fits && size_t(p) < fits->rms.size() && fits->rms[p] >= 0 && fits->rms[p] < 0.003 &&
                            (fits->at[p] - fg.pivot[1]).length() < 0.06f;
        if (fit_ok)
            fg.pivot[0] = fits->at[p];
        else if (!detail::shared(verts, [&](int b) { return b == p; }, [&](int b) { return b != p && !is_finger(b); }, fg.pivot[0]))
            fg.pivot[0] = cp - (fg.pivot[1] - cp);
        // Curl axis: square to the finger (MCP -> its tip bone) and the palm; +angle turns the finger to the palm.
        const Vec3 dir = (cd - fg.pivot[0]).normalized();
        const Vec3 pf = (palm - dir * palm.dot(dir)).normalized();
        fg.axis = dir.cross(pf).normalized();
    }
    h.ok = true;
    h.why = "ok";
    return h;
}

// Joint angles in degrees from the bind pose (MCP, PIP, DIP): open (curl 0, straightened a little: the bind hand
// is relaxed, a little curled) and closed (curl 1, a fist round the controller's handle).
struct Angles {
    float open[3] = {-10, -10, -5};
    float closed[3] = {70, 90, 60};
};

// The three bones of a finger at curl c (0 open .. 1 closed): bind-space rigid transforms relative to the hand bone.
inline void pose(const Finger& f, float c, const Angles& a, arm_ik::Rigid out[3]) {
    c = c < 0 ? 0 : c > 1 ? 1 : c;
    arm_ik::Rigid acc{};  // identity
    for (int j = 0; j < 3; ++j) {
        const float deg = a.open[j] + (a.closed[j] - a.open[j]) * c;
        const vm::Quat q = vm::Quat::axis_angle(f.axis, deg * 0.01745329f);
        const arm_ik::Rigid r{q, f.pivot[j] - q.rotate(f.pivot[j])};  // about the knuckle
        acc = {(acc.q * r.q).normalized(), acc.q.rotate(r.t) + acc.t};   // acc after r: x -> acc(r(x))
        out[j] = acc;
    }
}

// A skinning matrix (3x4 row-major, world = R x + t) times a bind-space rigid transform: out = m * T.
inline void compose(const float* m, const arm_ik::Rigid& T, float* out) {
    const vm::Vec3 cx = T.q.rotate({1, 0, 0}), cy = T.q.rotate({0, 1, 0}), cz = T.q.rotate({0, 0, 1});
    const float Q[3][3] = {{cx.x, cy.x, cz.x}, {cx.y, cy.y, cz.y}, {cx.z, cy.z, cz.z}};
    const float t[3] = {T.t.x, T.t.y, T.t.z};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) out[i * 4 + j] = m[i * 4] * Q[0][j] + m[i * 4 + 1] * Q[1][j] + m[i * 4 + 2] * Q[2][j];
        out[i * 4 + 3] = m[i * 4] * t[0] + m[i * 4 + 1] * t[1] + m[i * 4 + 2] * t[2] + m[i * 4 + 3];
    }
}

}  // namespace mecvr::finger_rig
