#pragma once
#include <string>

namespace mecvr::game_version {

// The exe version every hook was built against.
inline constexpr const char* kTarget = "1.0.3.47248";

// File version of the host exe, e.g. "1.0.3.47248", or "" if unreadable.
std::string host_exe_version();

}  // namespace mecvr::game_version
