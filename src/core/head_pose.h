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

// Which absolute pose the frame now being presented was rendered with.
void set_rendered(const Pose& absolute);
Pose rendered_pose();

// Desktop simulator (no headset): numpad drives the head.
//   4/6 yaw, 8/2 pitch, 7/9 roll, +/- height, 5 recentre (also recentres the headset).
void sim_update(float dt_seconds);

}  // namespace mecvr::head_pose
