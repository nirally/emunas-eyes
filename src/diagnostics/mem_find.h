#pragma once
#include <filesystem>

namespace mecvr::mem_find {

// Recon (main loop): MEMFIND file -> memfind_<n>.txt (where the view constants' previous/current
// camera values live in memory); WATCH file + watch.txt (hex addresses) -> watch_<addr>.txt (writers).
void poll(const std::filesystem::path& dir);

}  // namespace mecvr::mem_find
