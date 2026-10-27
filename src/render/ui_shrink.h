#pragma once

struct ID3D11DeviceContext;
struct ID3D11Resource;
struct ID3D11RenderTargetView;
struct IDXGISwapChain;

namespace mecvr::ui_shrink {

// Stage 8A (Q-801/Q-802): the game's UI - HUD, prompts, menus, the map - is drawn last, with indexed draws straight
// onto the back buffer (trace: after the tonemap composite, which uses plain draws). While the headset runs, each of
// those draws gets its viewport and scissor scaled by ui.scale about the frame's centre, so the corners the eyes
// never see come in. ui.scale 1 = off. diagnostics.ui_scale_flat applies it without a headset (desktop tests).
void set_scale(float k);       // ui.scale
void set_flat(bool on);        // diagnostics.ui_scale_flat
// 8C (PLAYTEST-8b: health bar "I can only see the end of it I need it to be bigger and you can even put it in the
// center"): UI draws in the bar's corner (bottom left) go to the bottom middle at their own size instead.
void set_hud(bool centre, float scale, float y);  // ui.hud_centre, ui.hud_scale, ui.hud_height
// 8D (PLAYTEST-8c: the "KSEC Responding" window "is to much to the upper right so I can't see it clearly"): UI draws
// in the top-right corner go in towards the middle at their own size.
void set_alert(bool on, float scale, float x, float y);  // ui.alert_move, ui.alert_scale, ui.alert_x, ui.alert_y

// 8E: seconds since the HUD's health bar was last drawn (3+ frames in a row); 0 before it was ever seen this session,
// so a failed lookup can't send the game to the big screen. Watched while ui.cutscene_screen is on, also while the
// big screen shows (the UI is full size there, untouched), so its return ends a cutscene. Render thread.
float hud_absent_seconds();

// From ctx_hooks (render thread).
void on_present(IDXGISwapChain* chain);
void on_set_targets(ID3D11DeviceContext* ctx, unsigned count, ID3D11RenderTargetView* const* rtvs);
bool before_draw(ID3D11DeviceContext* ctx, int base_vertex);  // indexed draws; true = after_draw must follow
void after_draw(ID3D11DeviceContext* ctx);
// cb_capture: the game mapped / unmaps a 2.56 MB vertex buffer (the UI's). While the HUD corner moves, the game
// writes into a shadow (returned) so each draw's position can be read; it is copied over at Unmap.
void* on_vb_map(ID3D11Resource* res, void* mapped);
void on_vb_unmap(ID3D11Resource* res);
// While a frame trace runs: a UI draw's first vertex and per-draw constants, as a trace line (Stage 8 recon).
void trace_draw(ID3D11DeviceContext* ctx, unsigned index_count, unsigned start_index, int base_vertex);

}  // namespace mecvr::ui_shrink
