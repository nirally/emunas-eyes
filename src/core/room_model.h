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
    // Her speed our command explains: the commanded speed reached with ~0.25 s lag (measured, D-029); movement
    // along the command beyond gain x that + slack is the game's own (attack steps, PLAYTEST-6b report).
    float response_s = 0.25f, explain_gain = 1.5f, explain_slack = 0.2f;
    float expect_speed = 0, cmd_speed = 0;
    // Stall push (PLAYTEST-6d): in fights the game ignored the usual small walking stick (0.75: Faith 0.00 m/s
    // for seconds, the gap stayed open). Asked to walk but not moving for stall_s -> push the stick further
    // (boost_rate per s, up to boost_max) and keep that push until this walk ends (easing off as soon as she
    // moved made her stop and stall again).
    float stall_s = 0.3f, boost_rate = 0.3f, boost_max = 0.2f, stick_max = 0.92f;
    float stalled = 0, boost = 0;
    float bleed = 0.25f;       // m/s: view slides back onto her body while the player drives (PLAYTEST-6e)
    // Stuck (PLAYTEST-6f): sometimes she won't walk at all, even pushed (taking hits, or her body against an
    // enemy): stick 0.9, Faith 0.00 m/s for 2-4 s. Stalled this long -> the view slides onto her instead.
    float stuck_s = 0.8f, stuck_bleed = 0.4f;

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
            expect_speed = cmd_speed = 0;
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
        if (dt > 0) expect_speed += (cmd_speed - expect_speed) * (dt / (response_s + dt));
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
            else {
                // Only as much as our command explains (PLAYTEST-6b report: in fights the game's slower attack
                // steps along our walking were absorbed too, the view stayed behind her body and room-scale had
                // to walk her back). The rest is the game's own movement: the view goes with her.
                const float cap = (expect_speed * explain_gain + explain_slack) * dt;
                anchor = anchor + cmd_dir * std::fmax(-cap, std::fmin(along, cap));
            }
        }
        // While the player drives (stick, or just after an attack) nothing walks her to the head, so steps and
        // leans made meanwhile added up (PLAYTEST-6e: 0.34 m in a fight). Slide the view back onto her body
        // gently; she is moving anyway at these times.
        if (user_moving && dt > 0) {
            const V2 o = head - anchor;
            const float l = o.len();
            if (l > gap_ok) anchor = anchor + o * (std::fmin(bleed * dt, l - gap_ok) / l);
        }
        V2 off = head - anchor;
        Stick cmd;
        const bool was_walking = walking;
        walking = false;
        cmd_speed = 0;
        if (user_moving || !was_walking) stalled = 0;
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
                if (faith_vel.len() < 0.05f) stalled += dt;
                else stalled = 0;
                if (stalled > stall_s) boost = std::fmin(boost + boost_rate * dt, boost_max);
                const float s = std::fmin(curve.stick_for(v) + boost, stick_max);
                cmd_speed = curve.speed_of(curve.stick_for(v));
                cmd.x = want.x / v * s;
                cmd.y = -want.z / v * s;  // z back -> stick forward
            }
        }
        if (!walking) stalled = boost = 0;
        if (stalled > stuck_s && dt > 0) {
            const float l = off.len();
            if (l > gap_ok) {
                anchor = anchor + off * (std::fmin(stuck_bleed * dt, l - gap_ok) / l);
                off = head - anchor;
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
