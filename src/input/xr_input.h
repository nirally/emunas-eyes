#pragma once
#include <openxr/openxr.h>

namespace mecvr::xr_input {

// Stage 3 (D-026): Touch controllers -> the game's Xbox pad layout (xinput_hook virtual pad).
// OpenXR actions per physical control, both hands (subaction paths), with suggested bindings for
// Touch, Index and the generic simple controller; SteamVR's binding UI can remap them per game.
// Default mapping (right-handed; "input.left_handed" mirrors it):
//   left stick  -> move              right stick X -> turn (Y only with input.stick_pitch)
//   left grip   -> LB  Up actions    left trigger  -> LT  Down actions
//   right grip  -> RB  Quick turn    right trigger -> RT  Shift
//   A/B (right) -> A/B Interact / Disruptor        X/Y (left) -> X/Y Light / Heavy attack
//   right stick click -> R3 Runner's Vision        left stick click -> tap: Start (pause), hold: Back (map)
//   left menu   -> Start (SteamVR keeps it for its dashboard on Touch, so mostly unused)
// The game's vibration (XInputSetState) is played on the matching hand.

// After xrCreateSession: creates and attaches the action set. False keeps the game on keyboard/mouse.
bool create(XrInstance instance, XrSession session, XrSpace base, PFN_xrGetInstanceProcAddr gipa);

// Render thread, once per OpenXR frame. `focused`: session state FOCUSED (input is only valid then).
void sync(XrTime time, bool focused);

void set_left_handed(bool on);   // input.left_handed
void set_stick_pitch(bool on);   // input.stick_pitch
// comfort.physical_crouch / comfort.crouch_depth: really crouching (head this far below standing height)
// holds Down actions (crouch, or slide when running).
void set_physical_crouch(bool on, float depth_m);

}  // namespace mecvr::xr_input
