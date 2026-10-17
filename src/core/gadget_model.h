#pragma once
// Gadgets by hand (Stage 6D, Q-604: gesture-fired, the game keeps aiming by the view = head). Pure, unit-tested.
//
// MAG Rope = a throw: a fast hand movement (relative to the head) more than ~45 deg upwards that ends above
// the head. It holds Up actions (the game's MAG Rope button, LB) until that hand comes back below the eyes:
// raise to swing, drop to let go. A throw that starts like a punch cancels the punch (punches go out 100 ms
// late, D-039); a hook just before blocks the throw.
// Disruptor = an open-hand push: a straight punch with the hand held fully open (no finger on the trigger,
// thumb off, grip released) for a moment first; B is then held while the hand stays open.
// Vectors are in the recentred tracking frame (metres, seconds), y up.
#include "core/vmath.h"

namespace mecvr::gadget_model {

// Finger values as hand_pose::Fingers (0 = open .. 1 = closed, thumb 1 = touching).
// PLAYTEST-6h: "grip loose" (others < 0.35) matched relaxed fists in fights; the grip has to be let go.
inline bool open_hand(float index, float others, float thumb) { return index < 0.1f && others < 0.2f && thumb < 0.2f; }
constexpr float kOpenFor = 0.3f;  // s the hand has to be open before a push counts
// The game's Disruptor charges while B is held (user, PLAYTEST-6j): held from the push while the hand stays open.
constexpr unsigned kDisruptMinMs = 1500, kDisruptMaxMs = 5000;

struct Throw {
    // Tunables. PLAYTEST-6h: chin height / 30 deg / release at the shoulder fired on hooks from a raised guard
    // and then held Up (and swallowed that hand's punches) for up to 4 s, as a guard never drops that low.
    float speed = 2.5f;       // m/s, like a punch
    float up = 0.7f;          // direction's upward part: more than ~45 deg up
    float above = 0.08f;      // m above the eyes: the hand has to get above the head
    float release = -0.05f;   // m: hand back below the eyes (a fighting guard) = let go
    float quiet = 0.4f;       // s: no throw this soon after a punch from the same hand
    float rearm = 1.0f;       // m/s
    float min_hold = 0.15f, max_hold = 3.0f;  // s
    float smooth = 0.025f;    // s: velocity smoothing

    // State
    vm::Vec3 last_off, vel;
    bool primed = false, armed = true, holding = false;
    float held = 0, since_punch = 1e9f;

    // A hook from this hand was recognised (its swing can't also be a throw).
    void note_punch() { since_punch = 0; }

    // off: hand position minus head position; dt: s since last call. Returns true on the frame a throw is
    // recognised; `holding` says whether Up actions should be held now.
    bool update(vm::Vec3 off, float dt) {
        since_punch += dt;
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
        if (!armed || holding || s < speed || since_punch < quiet) return false;
        if (vel.y < up * s || off.y < above) return false;
        armed = false;
        holding = true;
        held = 0;
        return true;
    }
};

}  // namespace mecvr::gadget_model
