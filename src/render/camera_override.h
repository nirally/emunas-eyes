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

// Extra camera height in meters (comfort.height_offset). F3 / F2 adjust it live in 2 cm steps;
// take_height_changed() tells the main thread to save the new value.
void set_height_offset(float meters);
float height_offset();
bool take_height_changed();

// comfort.game_camera_motion: when on, the game's own camera rotation (rolls, wall-run tilt,
// landing dips) is added on top of the head pose; look pitch is excluded. F5 toggles; saved.
void set_game_motion(bool on);
bool game_motion();
bool take_motion_changed();

// stereo.mode: alternate = the game renders one eye per frame (left, right, left...).
void set_stereo(bool on);
bool stereo();
bool take_stereo_changed();  // F4 toggled it; main thread saves
// For eye sync (time_sync), game thread: overrides at the main site so far, and whether the next
// game frame will be drawn as the right eye (the main site flips the eye when it runs).
uint64_t main_site_count();
bool next_frame_is_right_eye();
void set_zero_ipd(bool on);  // diagnostics.zero_ipd: no eye offset, so the two eyes of a pair should be identical
void set_ipd_scale(float s);  // diagnostics.ipd_scale: eye offset x s (developer checks)
void set_ipd_vertical(bool on);  // diagnostics.ipd_vertical: eyes stacked vertically (developer checks)
// Render thread: eye of the frame being drawn now, from its view constants (0 left, 1 right, -1 mono or
// not matched). Set when the frame's view constants are uploaded, early in the frame.
int rendering_eye();
// The game camera's heading (radians, + = turned left), from the latest main-site camera.
float game_heading();

// Mod-owned turning (D-028, input.mod_turning, headset only): the view heading is the mod's; the right
// stick turns it (smooth at speed_deg_s, or snap); the game is steered after the view by mouse injection.
void set_mod_turning(bool on);
void set_turn_input(float x);  // turn stick, -1..1, + = right (render thread)
void set_turn_options(float speed_deg_s, bool snap, float snap_deg);
bool turning_active();
void set_turning_sim(bool on);  // diagnostics.mod_turning_sim (developer)
void set_body_follows_head(bool on);  // input.body_follows_head (default off, PLAYTEST-4a: stick sets the direction)
float world_heading();          // W
void report_turning();  // every 10 s

// Physical crouch started a slide (xr_input): the view stops adding the real crouch while it lasts.
void set_crouch_slide(bool on);

// rendering.full_fov: with a headset, the game camera's FOV and aspect are set to cover both eyes'
// full field of view (from the runtime), so the picture fills the headset instead of a window.
void set_full_fov(bool on);  // F9 toggles; saved
bool full_fov();
bool take_full_fov_changed();
void set_fov_test(bool on);  // diagnostics.fov_test: apply it without a headset (stand-in 90 deg FOV)
// View-constant watcher: the vertical projection scale (1 / tan(half vertical FOV)) actually rendered.
void note_projection(float sy);

}  // namespace mecvr::camera_override
