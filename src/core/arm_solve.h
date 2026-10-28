#pragma once
// Stage 7A: Faith's arms from her own skinning matrices (docs/recon/STAGE7-RECON.md). Pure, unit-tested.
//
// Every bone matrix S maps the bind pose to the world (S = M * B^-1, rigid; 3x4 row-major, as the game uploads
// it). Two bones that meet at a joint share one bind-space point x with S_a(x) = S_b(x) in every frame:
// (R_a - R_b) x = t_b - t_a, solved by least squares over the frames (`Learner` keeps the normal equations per
// pair, so frames aren't stored). Pairs that fit within 2 mm and really rotate against each other give joint
// points; points within 1.5 cm are one joint. A segment bone touches both of its end joints. Then, per arm:
//   wrist    = a joint with 4+ joints within 12 cm sharing a bone with it (the finger roots),
//   elbow    = the farthest joint 15-40 cm from the wrist sharing a bone with it,
//   shoulder = the farthest joint 15-40 cm from the elbow sharing a bone with it, on the far side from the wrist.
// Each bone rides on the segment of the most proximal joint it touches (bind space): beyond the elbow -> upper
// arm, beyond the wrist -> forearm, else the hand (fingers included); a bone touching a joint further from the
// elbow than the shoulder is (collarbone, chest) is left alone. A bone that touches no joint (fits 2-10 mm only)
// rides with its best-fitting partner.
#include <cmath>
#include <vector>

#include "core/vmath.h"

namespace mecvr::arm_solve {

struct Bone {  // 3x4 row-major: world = R * x + t (rows: r00 r01 r02 t0 / r10 ...)
    float m[12];
    vm::Vec3 apply(vm::Vec3 x) const {
        return {m[0] * x.x + m[1] * x.y + m[2] * x.z + m[3], m[4] * x.x + m[5] * x.y + m[6] * x.z + m[7],
                m[8] * x.x + m[9] * x.y + m[10] * x.z + m[11]};
    }
};

enum Segment { kNone = -1, kUpper = 0, kFore = 1, kHand = 2 };

struct Arm {
    vm::Vec3 shoulder, elbow, wrist;  // bind space; world = S(x) of any bone touching that joint
    int shoulder_bone = -1, elbow_bone = -1, wrist_bone = -1;  // a bone that touches each joint (to place it)
    int fore_bone = -1;  // the forearm's own bone (keeps both its ends best), for the hand-rider check (log)
    std::vector<vm::Vec3> hand_joints;  // bind space: the other joints hand bones touch (finger roots, knuckles)
    int hand_bone = -1;                 // a hand bone touching the wrist (the hand's rotation)
};

struct Result {
    bool ok = false;
    const char* why = "not solved";
    int joints = 0, wrists = 0, chains = 0;  // how far the solve got (log)
    struct Found { vm::Vec3 shoulder, elbow, wrist; };
    std::vector<Found> found;  // every arm chain found (bind space; log)
    Arm arm[2];
    std::vector<int> side;     // per bone: arm 0/1, -1 = not an arm bone
    std::vector<int> segment;  // per bone: Segment
};

// Arms a solve found that can't be Faith's (desktop: with her arms nearly still, a solve gave upper arms 0.37 and
// 0.40 m, forearms 0.31 and 0.20 m and 2 hand bones, and was taken): both arms of a human length and alike, each
// with a hand of several bones. Her arms: upper 0.282 m, forearm 0.245-0.267 m, 22-26 hand bones. Null if fine.
inline const char* implausible(const Result& r) {
    float up[2], fore[2];
    for (int a = 0; a < 2; ++a) {
        up[a] = (r.arm[a].shoulder - r.arm[a].elbow).length();
        fore[a] = (r.arm[a].elbow - r.arm[a].wrist).length();
        if (up[a] < 0.20f || up[a] > 0.36f || fore[a] < 0.18f || fore[a] > 0.32f) return "arm lengths not human";
        int hand = 0;
        for (size_t b = 0; b < r.side.size(); ++b) hand += r.side[b] == a && r.segment[b] == kHand;
        if (hand < 6) return "too few hand bones";
        // PLAYTEST-10a (BUG-038): of 3 chains the best-mirrored pair took a right "wrist" 2 cm up the forearm that no
        // hand bone turns on (her hand kept the game's turn). Also before the jacket work (desktop, raised hands).
        if (r.arm[a].hand_bone < 0) return "a wrist no hand bone turns on";
    }
    if (std::fabs(up[0] - up[1]) > 0.03f || std::fabs(fore[0] - fore[1]) > 0.03f) return "the two arms differ";
    return nullptr;
}

class Learner {
public:
    // Tunables
    double fit_m = 0.002;      // rms residual for a joint pair
    double motion = 1e-4;      // per-frame smallest pivot: the two bones must rotate against each other
    float merge_m = 0.015f;    // joint points closer than this are one joint
    double partner_m = 0.010;  // a bone touching no joint rides with a partner it fits this well
    bool ride_check = false;   // D-061: arm bones must keep a segment end (her jacket's merged pieces only; D-072)

