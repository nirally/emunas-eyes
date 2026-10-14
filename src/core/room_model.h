#pragma once
// Room-scale (Stage 4, D-029, BUG-007). Pure, unit-tested.
//
// The view used to add the whole real head offset (from the recentre point) to Faith's camera, so a
// step in the room put the view in front of her body. Now Faith walks to where you stand: the part of
// the head offset Faith hasn't caught up with ("offset") drives a gentle stick command, and Faith's
// movement along that command is absorbed into an anchor, so the view stays on the real head while
// her body arrives under it. Movement the command doesn't explain (hits, animations) isn't absorbed.
// Vectors are horizontal, in the body frame (x right, z back; metres), like the head offset.
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

struct Model {
    float start = 0.15f;     // m: start walking Faith over when the offset is larger
    float stop = 0.08f;      // m: stop when it's smaller (she coasts ~0.15 m after release)
    float full = 0.50f;      // m: offset at which the stick reaches max_stick
    float min_stick = 0.72f; // the game: 0.4 -> no movement, 0.6 -> 0.06 m/s, 0.8 -> 0.6 m/s, 1.0 -> 3.8 m/s
    float max_stick = 0.85f; // a walk, not a run
    float max_offset = 0.50f;// m: view never further from Faith than this (walls: no walking through)
    float jump_reset = 1.0f; // m: a head jump this large in one frame = recentre, start fresh

    V2 anchor;               // head position Faith is standing under
    V2 last_head;
    bool walking = false, primed = false;

    // head: real head offset; moved: Faith's displacement since last frame (body frame); user_moving: the
    // player is using the move stick (no correction then). Returns the stick to add; view_offset = the
    // horizontal head offset the camera should use.
    Stick update(V2 head, V2 moved, bool user_moving, V2& view_offset) {
        if (!primed || (head - last_head).len() > jump_reset) {
            primed = true;
            anchor = head;
            walking = false;
        }
        last_head = head;
        Stick cmd;
        V2 off = head - anchor;
        // Absorb Faith's movement along the direction we asked for, at most the offset itself.
        if (walking && !user_moving) {
            const float l = off.len();
            if (l > 1e-4f) {
                const V2 dir = off * (1.f / l);
                float along = moved.x * dir.x + moved.z * dir.z;
                if (along > l) along = l;
                if (along > 0) anchor = anchor + dir * along;
            }
            off = head - anchor;
        }
        const float l = off.len();
        if (user_moving) walking = false;
        else if (!walking && l > start) walking = true;
        else if (walking && l < stop) walking = false;
        if (walking) {
            float s = min_stick + (max_stick - min_stick) * (l > full ? 1.f : l / full);
            cmd.x = off.x / l * s;
            cmd.y = -off.z / l * s;  // z back -> stick forward
        }
        // Walls: the view can't run away from Faith; the anchor follows the head beyond max_offset.
        if (l > max_offset) {
            anchor = head - off * (max_offset / l);
            off = head - anchor;
        }
        view_offset = off;
        return cmd;
    }
};

}  // namespace mecvr::room_model
