#pragma once
#include <filesystem>

struct IDXGISwapChain;

namespace mecvr::ctx_hooks {

// Render thread, once per Present: installs the device-context hooks when per-eye TAA is on (or a
// TRACE is requested), runs the frame trace (a TRACE file in the mod folder logs the next two frames'
// draws/dispatches with shaders, inputs and outputs to trace_<n>.txt) and ticks taa_eyes.
void on_present(IDXGISwapChain* chain, const std::filesystem::path& dir);

}  // namespace mecvr::ctx_hooks
