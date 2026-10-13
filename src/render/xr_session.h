#pragma once
#include <filesystem>

struct IDXGISwapChain;

namespace mecvr::xr_session {

// Minimal OpenXR presence for Stage 1 (D-007): reads the headset pose and shows
// the game's (mono) frame world-locked in the headset at the game's own field
// of view. Stereo and full input come in Stages 2-3.
//
// All calls happen on the render thread inside Present. If the runtime, the
// headset or the GPU match is missing, it logs why and the mod stays flat
// (desktop simulator keeps working).

void configure(const std::filesystem::path& mod_dir, bool enabled);

// Called at the start of every Present, before the game's frame is shown.
void on_present(IDXGISwapChain* chain);

// Game projection scale terms from the view constants (1/tan of half-FOV).
void set_game_projection(float sx, float sy);

bool running();

}  // namespace mecvr::xr_session
