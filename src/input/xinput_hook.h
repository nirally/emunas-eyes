#pragma once
#include <cstdint>
#include <filesystem>

#include "core/pad_map.h"

namespace mecvr::xinput_hook {

// Stage 3 (D-026): the game reads its Xbox pad through XInput (xinput1_4 / xinput9_1_0 are loaded).
// Hooks XInputGetState (+ the ordinal-100 "Ex" variant), XInputGetCapabilities and XInputSetState in
// every XInput DLL the game has loaded (retried until they are), so a virtual pad can be served for
// controller 0 and the game's vibration requests can be routed to the Touch controllers.
// Every 10 s it logs which entry points were called, for which controller and how often.
void poll_install();  // main loop: hooks any XInput DLL loaded since the last call

// The virtual pad (XINPUT_GAMEPAD layout). While active, controller 0 reports it as connected.
using Pad = pad_map::Pad;
void set_virtual(bool active);
void set_pad(const Pad& pad);

// Last vibration the game asked for on controller 0 (0..65535 each).
void last_vibration(uint16_t& left_motor, uint16_t& right_motor);

void report();  // render/main thread, every 10 s

// Developer test (main loop): PADTEST file plays the pad states listed in padtest.txt.
void poll_test(const std::filesystem::path& dir);

}  // namespace mecvr::xinput_hook
