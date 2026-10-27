// Stage 8A: the game's UI pulled in towards the middle of the frame. See ui_shrink.h.
//
// Frame trace (1.0.3.47248, 2880x1620; gameplay and the pause menu, 2026-10-26): the tonemap composite writes the
// back buffer (R8G8B8A8, plain draws), a compute blur of it follows (the menus' frosted background), then every UI
// element is an indexed draw onto the back buffer with the UI's own vertex buffer (gameplay ~32 per frame, the pause
// menu ~93). Nothing else draws indexed onto the back buffer.
//
// 8C recon (desktop, gameplay): that vertex buffer (2.56 MB, dynamic) is mapped ~1.3 times a frame and every UI draw
// reads the buffer mapped last. Each draw's vertex shader constants (c0, in the per-draw ring faith_bones shadows)
// hold a matrix taking the vertex's first two floats (pixels) to clip space.
#include "render/ui_shrink.h"

#include <d3d11_1.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/ui_fit.h"
#include "diagnostics/log.h"
#include "render/ctx_hooks.h"
#include "render/faith_bones.h"
#include "render/screen_mode.h"
#include "render/xr_session.h"

namespace mecvr::ui_shrink {
namespace {

std::atomic<float> g_scale{0.4f};
std::atomic<bool> g_flat{false};
std::atomic<bool> g_hud_centre{true};
std::atomic<float> g_hud_scale{0.7f}, g_hud_y{-0.35f};
std::atomic<bool> g_alert_on{true};
std::atomic<float> g_alert_scale{0.5f}, g_alert_x{0.12f}, g_alert_y{0.18f};
ID3D11Resource* g_back = nullptr;  // this Present's back buffer (compared only, not held)
float g_fw = 0, g_fh = 0;

// The health/focus bar's corner of the frame (frame space: -1..1 across, y up). A UI draw whose first vertex lies in
// it moves on its own: kAnchor (the bar and its icon, x -0.98..-0.77 y -0.95..-0.86 on the desktop) lands on (0, ui.hud_y) at ui.hud_scale.
constexpr float kCornerX = -0.45f, kCornerY = -0.45f;
constexpr float kAnchorX = -0.77f, kAnchorY = -0.90f;  // x: the middle of its track, which runs on to the right
// 8D: the top-right corner (the "KSEC Responding" alert window). Not seen on the desktop yet: kAlert is a guess at
// its middle, landing on (ui.alert_x, ui.alert_y) at ui.alert_scale; the 10 s log gives the corner's first vertices.
constexpr float kAlertCornerX = 0.45f, kAlertCornerY = 0.45f;
constexpr float kAlertX = 0.75f, kAlertY = 0.80f;

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
unsigned g_shrunk = 0, g_cornered = 0, g_alerted = 0, g_frames = 0;  // 10 s log
float g_box[4] = {1, 1, -1, -1};        // corner draws' first vertices, 10 s (x0, y0, x1, y1)
float g_alert_box[4] = {1, 1, -1, -1};  // the same for the top-right corner

// The UI vertex buffer, written into a shadow while the HUD corner moves (render thread).
constexpr unsigned kVbBytes = 2560000;
std::vector<unsigned char> g_vb_shadow;
ID3D11Resource* g_vb = nullptr;  // the buffer the shadow mirrors (null: its last map was not shadowed)
void* g_vb_real = nullptr;       // its mapping, until Unmap
// The furthest any UI draw has read from it; only the start of the buffer is copied (gameplay uses ~80 KB).
long long g_vb_used = 0;
LONGLONG g_copy_ticks = 0;
unsigned g_copies = 0;

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

bool shown() { return g_back && (xr_session::running() || g_flat.load()) && !screen_mode::active(); }
bool moving() { return g_hud_centre.load() || g_alert_on.load(); }  // some corner moves on its own: the vertex shadow is needed
bool active() { return (g_scale.load() < 0.999f || moving()) && shown(); }
// 8E: the health bar's draws are looked for (not moved) while cutscenes may go to the big screen.
bool watching() { return screen_mode::cutscenes() && g_back && (xr_session::running() || g_flat.load()); }
bool g_hud_now = false;          // a health-bar draw this frame
unsigned g_hud_run = 0;          // frames in a row with one
ULONGLONG g_hud_seen_ms = 0;     // when the bar was last there (3+ frames in a row)

void grow(float* box, float fx, float fy) {
    if (fx < box[0]) box[0] = fx;
    if (fy < box[1]) box[1] = fy;
    if (fx > box[2]) box[2] = fx;
    if (fy > box[3]) box[3] = fy;
}

// The draw's vertex shader constants c0 (12 float4) if readable.
const float* draw_constants(ID3D11DeviceContext* ctx, UINT* first_out = nullptr, UINT* num_out = nullptr) {
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&c1))) || !c1) return nullptr;
    ID3D11Buffer* cb = nullptr;
    UINT first = 0, num = 0;
    c1->VSGetConstantBuffers1(0, 1, &cb, &first, &num);
    c1->Release();
    size_t size = 0;
    const unsigned char* d = cb ? faith_bones::shadow_of(cb, &size) : nullptr;
    if (cb) cb->Release();
    if (first_out) *first_out = first;
    if (num_out) *num_out = num;
    if (!d || size_t(first) * 16 + 12 * 16 > size) return nullptr;
    return reinterpret_cast<const float*>(d + size_t(first) * 16);
}

