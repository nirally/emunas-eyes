#pragma once
#include <windows.h>

namespace mecvr::proxy {

// Which system DLL this binary is standing in for, decided by our own file name.
enum class Role { Version, DInput8, Unknown };

// Called from DllMain(PROCESS_ATTACH): loads the real system DLL and fills the
// forwarding table. Must stay cheap: no threads, no waiting.
Role init(HMODULE self);

}  // namespace mecvr::proxy
