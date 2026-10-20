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
#include <vector>

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

// Where the elbow should point (unit, world) for a hand at `target`: down and out from the body with the hand
// low, turning to out (a little down) as the hand rises towards shoulder height - a punch lifts the elbow.
// PLAYTEST-7h ("the arms bend a little into the body when throwing a punch", elbow / upper arm, since 7B): with the
// elbow kept low, the upper arm hung from the shoulder across the front of her chest.
inline vm::Vec3 elbow_pole(vm::Vec3 shoulder, vm::Vec3 other, vm::Vec3 target) {
    vm::Vec3 out = shoulder - other;
    out.y = 0;
    float k = (target.y - (shoulder.y - 0.35f)) / 0.30f;  // 0 with the hand 35+ cm below the shoulder, 1 at 5 cm below
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    return (vm::Vec3{0, -1.f + 0.75f * k, 0} + out.normalized() * (0.7f + 0.5f * k)).normalized();
}

// Where the elbow should point (unit, world) from the way the controller is held: with a straight wrist the forearm
// runs back from the hand along the fingers, so the elbow sits near target - fingers * forearm. A guard (controller
// up) tucks it under the hand, a punch puts it behind, a hook out to the side. `body` (elbow_pole) breaks the tie
// when that point is on the shoulder-hand line; k blends from body (0) to hand (1).
// PLAYTEST-7i: with elbow_pole alone "the elbows in both arms bend in to the body like an 'L' shape".
inline vm::Vec3 elbow_from_hand(vm::Vec3 shoulder, vm::Vec3 target, vm::Vec3 fingers, float forearm, vm::Vec3 body, float k) {
    const vm::Vec3 to = target - shoulder;
    if (to.length() < 1e-4f || k <= 0.f) return body;
    const vm::Vec3 n = to.normalized(), v = target - fingers.normalized() * forearm - shoulder;
    const vm::Vec3 hand = v - n * v.dot(n), side = body - n * body.dot(n);
    const vm::Vec3 b = hand * (k > 1.f ? 1.f : k) + side * 0.04f;  // the body's side counts as 4 cm off the line
    return b.length() < 1e-5f ? body : b.normalized();
}

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

// The hand's own axes in bind space, laid out like the glove's hand space (hand_render): -Z along the straight
// fingers (wrist -> the mean of the hand joints past 6 cm: knuckles, fingers), +Y the thumb / index side (Faith's
// front: in her bind pose the palms face the thighs), X = Y x Z. False without finger joints.
inline bool hand_frame(vm::Vec3 wrist, const std::vector<vm::Vec3>& joints, vm::Vec3 front, vm::Quat& out) {
    vm::Vec3 sum;
    int n = 0;
    for (const vm::Vec3& j : joints)
        if ((j - wrist).length() > 0.06f) { sum = sum + (j - wrist); ++n; }
    if (!n) return false;
    const vm::Vec3 z = (sum * -1.f).normalized();
    const vm::Vec3 y = (front - z * front.dot(z)).normalized();
    if (y.length() < 0.5f) return false;
    out = detail::frame(y.cross(z), z);
    return true;
}

// Hand correction: the hand turns from its world rotation `now` (bone rotation * hand_frame) to `want` (the
// controller's), about the new wrist.
inline Rigid hand_to(vm::Quat now, vm::Quat want, vm::Vec3 w0, vm::Vec3 wt) {
    const vm::Quat q = (want * now.conjugate()).normalized();
    return {q, wt - q.rotate(w0)};
}

// The part of q that turns about the unit axis (swing-twist decomposition), as a rotation angle in (-pi, pi].
inline float twist_angle(vm::Quat q, vm::Vec3 axis) {
    const float s = vm::Vec3{q.x, q.y, q.z}.dot(axis);
    float a = 2.f * std::atan2(s, q.w);
    if (a > 3.14159265f) a -= 6.2831853f;
    if (a < -3.14159265f) a += 6.2831853f;
    return a;
}

