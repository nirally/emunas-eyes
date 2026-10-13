#pragma once
#include <filesystem>

namespace mecvr::cam_hunt {

// Recon tool (Stage 1). Given the camera world position read from the view
// constant buffer, finds CPU-side copies of it in memory and records them.
// Then, one candidate at a time, nudges the copy upward for a few frames and
// checks whether the next frame's view constants moved: proof that the
// candidate feeds the renderer ("a hook is not real until the game reacts").

// Called by cb_capture when it has identified the view matrix in a frame.
// view[16] is laid out as in the buffer (row-major, translation in column 3).
void on_view_matrix(const float* view, const std::filesystem::path& out_dir);

// Where reports go and where the HUNT / TRACE trigger files are watched.
void set_output_dir(const std::filesystem::path& dir);

// Called every Present (render thread): drives the poke test and the hotkeys
// F8 (hunt: keep the camera still, e.g. paused) and F9 (trace writers while moving).
void on_present();

}  // namespace mecvr::cam_hunt