    void reset(int bones) {
        n_ = bones;
        frames_ = 0;
        acc_.assign(size_t(bones) * bones, Acc{});
    }
    int bones() const { return n_; }
    int frames() const { return frames_; }

    // One frame of all n bones.
    void add(const Bone* b) {
        for (int i = 0; i < n_; ++i)
            for (int j = i + 1; j < n_; ++j) {
                Acc& a = acc_[size_t(i) * n_ + j];
                for (int r = 0; r < 3; ++r) {
                    const double row[3] = {double(b[i].m[r * 4]) - b[j].m[r * 4], double(b[i].m[r * 4 + 1]) - b[j].m[r * 4 + 1],
                                           double(b[i].m[r * 4 + 2]) - b[j].m[r * 4 + 2]};
                    const double rhs = double(b[j].m[r * 4 + 3]) - b[i].m[r * 4 + 3];
                    a.btb += rhs * rhs;
                    for (int p = 0; p < 3; ++p) {
                        a.atb[p] += row[p] * rhs;
                        for (int q = p; q < 3; ++q) a.ata[p][q] += row[p] * row[q];
                    }
                }
            }
        ++frames_;
    }

    Result solve() const;
    // The bind-space point bones i and j share over the frames so far, and the fit's rms (m). False if they don't
    // turn against each other (no point is determined). 7D: a finger's first knuckle, from the hand bone.
    bool joint(int i, int j, vm::Vec3& x, double& rms) const;
    // How far apart (rms, m) bones i and j put the bind-space point x over the frames so far.
    double apart(int i, int j, vm::Vec3 x) const;

private:
    struct Acc { double ata[3][3] = {}, atb[3] = {}, btb = 0; };
    int n_ = 0, frames_ = 0;
    std::vector<Acc> acc_;
};

namespace detail {
// Symmetric 3x3 (upper triangle filled) solve with partial pivoting; false if a pivot is below min_pivot.
inline bool solve3(const double (&u)[3][3], const double (&b)[3], double min_pivot, double (&x)[3]) {
    double m[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m[i][j] = i <= j ? u[i][j] : u[j][i];
        m[i][3] = b[i];
    }
    for (int c = 0; c < 3; ++c) {
        int p = c;
        for (int r = c + 1; r < 3; ++r)
            if (std::fabs(m[r][c]) > std::fabs(m[p][c])) p = r;
        for (int j = 0; j < 4; ++j) { const double t = m[c][j]; m[c][j] = m[p][j]; m[p][j] = t; }
        if (std::fabs(m[c][c]) < min_pivot) return false;
        for (int r = 0; r < 3; ++r)
            if (r != c) {
                const double f = m[r][c] / m[c][c];
                for (int j = c; j < 4; ++j) m[r][j] -= f * m[c][j];
            }
    }
    for (int i = 0; i < 3; ++i) x[i] = m[i][3] / m[i][i];
    return true;
}
}  // namespace detail

inline bool Learner::joint(int i, int j, vm::Vec3& x, double& rms) const {
    if (i == j || i < 0 || j < 0 || i >= n_ || j >= n_ || frames_ == 0) return false;
    if (i > j) { const int t = i; i = j; j = t; }
    const Acc& a = acc_[size_t(i) * n_ + j];
    double p[3];
    if (!detail::solve3(a.ata, a.atb, motion * frames_, p)) return false;
    double r2 = a.btb;
    for (int u = 0; u < 3; ++u) {
        r2 -= 2 * p[u] * a.atb[u];
        for (int v = 0; v < 3; ++v) r2 += p[u] * (u <= v ? a.ata[u][v] : a.ata[v][u]) * p[v];
    }
    rms = std::sqrt(std::fmax(r2, 0.0) / frames_);
    x = {float(p[0]), float(p[1]), float(p[2])};
    return true;
}

inline double Learner::apart(int i, int j, vm::Vec3 x) const {
    if (i == j || i < 0 || j < 0 || i >= n_ || j >= n_ || frames_ == 0) return 0;
    if (i > j) { const int t = i; i = j; j = t; }  // the residual's square doesn't change with the order
    const Acc& a = acc_[size_t(i) * n_ + j];
    const double p[3] = {x.x, x.y, x.z};
    double r2 = a.btb;
    for (int u = 0; u < 3; ++u) {
        r2 -= 2 * p[u] * a.atb[u];
        for (int v = 0; v < 3; ++v) r2 += p[u] * (u <= v ? a.ata[u][v] : a.ata[v][u]) * p[v];
    }
    return std::sqrt(std::fmax(r2, 0.0) / frames_);
}

inline Result Learner::solve() const {
    using vm::Vec3;
    Result res;
    res.side.assign(n_, -1);
    res.segment.assign(n_, kNone);
    if (frames_ < 30) { res.why = "too few frames"; return res; }

    // Joint points and which bones touch them.
    struct Joint { Vec3 x; std::vector<int> bones; };
    std::vector<Joint> joints;
    std::vector<int> best(n_, -1);
    std::vector<double> best_rms(n_, partner_m);
    auto touch = [&](Joint& j, int b) {
        for (int k : j.bones) if (k == b) return;
        j.bones.push_back(b);
    };
    for (int i = 0; i < n_; ++i)
        for (int j = i + 1; j < n_; ++j) {
            const Acc& a = acc_[size_t(i) * n_ + j];
            double x[3];
            if (!detail::solve3(a.ata, a.atb, motion * frames_, x)) continue;
            double r2 = a.btb;
            for (int p = 0; p < 3; ++p) {
                r2 -= 2 * x[p] * a.atb[p];
                for (int q = 0; q < 3; ++q) r2 += x[p] * (p <= q ? a.ata[p][q] : a.ata[q][p]) * x[q];
            }
            const double rms = std::sqrt(std::fmax(r2, 0.0) / frames_);
            for (int s = 0; s < 2; ++s) {  // each bone's best partner, for bones that touch no joint (below)
                const int me = s ? j : i, other = s ? i : j;
                if (rms < best_rms[me]) { best_rms[me] = rms; best[me] = other; }
            }
            if (rms > fit_m) continue;
            const Vec3 pt{float(x[0]), float(x[1]), float(x[2])};
            Joint* hit = nullptr;
            for (Joint& jt : joints)
                if ((jt.x - pt).length() < merge_m) { hit = &jt; break; }
            if (!hit) { joints.push_back({pt, {}}); hit = &joints.back(); }
            touch(*hit, i);
            touch(*hit, j);
        }
    const int J = int(joints.size());
    res.joints = J;
    std::vector<std::vector<int>> of_bone(n_);  // joints each bone touches
    for (int k = 0; k < J; ++k)
        for (int b : joints[k].bones) of_bone[b].push_back(k);
    auto linked = [&](int a, int b) {  // a bone touches both joints
        for (int x : joints[a].bones)
            for (int y : joints[b].bones)
                if (x == y) return true;
        return false;
    };
    auto dist = [&](int a, int b) { return (joints[a].x - joints[b].x).length(); };

    // Wrists: joints with 4+ linked joints within 12 cm (finger roots, hand bones).
    std::vector<int> wrists;
    for (int k = 0; k < J; ++k) {
        int close = 0;  // ("near" is a macro in windows.h)
        for (int o = 0; o < J; ++o)
            if (o != k && dist(k, o) < 0.12f && linked(k, o)) ++close;
        if (close >= 4) wrists.push_back(k);
    }
    // A hand has several joints that qualify; keep the one each arm chain starts from (it has an elbow).
    struct Chain { int w, e, s; };
    std::vector<Chain> chains;
    for (int w : wrists) {
        int e = -1;
        for (int o = 0; o < J; ++o)
            if (o != w && linked(w, o) && dist(w, o) > 0.15f && dist(w, o) < 0.40f && (e < 0 || dist(w, o) > dist(w, e))) e = o;
        if (e < 0) continue;
        int s = -1;
        for (int o = 0; o < J; ++o)
            if (o != e && o != w && linked(e, o) && dist(e, o) > 0.15f && dist(e, o) < 0.40f &&
                dist(w, o) > dist(w, e) + 0.10f && (s < 0 || dist(e, o) > dist(e, s)))
                s = o;
        if (s < 0) continue;
        bool dup = false;  // another wrist candidate of the same arm (same elbow, or the same shoulder)
        for (Chain& c : chains)
            if (c.e == e || dist(c.s, s) < 0.05f) {
                dup = true;
                // keep the longer chain (the real wrist and elbow; the others are hand, finger or twist joints)
                if (dist(w, e) + dist(e, s) > dist(c.w, c.e) + dist(c.e, c.s)) c = {w, e, s};
            }
        if (!dup) chains.push_back({w, e, s});
    }
    res.wrists = int(wrists.size());
    res.chains = int(chains.size());
    for (const Chain& c : chains)  // for the log
        if (res.found.size() < 6) res.found.push_back({joints[c.s].x, joints[c.e].x, joints[c.w].x});
    // Faith's two arms mirror each other: equal lengths, and the elbows' and wrists' midpoints on the shoulders'
    // midline. Other chains (one ran backwards from the breastbone through the collarbone: live, try 1) don't
    // pair up. Keep the best-mirrored pair.
    if (chains.size() > 2) {
        double best_score = 1e9;
        size_t bi = 0, bj = 1;
        for (size_t i = 0; i < chains.size(); ++i)
            for (size_t j = i + 1; j < chains.size(); ++j) {
                const Chain &a = chains[i], &b = chains[j];
                const Vec3 lat = (joints[a.s].x - joints[b.s].x).normalized();
                const Vec3 ms = (joints[a.s].x + joints[b.s].x) * 0.5f, me = (joints[a.e].x + joints[b.e].x) * 0.5f,
                           mw = (joints[a.w].x + joints[b.w].x) * 0.5f;
                const double score = std::fabs((me - ms).dot(lat)) + std::fabs((mw - ms).dot(lat)) +
                                     std::fabs(dist(a.s, a.e) - dist(b.s, b.e)) + std::fabs(dist(a.e, a.w) - dist(b.e, b.w));
                if (score < best_score) { best_score = score; bi = i; bj = j; }
            }
        if (best_score > 0.05) { res.why = "no two arm chains mirror each other"; return res; }
        chains = {chains[bi], chains[bj]};
    }
    if (chains.size() != 2) { res.why = chains.size() < 2 ? "fewer than two arms found" : "more than two arms found"; return res; }

    std::vector<bool> not_arm(n_, false);
    for (int a = 0; a < 2; ++a) {
        const Chain& c = chains[a];
        Arm& arm = res.arm[a];
        arm.wrist = joints[c.w].x;
        arm.elbow = joints[c.e].x;
        arm.shoulder = joints[c.s].x;
        arm.wrist_bone = joints[c.w].bones[0];
        arm.elbow_bone = joints[c.e].bones[0];
        arm.shoulder_bone = joints[c.s].bones[0];
        const Vec3 up_dir = (arm.shoulder - arm.elbow).normalized(), fore_dir = (arm.elbow - arm.wrist).normalized();
        const float reach = (arm.shoulder - arm.elbow).length() + 0.03f;
        for (int b = 0; b < n_; ++b) {
            if (of_bone[b].empty()) continue;
            bool body = false, upper = false, fore = false, near_hand = false;
            for (int jk : of_bone[b]) {
                const Vec3 p = joints[jk].x;
                if ((p - arm.elbow).length() > reach && (p - arm.elbow).dot(up_dir) > 0) body = true;  // past the shoulder
                if ((p - arm.elbow).dot(up_dir) > 0.02f) upper = true;
                else if ((p - arm.wrist).dot(fore_dir) > 0.02f) fore = true;
                else if ((p - arm.wrist).length() < 0.25f) near_hand = true;
            }
            if (body) continue;
            const int seg = upper ? kUpper : fore ? kFore : near_hand ? kHand : kNone;
            if (seg == kNone) continue;
            if (res.side[b] >= 0 && res.side[b] != a) { res.side[b] = -2; continue; }  // claimed by both arms: leave it
            res.side[b] = a;
            res.segment[b] = seg;
        }
        // A bone that pivots only at the shoulder rides with the upper arm only if it keeps her elbow where the
        // upper arm does (a twist bone turns about the line through it). Her collarbone pivots there too: when the
        // piece shows no joint of it past the shoulder (live, PLAYTEST-7g: 28 joints, upper arms of 5 bones) it took
        // the arm's turns and swung her whole body with the arms.
        int main_upper = -1;  // the upper arm itself: touches the shoulder and the elbow
        for (int x : joints[c.s].bones)
            for (int y : joints[c.e].bones)
                if (x == y && res.side[x] == a && res.segment[x] == kUpper) main_upper = x;
        if (main_upper >= 0)
            for (int b = 0; b < n_; ++b) {
                if (b == main_upper || res.side[b] != a || res.segment[b] != kUpper) continue;
                bool only_shoulder = true;
                for (int jk : of_bone[b]) only_shoulder &= (joints[jk].x - arm.shoulder).length() < 0.03f;
                if (only_shoulder && apart(b, main_upper, arm.elbow) > 0.02) not_arm[b] = true;
            }
        // PLAYTEST-8p (her jacket, the arms learned over all her pieces: legs included, "the legs is gliching like
        // crazy"): a hip, leg or hem joint can lie beside the arm in bind space, so leg bones were put on the forearm and
        // swung with it (desktop recording: 10 of them, keeping the elbow and wrist 24-67 cm off). An upper arm or
        // forearm bone must keep one of its segment's ends within kRideM of where the segment's own bone does (her
        // sleeves' cloth bones: 5 cm).
        constexpr double kRideM = 0.10;
        int main_fore = -1;  // the forearm itself: touches the elbow and the wrist
        for (int x : joints[c.e].bones)
            for (int y : joints[c.w].bones)
                if (x == y && res.side[x] == a && res.segment[x] == kFore) main_fore = x;
        for (int b = 0; b < n_ && ride_check; ++b) {
            if (res.side[b] != a || b == main_upper || b == main_fore) continue;
            if (res.segment[b] == kUpper && main_upper >= 0 &&
                std::fmin(apart(b, main_upper, arm.shoulder), apart(b, main_upper, arm.elbow)) > kRideM)
                not_arm[b] = true;
            if (res.segment[b] == kFore && main_fore >= 0 &&
                std::fmin(apart(b, main_fore, arm.elbow), apart(b, main_fore, arm.wrist)) > kRideM)
                not_arm[b] = true;
        }
    }
    for (int b = 0; b < n_; ++b)
        if (not_arm[b]) { res.side[b] = -1; res.segment[b] = kNone; }
    for (int b = 0; b < n_; ++b)
        if (res.side[b] == -2) { res.side[b] = -1; res.segment[b] = kNone; }
    for (int a = 0; a < 2; ++a)
        for (int k = 0; k < J; ++k) {
            if ((joints[k].x - res.arm[a].wrist).length() < 0.005f) {  // the wrist: the hand bone that turns on it
                for (int b : joints[k].bones)
                    if (res.side[b] == a && res.segment[b] == kHand && res.arm[a].hand_bone < 0) res.arm[a].hand_bone = b;
                continue;
            }
            for (int b : joints[k].bones)
                if (res.side[b] == a && res.segment[b] == kHand) { res.arm[a].hand_joints.push_back(joints[k].x); break; }
        }
    // 2026-10-28 (her right glove torn at the wrist, "faith's right arms it looks broken"): that solve put bones 4 and 5
    // on the forearm (8 forearm bones, 23 hand; good solves 6 and 25) - they touched a stray joint 2+ cm up the forearm,
    // but ride with the hand, so the glove's cuff stayed with the forearm while the rest turned with the hand. A forearm
    // bone that carries a point in the palm (on the forearm's line, 8 cm past the wrist: twist bones turn about it) far
    // closer to where the hand bone does than the forearm does goes on the hand. Recorded 81-bone sessions: forearm
    // bones 0 mm from the forearm and 17-19 from the hand, cloth bones ~equal both ways (tools/forearm_check).
    for (int a = 0; a < 2; ++a) {
        const Arm& arm = res.arm[a];
        if (arm.hand_bone < 0 || arm.wrist_bone < 0 || arm.elbow_bone < 0) continue;
        int main_fore = -1;
        double best = 1e9;
        for (int b = 0; b < n_; ++b)
            if (res.side[b] == a && res.segment[b] == kFore) {
                const double d = apart(b, arm.elbow_bone, arm.elbow) + apart(b, arm.wrist_bone, arm.wrist);
                if (d < best) { best = d; main_fore = b; }
            }
        if (main_fore < 0) continue;
        res.arm[a].fore_bone = main_fore;
        const Vec3 palm = arm.wrist + (arm.wrist - arm.elbow).normalized() * 0.08f;
        for (int b = 0; b < n_; ++b) {
            if (b == main_fore || res.side[b] != a || res.segment[b] != kFore) continue;
            const double to_fore = apart(b, main_fore, palm), to_hand = apart(b, arm.hand_bone, palm);
            if (to_fore > 0.005 && to_hand * 3 < to_fore) res.segment[b] = kHand;
        }
    }
    // Bones that touch no joint (real fingertips fit only 2-5 mm, STAGE7-RECON) ride with their best partner,
    // repeated so chains of them follow.
    for (bool changed = true; changed;) {
        changed = false;
        for (int b = 0; b < n_; ++b)
            if (of_bone[b].empty() && res.side[b] < 0 && best[b] >= 0 && res.side[best[b]] >= 0) {
                res.side[b] = res.side[best[b]];
                res.segment[b] = res.segment[best[b]];
                changed = true;
            }
    }
    res.ok = true;
    res.why = "ok";
    if (const char* bad = implausible(res)) {
        res.ok = false;
        res.why = bad;
    }
    return res;
}

// 7C (PLAYTEST-7b: two fingers of her left hand kept the game's pose - stretched skin; that session placed 22 of
// its hand bones, earlier ones 25-26): learning goes on after the IK starts, and a later solve can place bones the
// first one couldn't (a finger the game hardly moved in the first seconds). Bones `cur` left off the arms that
// `next` puts on a segment join it; nothing placed changes. The arms are matched by the bones both placed (joint
// points move a few cm between solves: another hand joint can win as the wrist) and must agree on at least
// `agree` of them (arm and segment), else nothing is taken. Returns the bones taken, -1 on a mismatch.
inline int adopt_new_bones(Result& cur, const Result& next, float agree = 0.9f, float* agreed = nullptr) {
    if (!cur.ok || !next.ok || cur.side.size() != next.side.size()) return -1;
    int same = 0, swapped = 0, both = 0;  // bones on an arm in both: same arm number / the other one
    for (size_t b = 0; b < cur.side.size(); ++b)
        if (cur.side[b] >= 0 && next.side[b] >= 0) {
            ++both;
            if (cur.segment[b] != next.segment[b]) continue;
            (cur.side[b] == next.side[b] ? same : swapped)++;
        }
    const bool swap = swapped > same;
    if (agreed) *agreed = both ? float(swap ? swapped : same) / float(both) : 0.f;
    if (both == 0 || float(swap ? swapped : same) < agree * float(both)) return -1;
    int taken = 0;
    for (size_t b = 0; b < cur.side.size(); ++b) {
        if (cur.side[b] >= 0 || next.side[b] < 0 || next.segment[b] == kNone) continue;
        cur.side[b] = swap ? 1 - next.side[b] : next.side[b];
        cur.segment[b] = next.segment[b];
        ++taken;
    }
    return taken;
}

// Whether her marked pieces this frame (bones per piece) still fit the arms as learned: over one piece, that piece's
// size is among them; over her pieces (`multi`), one of their sizes is and none is big enough for both arms on its own
// (one_piece_bones). PLAYTEST-10b3 (BUG-035, "after I pressed abort mission and got back to freeroam the hands did not
// worked"): free roam's pieces (81 4 3 ...) share the 3-bone size with her jacket's (3 18 26 42 51), so any size
// matching was never "changed" and the arms weren't learned again.
inline bool outfit_known(const std::vector<int>& bones, const std::vector<int>& learned, bool multi, int one_piece_bones) {
    bool any = false;
    for (int b : bones) {
        if (multi && b >= one_piece_bones) return false;
        for (int l : learned) any |= b == l;
    }
    return any;
}

// PLAYTEST-8n (her jacket, first mission: "not having hands that follow the controllers"): there her arms are in no
// one piece - 5 pieces of 3, 51, 26, 42 and 18 bones, hands in the 51, sleeves in the 26 and 42, forearm bones in all
// five - and each piece carries its own copies of the skeleton's matrices. One skeleton from them: a piece's bone that
// equals an earlier piece's bone in every frame given is that bone (desktop recording: 140 rows -> 95 bones, and the
// solve on them found both arms as without the jacket, upper 0.283 m, forearm 0.245 m).
// frames[f][p] = piece p's bones in frame f (bones[p] of them).
struct Merge {
    std::vector<std::pair<int, int>> src;  // per merged bone: (piece, bone) it is read from
    std::vector<std::vector<int>> of;      // per piece, per bone: its merged bone
};
inline Merge merge_pieces(const std::vector<std::vector<const Bone*>>& frames, const std::vector<int>& bones) {
    Merge m;
    m.of.resize(bones.size());
    auto same = [&](int p, int b, int q, int c) {
        for (const auto& f : frames)
            for (int k = 0; k < 12; ++k)
                if (f[p][b].m[k] != f[q][c].m[k]) return false;
        return true;
    };
    for (int p = 0; p < int(bones.size()); ++p)
        for (int b = 0; b < bones[p]; ++b) {
            int hit = -1;
            for (int u = 0; u < int(m.src.size()) && hit < 0; ++u)
                if (same(p, b, m.src[u].first, m.src[u].second)) hit = u;
            if (hit < 0) {
                hit = int(m.src.size());
                m.src.push_back({p, b});
            }
            m.of[p].push_back(hit);
        }
    return m;
}

}  // namespace mecvr::arm_solve
