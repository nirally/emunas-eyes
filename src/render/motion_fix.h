#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace mecvr::motion_fix {

// Per-eye TAA (D-023): the engine computes motion vectors against the previous frame's camera, which
// with alternate-eye rendering is the other eye's - so every pixel looks like it moved by the eye
// separation, and TAA reprojects this eye's history to the wrong place. Just before the TAA draw this
// adds, per pixel, the difference between where the point was under the engine's previous camera and
// under this eye's previous camera (additive blend into the RG16F motion texture). Saves and restores
// all pipeline state it touches. Matrices: 16 floats, rows, as in the view constants.
bool apply(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* depth_srv, ID3D11Texture2D* motion,
           const float* cur_vp_jittered, const float* prev_engine_vp, const float* prev_own_vp,
           const float* prev_engine_vp_j, const float* prev_own_vp_j);

// diagnostics.motion_fix_debug: run the pass but write zeros (state vs value problems).
void set_debug_zero(bool on);
// diagnostics.motion_fix_jitter: 0 = camera only, 1 = include the TAA jitter difference, -1 = opposite.
void set_jitter_mode(float m);

}  // namespace mecvr::motion_fix
