// Stage 8A: the game's UI pulled in towards the middle of the frame. See ui_shrink.h.
//
// Frame trace (1.0.3.47248, 2880x1620; gameplay and the pause menu, 2026-10-26): the tonemap composite writes the
// back buffer (R8G8B8A8, plain draws), a compute blur of it follows (the menus' frosted background), then every UI
// element is an indexed draw onto the back buffer with the UI's own vertex buffer (gameplay ~32 per frame, the pause
// menu ~93). Nothing else draws indexed onto the back buffer.
#include "render/ui_shrink.h"

#include <d3d11.h>
#include <windows.h>

#include <atomic>

#include "core/ui_fit.h"
#include "diagnostics/log.h"
#include "render/xr_session.h"

namespace mecvr::ui_shrink {
namespace {

std::atomic<float> g_scale{0.4f};
std::atomic<bool> g_flat{false};
ID3D11Resource* g_back = nullptr;  // this Present's back buffer (compared only, not held)
float g_fw = 0, g_fh = 0;

// Contexts whose render target 0 is the back buffer (the game draws on the immediate context; a few slots in case).
struct Slot {
    ID3D11DeviceContext* ctx;
    bool on;
};
Slot g_slots[8] = {};

// The draw in flight (render thread).
D3D11_VIEWPORT g_vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
D3D11_RECT g_sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
UINT g_vp_n = 0, g_sc_n = 0;
unsigned g_shrunk = 0, g_frames = 0;  // 10 s log

Slot* slot(ID3D11DeviceContext* ctx, bool add) {
    for (auto& s : g_slots)
        if (s.ctx == ctx) return &s;
    if (!add) return nullptr;
    for (auto& s : g_slots)
        if (!s.ctx) {
            s.ctx = ctx;
            return &s;
        }
    return nullptr;
}

bool active() {
    return g_scale.load() < 0.999f && g_back && (xr_session::running() || g_flat.load());
}

}  // namespace

void set_scale(float k) { g_scale = k < 0.2f ? 0.2f : k > 1.f ? 1.f : k; }
void set_flat(bool on) { g_flat = on; }

void on_present(IDXGISwapChain* chain) {
    ID3D11Texture2D* t = nullptr;
    if (SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) && t) {
        D3D11_TEXTURE2D_DESC d{};
        t->GetDesc(&d);
        g_back = t;
        g_fw = float(d.Width);
        g_fh = float(d.Height);
        t->Release();
    }
    ++g_frames;
    static ULONGLONG last = 0;
    if (GetTickCount64() - last >= 10000) {
        last = GetTickCount64();
        if (active() || g_shrunk)
            LOG_I("ui", "UI draws pulled in (scale %.2f): %u in %u frames", g_scale.load(), g_shrunk, g_frames);
        g_shrunk = g_frames = 0;
    }
}

void on_set_targets(ID3D11DeviceContext* ctx, unsigned count, ID3D11RenderTargetView* const* rtvs) {
    bool back = false;
    if (count > 0 && rtvs && rtvs[0] && g_back) {
        ID3D11Resource* r = nullptr;
        rtvs[0]->GetResource(&r);
        back = r == g_back;
        if (r) r->Release();
    }
    if (Slot* s = slot(ctx, back)) s->on = back;
}

bool before_draw(ID3D11DeviceContext* ctx) {
    if (!active()) return false;
    const Slot* s = slot(ctx, false);
    if (!s || !s->on) return false;
    const float k = g_scale.load();
    g_vp_n = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetViewports(&g_vp_n, g_vp);
    if (!g_vp_n) return false;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    for (UINT i = 0; i < g_vp_n; ++i) {
        const ui_fit::Rect r = ui_fit::scale_about_centre({g_vp[i].TopLeftX, g_vp[i].TopLeftY, g_vp[i].Width, g_vp[i].Height}, g_fw, g_fh, k);
        vp[i] = g_vp[i];
        vp[i].TopLeftX = r.x;
        vp[i].TopLeftY = r.y;
        vp[i].Width = r.w;
        vp[i].Height = r.h;
    }
    ctx->RSSetViewports(g_vp_n, vp);
    // Clipped elements (scrolling lists, the map) clip with the scissor: it moves with them.
    g_sc_n = 0;
    ID3D11RasterizerState* rs = nullptr;
    ctx->RSGetState(&rs);
    if (rs) {
        D3D11_RASTERIZER_DESC d{};
        rs->GetDesc(&d);
        rs->Release();
        if (d.ScissorEnable) {
            g_sc_n = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            ctx->RSGetScissorRects(&g_sc_n, g_sc);
            D3D11_RECT sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            for (UINT i = 0; i < g_sc_n; ++i) {
                const ui_fit::Rect r = ui_fit::scale_about_centre(
                    {float(g_sc[i].left), float(g_sc[i].top), float(g_sc[i].right - g_sc[i].left), float(g_sc[i].bottom - g_sc[i].top)}, g_fw, g_fh, k);
                sc[i] = {LONG(r.x), LONG(r.y), LONG(r.x + r.w + 0.999f), LONG(r.y + r.h + 0.999f)};
            }
            if (g_sc_n) ctx->RSSetScissorRects(g_sc_n, sc);
        }
    }
    ++g_shrunk;
    return true;
}

void after_draw(ID3D11DeviceContext* ctx) {
    ctx->RSSetViewports(g_vp_n, g_vp);
    if (g_sc_n) ctx->RSSetScissorRects(g_sc_n, g_sc);
}

}  // namespace mecvr::ui_shrink
