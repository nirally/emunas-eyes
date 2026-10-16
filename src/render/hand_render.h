#pragma once
#include <filesystem>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace mecvr::hand_render {

// Stage 5 hands (D-031): drawn into the HDR scene right after the TAA pass (before bloom and tonemap,
// so they're exposed like the scene), with the frame's own unjittered view-projection, depth-tested
// against the scene depth (reverse-Z, read-only). 5a: markers at the controllers' grip poses.
// Render thread; saves and restores all pipeline state it touches.
void draw(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* scene_color, ID3D11Texture2D* depth,
          const float* vp);
void set_enabled(bool on);  // hands.show
bool enabled();
void set_size(float s);  // hands.size: scales the whole hand about the controller's handle
void set_collide(bool on);  // hands.collide: hands stop at walls (5c)
void set_touch_interact(bool on);  // hands.touch_interact: pressing a hand into something in front of you = Interact (5d)
// STENCIL trigger: dump the scene depth-stencil at the next hand pass (5c recon).
void stencil_probe_next(const std::filesystem::path& dir);

}  // namespace mecvr::hand_render
