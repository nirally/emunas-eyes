#pragma once
#include <d3d11.h>

#include <filesystem>

namespace mecvr::cb_capture {

// Recon tool (Stage 1). Hooks constant-buffer uploads on the D3D11 immediate
// context. When triggered (F10, or a `CAPTURE` file in the mod folder), records
// every constant buffer written during the next frame to capture_<n>.txt and
// flags 4x4 blocks that look like view or projection matrices.
bool install(const std::filesystem::path& out_dir, bool recon);

// Called from the Present hook once per frame.
void on_present();

// Copies the latest per-view constant buffer as the game wrote it (284 floats); false if none yet.
bool last_view_cb(float* out);

// The work of the Map / Unmap / UpdateSubresource hooks, also called by ctx_hooks for the other versions
// of those methods (BUG-011). Render thread; outermost hooked call only (hook_guard).
void after_map(ID3D11Resource* res, HRESULT hr, const D3D11_MAPPED_SUBRESOURCE* out);
void before_unmap(ID3D11Resource* res);
void before_update(ID3D11Resource* res, const D3D11_BOX* box, const void* data);

}  // namespace mecvr::cb_capture