// The forearm takes the share k (0..1) of the hand's twist about it, so the wrist skin (weighted between forearm and
// hand) isn't wrung when the controller turns far from the game's hand (live: a gap at the wrist). The forearm
// still runs elbow -> wrist; the hand is unchanged.
inline void share_twist(Result& r, vm::Vec3 e0, vm::Vec3 w0, float k) {
    const vm::Vec3 u0 = (w0 - e0).normalized();
    const float a = twist_angle(r.fore.q.conjugate() * r.hand.q, u0) * k;
    r.fore.q = (r.fore.q * vm::Quat::axis_angle(u0, a)).normalized();  // turn about the game's forearm first
    r.fore.t = r.elbow - r.fore.q.rotate(e0);
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

// The world correction that took a bone from `game` to `now` (both rigid 3x4 row-major), applied to another
// matrix `row`: out = now * game^-1 * row. For bones the arm piece doesn't have that ride with one it has (BUG-020:
// a helper bone of her right glove, 6 mm from her hand bone, kept the game's pose and pulled the palm).
inline void carry(const float* game, const float* now, const float* row, float* out) {
    float a[12];  // game^-1 * row: R = Rg^T Rr, t = Rg^T (tr - tg)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) a[i * 4 + j] = game[i] * row[j] + game[4 + i] * row[4 + j] + game[8 + i] * row[8 + j];
        a[i * 4 + 3] = game[i] * (row[3] - game[3]) + game[4 + i] * (row[7] - game[7]) + game[8 + i] * (row[11] - game[11]);
    }
    for (int i = 0; i < 3; ++i) {  // now * a
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = now[i * 4] * a[j] + now[i * 4 + 1] * a[4 + j] + now[i * 4 + 2] * a[8 + j];
        out[i * 4 + 3] += now[i * 4 + 3];
    }
}

// Part of the way from `game` to `now` (both rigid 3x4 row-major): the correction now * game^-1 scaled by w in
// [0, 1] - its rotation by angle, the move of `pivot` by length - so the bone turns about the pivot (her shoulder)
// on the way. (Scaling about the world origin, 500+ m away, would swing the arm through the city.) PLAYTEST-7c:
// in ledge hangs her arms fade to the game's pose. w = 1 leaves now, w = 0 gives game.
inline void blend(const float* game, float* now, vm::Vec3 pivot, float w) {
    if (w >= 1.f) return;
    float rc[3][3];  // R_now * R_game^T
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) rc[i][j] = now[i * 4] * game[j * 4] + now[i * 4 + 1] * game[j * 4 + 1] + now[i * 4 + 2] * game[j * 4 + 2];
    vm::LinearTransform lt{};  // basis = rc's columns
    for (int c = 0; c < 3; ++c) {
        lt.right[c] = rc[c][0];
        lt.up[c] = rc[c][1];
        lt.back[c] = rc[c][2];
    }
    vm::Quat q = vm::quat_of(lt);
    if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
    const float angle = 2.f * std::acos(std::fmin(1.f, q.w));
    const vm::Quat qw = angle > 1e-6f ? vm::Quat::axis_angle({q.x, q.y, q.z}, angle * (w > 0 ? w : 0)) : vm::Quat{};
    // Where the full correction takes the pivot: C(p) = R_c (p - t_game) + t_now, with t_game/t_now the translations.
    const vm::Vec3 tg{game[3], game[7], game[11]}, tn{now[3], now[7], now[11]}, rel = pivot - tg;
    const vm::Vec3 moved{rc[0][0] * rel.x + rc[0][1] * rel.y + rc[0][2] * rel.z + tn.x,
                         rc[1][0] * rel.x + rc[1][1] * rel.y + rc[1][2] * rel.z + tn.y,
                         rc[2][0] * rel.x + rc[2][1] * rel.y + rc[2][2] * rel.z + tn.z};
    const float k = w > 0 ? w : 0;
    const Rigid cw{qw, pivot + (moved - pivot) * k - qw.rotate(pivot)};
    for (int i = 0; i < 12; ++i) now[i] = game[i];
    apply(cw, now);
}

}  // namespace mecvr::arm_ik
