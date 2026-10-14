#pragma once
// Controller poses for Stage 5 (hands), in the recentred tracking frame like head_pose::Sample::relative.
#include "core/head_pose.h"

namespace mecvr::hand_pose {

// hand 0 = left, 1 = right. Set from OpenXR each frame (grip pose: the hand's palm/handle).
void set(int hand, const head_pose::Pose& relative);
// Latest pose; without a headset and with diagnostics.hand_sim, hands held in front of the head.
head_pose::Pose get(int hand);
void set_sim(bool on);

}  // namespace mecvr::hand_pose
