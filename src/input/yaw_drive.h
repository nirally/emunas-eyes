#pragma once
#include <filesystem>

namespace mecvr::yaw_drive {

// Stage 4 (D-028): the mod owns turning; the game's heading is steered after the player's view by
// injected relative mouse motion (the game reads raw mouse input; one count turns it a fixed angle).
// Only injects while the game window is in front.
bool inject(int dx_counts);

// Developer test (main loop): MOUSETEST file injects known counts and logs how far the game turned.
void poll_test(const std::filesystem::path& dir);

}  // namespace mecvr::yaw_drive