// The draw's first vertex (its first two floats) in frame space, from the shadow; false if unknown.
bool first_vertex(ID3D11DeviceContext* ctx, int base_vertex, float* fx, float* fy, const float** raw = nullptr) {
    if (!g_vb || g_vb_real || g_fw <= 0) return false;  // not shadowed, or still being written
    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, off = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &off);
    if (!vb) return false;
    const bool ours = static_cast<ID3D11Resource*>(vb) == g_vb;
    vb->Release();
    const long long at = static_cast<long long>(off) + static_cast<long long>(base_vertex) * stride;
    if (!ours || stride < 8 || at < 0 || at + 16 > kVbBytes) return false;
    if (at > g_vb_used) g_vb_used = at;
    const float* v = reinterpret_cast<const float*>(g_vb_shadow.data() + at);
    if (raw) *raw = v;
    const float* c = draw_constants(ctx);
    if (c) {  // rows 1, 2, 4 of c0: clip x, y, w for (x, y, 0, 1)
        const float w = c[15];
        if (w == 0.f) return false;
        *fx = (c[4] * v[0] + c[5] * v[1] + c[7]) / w;
        *fy = (c[8] * v[0] + c[9] * v[1] + c[11]) / w;
    } else {
        *fx = v[0] / g_fw * 2.f - 1.f;
        *fy = 1.f - v[1] / g_fh * 2.f;
    }
    return true;
}

}  // namespace

void* on_vb_map(ID3D11Resource* res, void* mapped) {
    if (!mapped || !((moving() && shown()) || watching())) {
        if (res == g_vb) g_vb = nullptr;
        return mapped;
    }
    if (g_vb_shadow.size() != kVbBytes) g_vb_shadow.assign(kVbBytes, 0);
    g_vb = res;
    g_vb_real = mapped;
    return g_vb_shadow.data();
}

void on_vb_unmap(ID3D11Resource* res) {
    if (res != g_vb || !g_vb_real) return;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    const long long n = 2 * g_vb_used + 128 * 1024 < 512 * 1024 ? 512 * 1024 : 2 * g_vb_used + 128 * 1024;
    memcpy(g_vb_real, g_vb_shadow.data(), size_t(n < kVbBytes ? n : kVbBytes));
    QueryPerformanceCounter(&t1);
    g_copy_ticks += t1.QuadPart - t0.QuadPart;
    ++g_copies;
    g_vb_real = nullptr;
}

