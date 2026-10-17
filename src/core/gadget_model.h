#pragma once
// Gadgets by hand (Stage 6D, Q-604: gesture-fired, the game keeps aiming by the view = head). Pure, unit-tested.
//
// MAG Rope = a throw: a fast hand movement (relative to the head) more than ~30 deg upwards that ends above
// chin height. It holds Up actions (the game's MAG Rope button, LB) until that arm comes down below the
// shoulder: raise to swing, drop to let go. A throw that starts like a punch cancels the punch (punches go
// out 100 ms late, D-039).
// Disruptor = an open-hand push: a straight punch with no finger on the trigger, thumb off and grip loose.
// Vectors are in the recentred tracking frame (metres, seconds), y up.
#include "core/vmath.h"

namespace mecvr::gadget_model {

// Finger values as hand_pose::Fingers (0 = open .. 1 = closed, thumb 1 = touching).
inline bool open_hand(float index, float others, float thumb) { return index < 0.1f && others < 0.35f && thumb < 0.2f; }

struct Throw {
    // Tunables
    float speed = 2.5f;       // m/s, like a punch
    float up = 0.5f;          // direction's upward part: more than ~30 deg up
    float above = -0.10f;     // m below the eyes: the hand has to get above chin height
    float release = -0.18f;   // m below the eyes: arm down below the shoulder = let go
    float rearm = 1.0f;       // m/s
    float min_hold = 0.15f, max_hold = 4.0f;  // s
    float smooth = 0.025f;    // s: velocity smoothing

    // State
    vm::Vec3 last_off, vel;
    bool primed = false, armed = true, holding = false;
    float held = 0;

    // off: hand position minus head position; dt: s since last call. Returns true on the frame a throw is
    // recognised; `holding` says whether Up actions should be held now.
    bool update(vm::Vec3 off, float dt) {
        if (holding) {
            held += dt;
            if (held >= max_hold || (held >= min_hold && off.y < release)) holding = false;
        }
        if (!primed || dt <= 0 || dt > 0.1f) {
            primed = true;
            last_off = off;
            vel = {};
            return false;
        }
        const vm::Vec3 v = (off - last_off) * (1.f / dt);
        last_off = off;
        vel = vel + (v - vel) * (dt / (smooth + dt));
        const float s = vel.length();
        if (s < rearm) armed = true;
        if (!armed || holding || s < speed) return false;
        if (vel.y < up * s || off.y < above) return false;
        armed = false;
        holding = true;
        held = 0;
        return true;
    }
};

}  // namespace mecvr::gadget_model
