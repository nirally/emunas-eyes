#pragma once
#include <filesystem>

namespace mecvr::cb_capture {

// Recon tool (Stage 1). Hooks constant-buffer uploads on the D3D11 immediate
// context. When triggered (F10, or a `CAPTURE` file in the mod folder), records
// every constant buffer written during the next frame to capture_<n>.txt and
// flags 4x4 blocks that look like view or projection matrices.
bool install(const std::filesystem::path& out_dir);

// Called from the Present hook once per frame.
void on_present();

}  // namespace mecvr::cb_capture