void set_scale(float k) { g_scale = k < 0.2f ? 0.2f : k > 1.f ? 1.f : k; }
void set_flat(bool on) { g_flat = on; }
void set_hud(bool centre, float scale, float y) {
    g_hud_centre = centre;
    g_hud_scale = scale < 0.2f ? 0.2f : scale > 1.5f ? 1.5f : scale;
    g_hud_y = y < -0.9f ? -0.9f : y > 0.9f ? 0.9f : y;
}
void set_alert(bool on, float scale, float x, float y) {
    g_alert_on = on;
    g_alert_scale = scale < 0.2f ? 0.2f : scale > 1.5f ? 1.5f : scale;
    g_alert_x = x < -0.9f ? -0.9f : x > 0.9f ? 0.9f : x;
    g_alert_y = y < -0.9f ? -0.9f : y > 0.9f ? 0.9f : y;
}

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
    g_hud_run = g_hud_now ? g_hud_run + 1 : 0;
    if (g_hud_run >= 3) g_hud_seen_ms = GetTickCount64();
    g_hud_now = false;
    static ULONGLONG last = 0;
    if (GetTickCount64() - last >= 10000) {
        last = GetTickCount64();
        if (active() || g_shrunk) {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            LOG_I("ui", "UI draws pulled in (scale %.2f): %u in %u frames; HUD corner %u (first vertices x %.2f..%.2f y %.2f..%.2f), "
                  "alert corner %u (x %.2f..%.2f y %.2f..%.2f), vertex copies %u, %.3f ms each", g_scale.load(), g_shrunk, g_frames, g_cornered,
                  g_box[0], g_box[2], g_box[1], g_box[3], g_alerted, g_alert_box[0], g_alert_box[2], g_alert_box[1], g_alert_box[3], g_copies, g_copies ? 1000.0 * double(g_copy_ticks) / double(f.QuadPart) / g_copies : 0.0);
        }
        g_shrunk = g_cornered = g_alerted = g_frames = g_copies = 0;
        g_copy_ticks = 0;
        g_box[0] = g_box[1] = g_alert_box[0] = g_alert_box[1] = 1.f;
        g_box[2] = g_box[3] = g_alert_box[2] = g_alert_box[3] = -1.f;
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

float hud_absent_seconds() { return g_hud_seen_ms ? (GetTickCount64() - g_hud_seen_ms) / 1000.f : 0.f; }

bool before_draw(ID3D11DeviceContext* ctx, int base_vertex) {
    const bool on = active();
    if (!on && !watching()) return false;
    const Slot* s = slot(ctx, false);
    if (!s || !s->on) return false;
    if (!on) {  // the big screen shows: only look for the health bar
        float fx, fy;
        if (first_vertex(ctx, base_vertex, &fx, &fy) && fx < kCornerX && fy < kCornerY) g_hud_now = true;
        return false;
    }
    // Everything scales by ui.scale about the centre; the health bar's corner goes to the bottom middle on its own,
    // the top-right alerts in towards the middle.
    float k = g_scale.load(), ox = 0.f, oy = 0.f, fx = 0.f, fy = 0.f;
    if ((moving() || watching()) && first_vertex(ctx, base_vertex, &fx, &fy)) {
        if (fx < kCornerX && fy < kCornerY) g_hud_now = true;
        if (g_hud_centre.load() && fx < kCornerX && fy < kCornerY) {
            k = g_hud_scale.load();
            ox = -k * kAnchorX;
            oy = g_hud_y.load() - k * kAnchorY;
            ++g_cornered;
            grow(g_box, fx, fy);
        } else if (g_alert_on.load() && fx > kAlertCornerX && fy > kAlertCornerY) {
            k = g_alert_scale.load();
            ox = g_alert_x.load() - k * kAlertX;
            oy = g_alert_y.load() - k * kAlertY;
            ++g_alerted;
            grow(g_alert_box, fx, fy);
        }
    }
    if (k > 0.999f && ox == 0.f && oy == 0.f) return false;
    g_vp_n = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetViewports(&g_vp_n, g_vp);
    if (!g_vp_n) return false;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    for (UINT i = 0; i < g_vp_n; ++i) {
        const ui_fit::Rect r = ui_fit::place({g_vp[i].TopLeftX, g_vp[i].TopLeftY, g_vp[i].Width, g_vp[i].Height}, g_fw, g_fh, k, ox, oy);
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
                const ui_fit::Rect r = ui_fit::place(
                    {float(g_sc[i].left), float(g_sc[i].top), float(g_sc[i].right - g_sc[i].left), float(g_sc[i].bottom - g_sc[i].top)}, g_fw, g_fh,
                    k, ox, oy);
                sc[i] = {LONG(r.x), LONG(r.y), LONG(r.x + r.w + 0.999f), LONG(r.y + r.h + 0.999f)};
            }
            if (g_sc_n) ctx->RSSetScissorRects(g_sc_n, sc);
        }
    }
    ++g_shrunk;
    return true;
}

void trace_draw(ID3D11DeviceContext* ctx, unsigned index_count, unsigned start_index, int base_vertex) {
    const Slot* s = slot(ctx, false);
    if (!s || !s->on) return;
    char line[1024];
    int n = snprintf(line, sizeof line, "ui draw: %u idx from %u base %d", index_count, start_index, base_vertex);
    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, off = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &off);
    if (vb) vb->Release();
    n += snprintf(line + n, sizeof line - n, "; vb stride %u off %u", stride, off);
    float fx = 0, fy = 0;
    const float* v = nullptr;
    if (first_vertex(ctx, base_vertex, &fx, &fy, &v))
        n += snprintf(line + n, sizeof line - n, "; vertex 0: %.1f %.1f %.4g %.4g -> frame %.3f %.3f", v[0], v[1], v[2], v[3], fx, fy);
    UINT first = 0, num = 0;
    if (const float* f = draw_constants(ctx, &first, &num)) {
        n += snprintf(line + n, sizeof line - n, "; vs c0 @%u x%u:", first, num);
        for (int i = 0; i < 20 && n < int(sizeof line) - 16; ++i) n += snprintf(line + n, sizeof line - n, "%s%.4g", i % 4 ? " " : " |", f[i]);
    }
    ctx_hooks::trace_note(ctx, line);
}

void after_draw(ID3D11DeviceContext* ctx) {
    ctx->RSSetViewports(g_vp_n, g_vp);
    if (g_sc_n) ctx->RSSetScissorRects(g_sc_n, g_sc);
}

}  // namespace mecvr::ui_shrink
