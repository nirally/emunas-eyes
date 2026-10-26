#pragma once
#include <filesystem>

struct IDXGISwapChain;
struct ID3D11DeviceContext;

namespace mecvr::ctx_hooks {

// Render thread, once per Present: installs the device-context hooks when per-eye TAA is on (or a
// TRACE is requested), runs the frame trace (a TRACE file in the mod folder logs the next two frames'
// draws/dispatches with shaders, inputs and outputs to trace_<n>.txt) and ticks taa_eyes.
void on_present(IDXGISwapChain* chain, const std::filesystem::path& dir);
void request_trace();  // same as a TRACE file: the next frames are traced (BUG-011 auto trace)
// Stage 7 recon: while a trace runs, cb_capture hands over each CPU write to a big dynamic buffer (at Unmap).
bool tracing();
void trace_note(ID3D11DeviceContext* ctx, const char* text);  // a line in the running trace
void on_big_write(const void* res, const void* data, unsigned size);

}  // namespace mecvr::ctx_hooks
