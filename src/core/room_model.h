#pragma once
// Room-scale (Stage 4, D-029, BUG-007). Pure, unit-tested.
//
// The view used to add the whole real head offset (from the recentre point) to Faith's camera, so a
// step in the room put the view in front of her body. Now Faith moves with you: she is commanded to
// move at your head's own velocity plus a catch-up term for the gap left ("offset"), converted to the
// stick deflection that gives that speed in this game. Her movement along the gap is absorbed into an
// anchor, so the view stays on the real head while her body stays under it. Movement the command
// doesn't explain (hits, animations) isn't absorbed. Vectors are horizontal, in the body frame
// (x right, z back; metres), like the head offset.
#include <cmath>

namespace mecvr::room_model {

struct V2 {
    float x = 0, z = 0;
    V2 operator+(V2 o) const { return {x + o.x, z + o.z}; }
    V2 operator-(V2 o) const { return {x - o.x, z - o.z}; }
    V2 operator*(float s) const { return {x * s, z * s}; }
    float len() const { return std::sqrt(x * x + z * z); }
};

struct Stick { float x = 0, y = 0; };  // pad left stick, y + = forward

// Faith's steady speed for a stick deflection (measured PLAYTEST-4e follow-up: big deadzone, steep).
struct SpeedCurve {
    static constexpr int kN = 6;
    float stick[kN] = {0.62f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f};
    float speed[kN] = {0.00f, 0.16f, 0.35f, 0.93f, 1.50f, 3.90f};  // m/s
    float speed_of(float s) const {
        if (s <= stick[0]) return 0;
        for (int i = 1; i < kN; ++i)
            if (s <= stick[i]) return speed[i - 1] + (speed[i] - speed[i - 1]) * (s - stick[i - 1]) / (stick[i] - stick[i - 1]);
        return speed[kN - 1];
    }
    float stick_for(float v) const {
        if (v <= 0) return 0;
        for (int i = 1; i < kN; ++i)
            if (v <= speed[i]) return stick[i - 1] + (stick[i] - stick[i - 1]) * (v - speed[i - 1]) / (speed[i] - speed[i - 1]);
        return stick[kN - 1];
    }
};

struct Model {
    SpeedCurve curve;
    float catch_up = 4.0f;     // 1/s: gap closed per second on top of following the head
    float max_speed = 2.5f;    // m/s: a brisk catch-up at most (a run is 3.9; 3.0 tested: no gain)
    float min_speed = 0.12f;   // m/s: below this, stop (the game's deadzone; she coasts a little)
    float gap_ok = 0.04f;      // m: a gap this small isn't worth moving for
    float vel_smooth = 0.08f;  // s: head velocity smoothing
    float max_offset = 0.50f;  // m: view never further from Faith than this (walls: no walking through)
    float jump_reset = 1.0f;   // m: a head jump this large in one frame = recentre, start fresh

    float coast_s = 1.5f;      // s: keep absorbing along the last command after release (she coasts, slow tail)
    float lead_s = 0.25f;      // s: her momentum carries her about this far ahead (speed x lead): aim short
    float absorb_max = 3.2f;   // m/s: faster than our walking ever gets her (attack lunge, zipline): not ours (PLAYTEST-5m)

    V2 anchor;                 // head position Faith is standing under
    V2 last_head, head_vel, faith_vel;
    V2 cmd_dir;                // body-frame direction of the last walking command
    float since_cmd = 1e9f;    // s since we last commanded walking
    float along_avg = 0, dt_avg = 0;  // short average of her movement along the command (speed check)
    unsigned released = 0;     // how often a fast game move ended the absorbing (log)
    bool walking = false, primed = false;

    // head: real head offset; moved: Faith's displacement since last frame (body frame); user_moving: the
    // player is using the move stick (no correction then); dt: seconds since last frame. Returns the stick
    // to add; view_offset = the horizontal head offset the camera should use.
    Stick update(V2 head, V2 moved, bool user_moving, float dt, V2& view_offset) {
        if (!primed || (head - last_head).len() > jump_reset) {
            primed = true;
            anchor = head;
            last_head = head;
            head_vel = {};
            faith_vel = {};
            walking = false;
            since_cmd = 1e9f;
        }
        if (dt > 0) {
            const V2 v = (head - last_head) * (1.f / dt);
            const float a = dt / (vel_smooth + dt);
            head_vel = head_vel + (v - head_vel) * a;
            faith_vel = faith_vel + (moved * (1.f / dt) - faith_vel) * a;
        }
        last_head = head;
        // Absorb Faith's movement along what we asked for (while walking and while she coasts after), so
        // the view stays exactly on the real head; sideways pushes (hits, animations) still move the view.
        since_cmd += dt;
        if (!user_moving && since_cmd < coast_s) {
            // Both ways along the command's axis: her momentum after a reversal is still ours.
            const float along = moved.x * cmd_dir.x + moved.z * cmd_dir.z;
            // Averaged over a few frames (frame times are coarse, eye sync holds every other frame still).
            along_avg = along_avg * 0.7f + std::fabs(along) * 0.3f;
            dt_avg = dt_avg * 0.7f + dt * 0.3f;
            // Faster than our walking ever gets her: the game moves her itself (attack lunge, zipline) - the
            // view goes with her, and this command is over.
            if (dt_avg > 0 && along_avg > absorb_max * dt_avg) {
                since_cmd = coast_s;
                ++released;
            }
            else anchor = anchor + cmd_dir * along;
        }
        V2 off = head - anchor;
        Stick cmd;
        walking = false;
        if (!user_moving) {
            // Where she'll stop if the stick let go now: aim at the gap minus that coast.
            const V2 gap = off - faith_vel * lead_s;
            V2 want = off.len() > gap_ok ? head_vel + gap * catch_up : head_vel;
            float v = want.len();
            if (v > max_speed) { want = want * (max_speed / v); v = max_speed; }
            if (v >= min_speed) {
                walking = true;
                cmd_dir = want * (1.f / v);
                since_cmd = 0;
                const float s = curve.stick_for(v);
                cmd.x = want.x / v * s;
                cmd.y = -want.z / v * s;  // z back -> stick forward
            }
        }
        // Walls: the view can't run away from Faith; the anchor follows the head beyond max_offset.
        const float l = off.len();
        if (l > max_offset) {
            anchor = head - off * (max_offset / l);
            off = head - anchor;
        }
        view_offset = off;
        return cmd;
    }
};

}  // namespace mecvr::room_model
