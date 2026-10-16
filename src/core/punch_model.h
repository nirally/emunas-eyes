#pragma once
// Physical punching (Stage 6C, D-039). Pure, unit-tested.
//
// A punch is a fast hand movement away from the head, towards the front: the hand's velocity relative to the
// head (so walking, room-scale steps and Faith's own motion don't count) above `speed`, pointing within
// ~50 deg of the view direction (straight) or across it (a hook), with the hand in front of the body. One
// punch per swing: the hand has to slow down below `rearm` before the next one counts, and punches are at
// least `min_gap` apart. Vectors are in the recentred tracking frame (metres, seconds).
#include "core/vmath.h"

namespace mecvr::punch_model {

struct Hand {
    enum Kind { kNone = 0, kStraight = 1, kHook = 2 };

    // Tunables
    float speed = 2.5f;      // m/s: slower movements are never punches (reaching for things is < 2 m/s)
    float rearm = 1.0f;      // m/s: below this, the hand is ready for the next punch
    float min_gap = 0.25f;   // s: at most four punches a second per hand
    float smooth = 0.025f;   // s: velocity smoothing (tracking jitter at 90 Hz)

    // State
    vm::Vec3 last_off, vel;
    bool primed = false, armed = true;
    float since = 1e9f;      // s since this hand's last punch

    // off: hand position minus head position; fwd: the head's view direction (unit); dt: s since last call.
    // Returns kStraight or kHook on the frame a punch is recognised (PLAYTEST-6a: hook = heavy), else kNone.
    int update(vm::Vec3 off, vm::Vec3 fwd, float dt) {
        since += dt;
        if (!primed || dt <= 0 || dt > 0.1f) {  // first sample, or tracking came back after a gap
            primed = true;
            last_off = off;
            vel = {};
            return kNone;
        }
        const vm::Vec3 v = (off - last_off) * (1.f / dt);
        last_off = off;
        const float a = dt / (smooth + dt);
        vel = vel + (v - vel) * a;
        const float s = vel.length();
        if (s < rearm) armed = true;
        if (!armed || s < speed || since < min_gap) return kNone;
        const vm::Vec3 dir = vel * (1.f / s);
        const vm::Vec3 out = off.normalized();
        const bool in_front = out.dot(fwd) > 0.2f;       // hand ahead of the body, not at the side or behind
        const bool forward = dir.dot(fwd) > 0.64f;        // within ~50 deg of where you look
        const bool away = dir.dot(out) > 0.2f;            // moving away from the head (not pulling back)
        const bool hook = std::fabs(dir.dot(fwd)) < 0.64f && dir.dot(out) > -0.3f &&
                          std::fabs(dir.y) < 0.7f;        // across the front, not pulling back, not up/down
        if (!in_front) return kNone;
        const int kind = forward && away ? kStraight : hook ? kHook : kNone;
        if (kind == kNone) return kNone;
        armed = false;
        since = 0;
        return kind;
    }
};

}  // namespace mecvr::punch_model
