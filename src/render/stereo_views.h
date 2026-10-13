#pragma once
#include <cstdint>

namespace mecvr::stereo_views {

// Stage 2 experiment: two views per frame through the engine's split-screen view builder.
bool install();
void set_enabled(bool on);
uint64_t split_frames();

}  // namespace mecvr::stereo_views
