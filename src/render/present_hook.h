#pragma once
#include <cstdint>

namespace mecvr::present_hook {

// Hooks IDXGISwapChain::Present and ResizeBuffers via a throwaway device's vtable.
// Must run on a worker thread, never inside DllMain.
bool install();
void uninstall();

uint64_t frame_count();

}  // namespace mecvr::present_hook
