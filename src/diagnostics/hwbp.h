#pragma once
#include <cstdint>
#include <filesystem>

namespace mecvr::hwbp {

// Recon tool. Sets a CPU write-watch (debug register DR0) on `address` in every
// game thread for `ms` milliseconds, records each distinct instruction that
// writes there with a short call stack (module+offset), then removes the watch.
// Writes results to `out` (appends). Blocking; call from a worker thread.
void trace_writes(uintptr_t address, unsigned ms, const std::filesystem::path& out, const char* label);

}  // namespace mecvr::hwbp
