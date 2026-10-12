#pragma once
#include <windows.h>

namespace mecvr::proxy {

inline constexpr int kExportCount = 32;

// Which DLL this binary is standing in for, decided by our own file name.
enum class Role { Version, DInput8, NvCamera, NvCameraMissingOriginal, Unknown };

// Called from DllMain(PROCESS_ATTACH): loads the real DLL and fills the
// forwarding table. Must stay cheap: no threads, no waiting.
Role init(HMODULE self);

// Per-export call counters, for finding out what the game actually uses.
const char* export_name(int index);
unsigned long long call_count(int index);

}  // namespace mecvr::proxy
