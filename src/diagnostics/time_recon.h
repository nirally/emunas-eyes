#pragma once
#include <filesystem>

namespace mecvr::time_recon {

// Recon (diagnostics.time_recon): hooks the Windows clock functions and, over a window of
// game frames, records which call sites read the clock on which thread. Writes `out` once.
void install(const std::filesystem::path& out);

// Called from the main camera site once per game frame (only when installed).
void on_game_frame();

}  // namespace mecvr::time_recon
