#pragma once
// Stage 8 (BUG-030, PLAYTEST-8e/8f: on the train in The Shard "the whole screen shaked a lot"). Pure, unit-tested.
//
// With comfort.game_camera_motion on, the game camera's rotation against its heading (rolls, tilts, dips, and with
// camera.pitch_source = game the stick's look pitch) is put on top of the head. On the train roof the game shakes
// that camera (8f log: its per-tick rotation change 2.2-3.0 deg avg, 6.5 max, against 0.00 running on the desktop).
// A One Euro filter on the rotation: small back-and-forth movement is smoothed (its speed, filtered as a vector,
// cancels out, so the cutoff stays low), a steady movement (a roll, the stick) raises the cutoff and passes with a
// lag under ~1.6 deg. A jump past snap_deg in one step (a camera cut) passes at once.
#include <cmath>

#include "core/vmath.h"

namespace mecvr::shake_filter {

// Rotation vector (axis * angle, radians) of a unit quaternion, shortest way.
inline vm::Vec3 log_q(vm::Quat q) {
    if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
    const float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-7f) return {q.x * 2.f, q.y * 2.f, q.z * 2.f};
    const float a = 2.f * std::atan2(s, q.w);
    return vm::Vec3{q.x, q.y, q.z} * (a / s);
}

inline vm::Quat exp_q(vm::Vec3 v) {
    const float a = std::sqrt(v.dot(v));
    if (a < 1e-7f) return vm::Quat{v.x * 0.5f, v.y * 0.5f, v.z * 0.5f, 1.f}.normalized();
    return vm::Quat::axis_angle(v * (1.f / a), a);
}

inline float alpha(float cutoff_hz, float dt) { return 1.f / (1.f + 1.f / (6.2831853f * cutoff_hz * dt)); }

struct Filter {
    float min_cutoff = 1.5f;  // Hz, when still
    float beta = 0.1f;        // Hz per deg/s of steady speed
    float d_cutoff = 1.0f;    // Hz, for the speed estimate
    float snap_deg = 20.f;    // one step past this: a cut, no smoothing
    vm::Quat out, prev_in;
    vm::Vec3 vel;  // deg/s, filtered
    bool primed = false;
    float last_diff_deg = 0;  // |out vs in| after the last update (log)

    vm::Quat update(vm::Quat in, float dt) {
        in = in.normalized();
        if (!primed || dt <= 0.f || dt > 0.25f) return reset(in);
        const vm::Vec3 step = log_q((prev_in.conjugate() * in).normalized());
        if (std::sqrt(step.dot(step)) * 57.29578f > snap_deg) return reset(in);
        prev_in = in;
        const float ad = alpha(d_cutoff, dt);
        vel = vel + (step * (57.29578f / dt) - vel) * ad;
        const float a = alpha(min_cutoff + beta * std::sqrt(vel.dot(vel)), dt);
        const vm::Vec3 err = log_q((out.conjugate() * in).normalized());
        out = (out * exp_q(err * a)).normalized();
        last_diff_deg = std::sqrt(err.dot(err)) * (1.f - a) * 57.29578f;
        return out;
    }
    vm::Quat reset(vm::Quat in) {
        primed = true;
        out = prev_in = in;
        vel = {};
        last_diff_deg = 0;
        return in;
    }
};

}  // namespace mecvr::shake_filter
