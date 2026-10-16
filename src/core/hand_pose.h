#pragma once
// Controller poses for Stage 5 (hands), in the recentred tracking frame like head_pose::Sample::relative.
#include "core/head_pose.h"

namespace mecvr::hand_pose {

// hand 0 = left, 1 = right. Set from OpenXR each frame (grip pose: the hand's palm/handle).
void set(int hand, const head_pose::Pose& relative);
// Latest pose; without a headset and with diagnostics.hand_sim, hands held in front of the head.
head_pose::Pose get(int hand);
void set_sim(bool on);
// diagnostics.hand_sim_closeup: simulated hands raised close in front of the eyes and turned inwards (screenshots).
void set_sim_closeup(bool on);

// How each hand holds the controller (Stage 5b), 0 = open .. 1 = closed, smoothed. index: from the trigger
// (lifted off = pointing); others (middle, ring, pinky): from the grip; thumb: 1 = resting on the stick or
// buttons. Simulated hands: left points, right makes a fist.
struct Fingers { float index = 0.15f, others = 0.15f, thumb = 0; };
void set_controls(int hand, float trigger, bool trigger_touch, float grip, bool thumb_touch);
Fingers fingers(int hand);

}  // namespace mecvr::hand_pose
