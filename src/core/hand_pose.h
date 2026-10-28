#pragma once
// Controller poses for Stage 5 (hands), in the recentred tracking frame like head_pose::Sample::relative.
#include <cstdint>

#include "core/head_pose.h"

namespace mecvr::hand_pose {

// hand 0 = left, 1 = right. Set from OpenXR each frame (grip pose: the hand's palm/handle).
void set(int hand, const head_pose::Pose& relative);
// Latest pose; without a headset and with diagnostics.hand_sim, hands held in front of the head.
head_pose::Pose get(int hand);
void set_sim(bool on);
bool simulated();
// diagnostics.hand_sim_closeup: simulated hands raised close in front of the eyes and turned inwards (screenshots).
void set_sim_closeup(bool on);
// diagnostics.hand_sim_roll: simulated hands rolled about the handle, degrees (+ = the left hand's thumb outwards,
// mirrored on the right), to twist Faith's forearms on the desktop (BUG-037).
void set_sim_roll(float deg);
// diagnostics.hand_sim_raise: simulated hands reaching forward and up, her elbows in view (BUG-037).
void set_sim_raise(bool on);

// How each hand holds the controller (Stage 5b), 0 = open .. 1 = closed, smoothed. index: from the trigger
// (lifted off = pointing); others (middle, ring, pinky): from the grip; thumb: 1 = resting on the stick or
// buttons. Simulated hands: left points, right makes a fist.
struct Fingers { float index = 0.15f, others = 0.15f, thumb = 0; };
void set_controls(int hand, float trigger, bool trigger_touch, float grip, bool thumb_touch);
Fingers fingers(int hand);

// 5c: a short buzz on this hand when it meets a surface (render thread), and its strength now (input thread).
void contact_pulse(int hand);
float contact_buzz(int hand);
// 5d: a hand pressing into a surface in front of you holds Interact (A) until it comes away.
void set_touch_press(int hand, bool on);
bool touch_press();
// 5d (PLAYTEST-5k): a fast flick into it taps an attack instead (the door slam). Speed = the hand's fastest
// movement relative to the head in the last 0.2 s (m/s), so walking or Faith running doesn't count.
float recent_speed(int hand);
// hands.flick: 0 off, 1 Heavy attack (Y), 2 Light attack (X); hands.flick_speed in m/s.
void set_flick(int mode, float speed);
float flick_speed();  // 0 when off
void flick_pulse();
// A MAG Rope throw met the wall (6D): drop a flick attack that hasn't gone out yet. True if one was dropped.
bool cancel_flick();
// Pad buttons the hands add this frame: Interact while pressing, the attack for 150 ms after a flick.
uint16_t touch_buttons();

}  // namespace mecvr::hand_pose

namespace mecvr::hand_pose {
// 6C physical punching (combat.punch, combat.punch_speed) and when the last flick / punch fired, so a door
// flick and a punch in the same swing don't both fire.
void set_punch(bool on, float speed);
bool punch_on();
float punch_speed();
unsigned long long last_flick_ms();  // GetTickCount64 of the last flick, 0 = never
void note_punch();
bool punch_recent(unsigned ms);
}  // namespace mecvr::hand_pose
