#pragma once

struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct IDXGISwapChain;

namespace mecvr::ui_shrink {

// Stage 8A (Q-801/Q-802): the game's UI - HUD, prompts, menus, the map - is drawn last, with indexed draws straight
// onto the back buffer (trace: after the tonemap composite, which uses plain draws). While the headset runs, each of
// those draws gets its viewport and scissor scaled by ui.scale about the frame's centre, so the corners the eyes
// never see come in. ui.scale 1 = off. diagnostics.ui_scale_flat applies it without a headset (desktop tests).
void set_scale(float k);       // ui.scale
void set_flat(bool on);        // diagnostics.ui_scale_flat

// From ctx_hooks (render thread).
void on_present(IDXGISwapChain* chain);
void on_set_targets(ID3D11DeviceContext* ctx, unsigned count, ID3D11RenderTargetView* const* rtvs);
bool before_draw(ID3D11DeviceContext* ctx);  // indexed draws; true = after_draw must follow
void after_draw(ID3D11DeviceContext* ctx);

}  // namespace mecvr::ui_shrink
