#pragma once
// Stage 7B: two-bone IK for Faith's arms, as corrections of her skinning matrices. Pure, unit-tested.
//
// The game's pose gives shoulder S, elbow E0 and wrist W0 (world, from arm_solve's joints). The shoulder stays;
// the wrist goes to the target (reach clamped to the arm's own lengths), the elbow bends towards the pole hint
// (down and out). Each segment gets one rigid correction, world -> world, to be applied to every bone riding on it
// (S' = C * S): upper arm about the shoulder, forearm from the old elbow to the new, hand from the old wrist to the
// new keeping the game's hand-to-forearm rotation (wrist rotation from the controller is a later step). Segment
// frames: x along the bone, z = the bend plane's normal, so the twist follows the bend plane.
#include <cmath>

#include "core/vmath.h"

namespace mecvr::arm_ik {

struct Rigid {  // p -> q * p + t
    vm::Quat q;
    vm::Vec3 t;
    vm::Vec3 apply(vm::Vec3 p) const { return q.rotate(p) + t; }
};

struct Result {
    Rigid upper, fore, hand;
    vm::Vec3 elbow, wrist;  // new joint positions
    bool clamped = false;   // target out of reach (or too close): the wrist stops short
};

namespace detail {
// Rotation taking the canonical axes to (x, y = z cross x, z); z is made perpendicular to x first.
inline vm::Quat frame(vm::Vec3 x, vm::Vec3 z) {
    z = (z - x * z.dot(x)).normalized();
    const vm::Vec3 y = z.cross(x);
    vm::LinearTransform m{};
    m.right[0] = x.x; m.right[1] = x.y; m.right[2] = x.z;
    m.up[0] = y.x;    m.up[1] = y.y;    m.up[2] = y.z;
    m.back[0] = z.x;  m.back[1] = z.y;  m.back[2] = z.z;
    return vm::quat_of(m).normalized();
}
// Unit normal of the plane through a bone direction x and a second direction d; `fallback` when they're parallel.
inline vm::Vec3 bend_normal(vm::Vec3 x, vm::Vec3 d, vm::Vec3 fallback) {
    vm::Vec3 n = x.cross(d);
    if (n.length() < 1e-4f) n = x.cross(fallback);
    if (n.length() < 1e-6f) n = x.cross({0, 0, 1});
    if (n.length() < 1e-6f) n = x.cross({1, 0, 0});
    return n.normalized();
}
}  // namespace detail

// pole: preferred elbow direction (unit, world), e.g. down and out from the body.
inline Result solve(vm::Vec3 s, vm::Vec3 e0, vm::Vec3 w0, vm::Vec3 target, vm::Vec3 pole) {
    using vm::Vec3;
    Result r;
    const float l1 = (e0 - s).length(), l2 = (w0 - e0).length();
    Vec3 to = target - s;
    float d = to.length();
    const float lo = std::fabs(l1 - l2) + 0.002f, hi = l1 + l2 - 0.002f;
    Vec3 n = d > 1e-6f ? to * (1.f / d) : (w0 - s).normalized();
    if (d > hi || d < lo) {
        d = d > hi ? hi : lo;
        r.clamped = true;
    }
    const Vec3 wt = s + n * d;
    // Elbow: in the plane of n and the pole, at the law-of-cosines angle from n.
    Vec3 b = pole - n * pole.dot(n);
    if (b.length() < 1e-4f) b = (e0 - s) - n * (e0 - s).dot(n);  // pole along the arm: keep the game's bend side
    if (b.length() < 1e-6f) b = detail::bend_normal(n, {0, 1, 0}, {1, 0, 0}).cross(n);
    b = b.normalized();
    float ca = (l1 * l1 + d * d - l2 * l2) / (2.f * l1 * d);
    ca = ca > 1.f ? 1.f : ca < -1.f ? -1.f : ca;
    const float sa = std::sqrt(1.f - ca * ca);
    const Vec3 e1 = s + (n * ca + b * sa) * l1;

    // Segment frames, old and new; the bend normals are shared by the two segments of each pose. New: b x n
    // exactly (same side as x cross u), even when the arm is nearly straight. Old: the game's bend plane, eased
    // towards the pole's as her arm straightens (x0 cross u0 shrinks), so the twist doesn't jitter there.
    const Vec3 x0 = (e0 - s).normalized(), u0 = (w0 - e0).normalized();
    const Vec3 x1 = (e1 - s).normalized(), u1 = (wt - e1).normalized();
    const Vec3 n1 = b.cross(n).normalized();
    Vec3 p0 = pole - x0 * pole.dot(x0);
    if (p0.length() < 1e-4f) p0 = detail::bend_normal(x0, {0, 1, 0}, {1, 0, 0}).cross(x0);
    const Vec3 n0 = detail::bend_normal(x0, u0 - p0.normalized() * 0.03f, pole);  // x0 x -p0 = p0 x x0: the b x n side
    const vm::Quat q_up = (detail::frame(x1, n1) * detail::frame(x0, n0).conjugate()).normalized();
    const vm::Quat q_fore = (detail::frame(u1, n1) * detail::frame(u0, n0).conjugate()).normalized();
    r.upper = {q_up, s - q_up.rotate(s)};
    r.fore = {q_fore, e1 - q_fore.rotate(e0)};
    r.hand = {q_fore, wt - q_fore.rotate(w0)};
    r.elbow = e1;
    r.wrist = wt;
    return r;
}

// Apply a correction to a 3x4 row-major skinning matrix (world = R x + t): R' = Q R, t' = Q t + c.t.
inline void apply(const Rigid& c, float* m) {
    const vm::Vec3 cx = c.q.rotate({1, 0, 0}), cy = c.q.rotate({0, 1, 0}), cz = c.q.rotate({0, 0, 1});
    const float Q[3][3] = {{cx.x, cy.x, cz.x}, {cx.y, cy.y, cz.y}, {cx.z, cy.z, cz.z}};
    float out[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = Q[i][0] * m[j] + Q[i][1] * m[4 + j] + Q[i][2] * m[8 + j];
    }
    out[3] += c.t.x;
    out[7] += c.t.y;
    out[11] += c.t.z;
    for (int k = 0; k < 12; ++k) m[k] = out[k];
}

}  // namespace mecvr::arm_ik
