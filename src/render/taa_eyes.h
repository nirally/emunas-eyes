#pragma once

struct ID3D11DeviceContext;
struct ID3D11PixelShader;
struct ID3D11RenderTargetView;
struct IDXGISwapChain;

namespace mecvr::taa_eyes {

// Per-eye TAA history (D-023). The game's TAA pass reads the history its previous frame wrote and
// ping-pongs two history sets every frame - with alternate-eye rendering each eye was built on the
// other eye's history. Around that pass, this eye's saved history is copied into the textures the
// pass reads, and the pass's new history is saved for this eye afterwards.
//
// The pass is recognised by its outputs (5 full-size targets: R8, RGBA16F, RGBA16F, R8, R8) and its
// inputs (motion vectors RG16F in slot 1, history RGBA16F in slot 4); its pixel shaders are logged as
// they are seen (the pass isn't tied to one: BUG-011). The motion vectors are corrected first (motion_fix: the engine measures motion against
// the other eye's camera). Does nothing in mono or while the eye of the frame isn't known.
void set_enabled(bool on);  // rendering.taa_per_eye; Shift+F9 toggles; saved
bool enabled();
bool take_changed();        // toggled by key; main thread saves
void set_motion_fix(bool on);   // diagnostics.taa_motion_fix (developer A/B)
void probe_velocity_next();     // log mean motion vectors at the next TAA pass (VELOCITY file)

// From ctx_hooks (render thread). before_draw returns true when it acted; after_draw must follow.
void on_present(IDXGISwapChain* chain);
void on_set_ps(ID3D11DeviceContext* ctx, ID3D11PixelShader* ps);
void on_set_targets(ID3D11DeviceContext* ctx, unsigned count, ID3D11RenderTargetView* const* rtvs);
bool before_draw(ID3D11DeviceContext* ctx);
void after_draw(ID3D11DeviceContext* ctx);

// From cb_capture, for every upload of the per-view constant buffer (render thread, after the eye of
// the frame is known): records this eye's view-projection for the motion-vector fix. Read-only.
void record_view_cb(const float* floats, unsigned count);

}  // namespace mecvr::taa_eyes
