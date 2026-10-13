#pragma once
#include <cstdint>

namespace mecvr::camera_override {

enum class PitchSource { Auto, Game, Head };

// Finds SetCameraData (0x3536f10 in 1.0.3.47248) and the gameplay camera-manager
// call site by signature, and hooks the setter. Returns false if either
// signature is missing or ambiguous (wrong game version) - the mod then stays flat.
bool install();

void set_enabled(bool on);
void set_pitch_source(PitchSource s);

// Render thread, once per frame: hotkeys (Numpad0 toggles), caller report, sim step.
void on_present(float dt);

// View-constant-buffer watcher feeds the final view rows (row-major 3x4) for the
// "no roll unless the head rolls" assertion.
void check_view(const float* view);

uint64_t assertion_failures();

}  // namespace mecvr::camera_override
