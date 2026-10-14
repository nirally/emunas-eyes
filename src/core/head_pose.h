#pragma once
#include "core/vmath.h"

namespace mecvr::head_pose {

// A pose in tracking space (X right, Y up, Z back; meters).
struct Pose {
    vm::Quat rotation;
    vm::Vec3 position;
    bool valid = false;
};

// What the camera should use this frame.
struct Sample {
    Pose relative;   // relative to the recentred origin (heading + position): drives the game camera
    Pose absolute;   // raw tracking-space pose: what the headset layer must be submitted with
    bool headset = false;  // true when it comes from the real headset, false = desktop simulator
};

Sample current();

// Headset (render thread, from OpenXR).
void set_headset_pose(const Pose& absolute, bool valid);
void request_recenter();     // next headset sample becomes the origin (heading + position)
unsigned recenter_count();   // recentres completed so far
// A tracking-space pose (e.g. a controller) in the same recentred frame as Sample::relative.
Pose to_relative(const Pose& absolute);

// Per-eye geometry relative to the head (from the runtime; defaults to 63 mm IPD, 90 deg FOV).
struct Eye {
    vm::Vec3 offset;       // eye position in head space (m)
    vm::Quat rotation;     // eye rotation in head space (canted displays)
    float fov[4] = {-0.785f, 0.785f, 0.785f, -0.785f};  // left, right, up, down (radians)
};
void set_eyes(const Eye& left, const Eye& right);
Eye eye(int index);  // 0 = left, 1 = right

// Which absolute pose the frame now being presented was rendered with.
void set_rendered(const Pose& absolute, int eye = -1);
Pose rendered_pose();
int rendered_eye();  // -1 = mono frame

// Desktop simulator (no headset): numpad drives the head.
//   4/6 yaw, 8/2 pitch, 7/9 roll, +/- height, 5 recentre (also recentres the headset).
void sim_update(float dt_seconds);
// Simulated step in the room (metres, x right, z back), for room-scale tests.
void set_sim_offset(float x, float z);

}  // namespace mecvr::head_pose
