// Per-eye TAA history (D-023). See taa_eyes.h.
//
// Frame trace (1.0.3.47248, 2880x1620; docs/recon/taa/trace_1.txt): the TAA draw reads
//   slot 0 depth, slot 1 motion vectors (RG16F), slot 2 R8 history A, slot 4 colour history (RGBA16F),
//   slot 6 R8 history B
// and writes
//   r0 new R8 history A, r1 scene colour, r2 new colour history, r3 (R8, read by the final composite),
//   r4 new R8 history B.
// The (in, out) history sets swap every frame, so each eye always read the other eye's output. And the
// motion vectors are computed against the previous frame's camera - the other eye's - so they hold the
// eye separation as fake motion (VELOCITY probe: +-24 px in a still scene, ~0 in mono). Both are fixed
// here: this eye's saved history goes in, and motion_fix moves "previous" to this eye's camera.
#include "render/taa_eyes.h"

#include <d3d11.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "diagnostics/log.h"
#include "render/camera_override.h"
#include "render/ctx_hooks.h"
#include "render/hand_render.h"
#include "render/motion_fix.h"

namespace mecvr::taa_eyes {
namespace {

constexpr int kHist = 3;                         // colour history + two R8 histories
constexpr UINT kInSlot[kHist] = {4, 2, 6};       // shader-resource slots the pass reads them from
constexpr UINT kOutTarget[kHist] = {2, 0, 4};    // render targets the pass writes the new ones to
constexpr UINT kDepthSlot = 0, kMotionSlot = 1;
constexpr DXGI_FORMAT kTargetFormats[5] = {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                           DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8_UNORM,
                                           DXGI_FORMAT_R8_UNORM};

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_motion_fix{true};   // diagnostics.taa_motion_fix (A/B)
std::atomic<bool> g_velocity_probe{false};
std::atomic<bool> g_dirty{false};      // toggled by Shift+F9; the main thread saves
ID3D11DeviceContext* g_imm = nullptr;   // the immediate context (the game renders on it alone)
ID3D11Device* g_dev = nullptr;
ID3D11PixelShader* g_ps = nullptr;      // last pixel shader set
// The TAA pass's pixel shaders, learned (BUG-011: in headset sessions the pass ran with a shader other than
// the first one learned and was missed almost every frame; any shader with the pass's inputs and outputs counts).
constexpr int kMaxTaaPs = 8;
ID3D11PixelShader* g_taa_ps[kMaxTaaPs] = {};
int g_taa_ps_n = 0;
bool g_targets_match = false;
// Why draws weren't taken as the TAA pass (logged with the 10 s count; BUG-011).
uint64_t g_n_five = 0, g_n_five_fmt = 0, g_n_other_ctx = 0, g_n_srv = 0;
char g_last_fmt[64] = "";
// What followed a matching 5-target set before a plain Draw (BUG-011): another target set (who called it)
// or a draw of another kind.
bool g_watch = false;
uint64_t g_n_reset = 0, g_n_other_draw = 0;
char g_reset_by[96] = "", g_other_draw[16] = "";

void describe_caller(const void* addr, char* out, size_t size) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<const char*>(addr), &m) && m)
        GetModuleFileNameA(m, path, sizeof(path));
    const char* base = strrchr(path, '\\');
    snprintf(out, size, "%s+%llx", base ? base + 1 : path,
             (unsigned long long)(static_cast<const char*>(addr) - reinterpret_cast<const char*>(m)));
}
ID3D11Resource* g_targets[5] = {};

struct Saved {
    ID3D11Texture2D* tex[kHist] = {};
    bool valid = false;
};
Saved g_saved[2];
int g_pending_eye = -1;
// For the hand pass after the TAA draw (Stage 5): scene depth and this eye's unjittered view-projection.
ID3D11Texture2D* g_draw_depth = nullptr;
float g_draw_vp[16];
bool g_draw_ok = false;
uint64_t g_passes = 0, g_swapped = 0, g_skipped = 0, g_fixed = 0;

// Per-view constant buffer (284 floats; docs/recon/taa/viewcb_1.txt): 40..55 the frame's jittered
// view-projection, 72..87 the same without jitter (rows; clip = M * world). Recorded per eye and frame.
constexpr unsigned kViewCbFloats = 284, kJitteredVp = 40, kVp = 72;
struct Cam {
    float vp[16], vp_jittered[16];
    bool valid = false;
};
struct EyeCams {
    Cam latest, prev;      // this eye's current frame, and its last frame
    uint64_t frame = ~0ull;
};
EyeCams g_cams[2];
struct Jitter { float x, y; int eye; };
constexpr unsigned kJitterLog = 16;
Jitter g_jitter[kJitterLog];
unsigned g_jitter_n = 0;
uint64_t g_frame = 0;      // presents so far (render thread)

ID3D11Texture2D* tex2d(ID3D11Resource* r, D3D11_TEXTURE2D_DESC* d) {
    if (!r) return nullptr;
    D3D11_RESOURCE_DIMENSION dim;
    r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return nullptr;
    auto* t = static_cast<ID3D11Texture2D*>(r);
    t->GetDesc(d);
    return t;
}

ID3D11Texture2D* tex_of(ID3D11ShaderResourceView* v, D3D11_TEXTURE2D_DESC* d) {
    if (!v) return nullptr;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    if (r) r->Release();  // the view keeps it alive
    return tex2d(r, d);
}

void invalidate() { g_saved[0].valid = g_saved[1].valid = false; }

float half_to_float(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    uint32_t bits;
    if (e == 0) bits = s << 31;  // zero / denormal (treated as 0)
    else if (e == 31) bits = (s << 31) | 0x7f800000 | (m << 13);
    else bits = (s << 31) | ((e + 112) << 23) | (m << 13);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

// VELOCITY trigger: mean motion vector of this frame at the TAA pass, in pixels (the texture holds
// previous - current in clip space). For a still camera and scene it should be ~0 when "previous" is
// this eye's camera.
void probe_velocity(ID3D11DeviceContext* ctx, ID3D11Texture2D* motion, const char* when) {
    D3D11_TEXTURE2D_DESC d;
    motion->GetDesc(&d);
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&sd, nullptr, &staging))) return;
    ctx->CopyResource(staging, motion);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
        double sx = 0, sy = 0, ax = 0, ay = 0;
        uint64_t n = 0, bad = 0;
        for (UINT y = 0; y < d.Height; y += 4) {
            const auto* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch);
            for (UINT x = 0; x < d.Width; x += 4) {
                // Clip-space (NDC) units: half the size in pixels.
                const float vx = half_to_float(row[x * 2]) * d.Width * 0.5f, vy = half_to_float(row[x * 2 + 1]) * d.Height * 0.5f;
                if (!(vx == vx) || !(vy == vy) || vx - vx != 0 || vy - vy != 0) { ++bad; continue; }  // NaN / inf
                sx += vx; sy += vy; ax += vx < 0 ? -vx : vx; ay += vy < 0 ? -vy : vy;
                ++n;
            }
        }
        ctx->Unmap(staging, 0);
        if (n)
            LOG_I("taa", "motion vectors %s (eye %d): mean %.2f, %.2f px; mean size %.2f, %.2f px; not finite %llu",
                  when, camera_override::rendering_eye(), sx / n, sy / n, ax / n, ay / n, (unsigned long long)bad);
    }
    staging->Release();
}

// Makes sure saved[eye].tex[i] matches `like`; recreating drops the saved history.
bool ensure(int eye, int i, ID3D11Texture2D* like) {
    D3D11_TEXTURE2D_DESC want;
    like->GetDesc(&want);
    want.Usage = D3D11_USAGE_DEFAULT;
    want.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    want.CPUAccessFlags = 0;
    want.MiscFlags = 0;
    ID3D11Texture2D*& t = g_saved[eye].tex[i];
    if (t) {
        D3D11_TEXTURE2D_DESC have;
        t->GetDesc(&have);
        if (have.Width == want.Width && have.Height == want.Height && have.Format == want.Format &&
            have.MipLevels == want.MipLevels && have.ArraySize == want.ArraySize)
            return true;
        t->Release();
        t = nullptr;
        g_saved[eye].valid = false;
    }
    if (FAILED(g_dev->CreateTexture2D(&want, nullptr, &t))) {
        LOG_E("taa", "could not create a %ux%u history copy - per-eye TAA off", want.Width, want.Height);
        g_enabled = false;
        return false;
    }
    g_saved[eye].valid = false;
    return true;
}

}  // namespace

void set_enabled(bool on) {
    g_enabled = on;
    invalidate();
    LOG_I("taa", "per-eye TAA history %s", on ? "ON" : "OFF");
}
bool enabled() { return g_enabled.load(); }
bool take_changed() { return g_dirty.exchange(false); }
void set_motion_fix(bool on) {
    g_motion_fix = on;
    invalidate();
    LOG_I("taa", "motion-vector fix %s", on ? "ON" : "OFF");
}
void probe_velocity_next() { g_velocity_probe = true; }

void on_present(IDXGISwapChain* chain) {
    ++g_frame;
    static bool was = false;
    static ULONGLONG last_toggle = 0;
    const bool now = (GetAsyncKeyState(VK_F9) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
    if (now && !was && GetTickCount64() - last_toggle > 700) {
        last_toggle = GetTickCount64();
        set_enabled(!g_enabled.load());
        g_dirty = true;
    }
    was = now;
    if (!g_imm) {
        if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_dev)))) return;
        g_dev->GetImmediateContext(&g_imm);
        g_imm->Release();  // the device keeps it alive; we hold the device
    }
    static ULONGLONG last = GetTickCount64();
    if (GetTickCount64() - last >= 10000) {
        last = GetTickCount64();
        if (g_enabled.load())
            LOG_I("taa", "TAA passes %llu: per-eye history %llu, motion fixed %llu, skipped %llu (mono / eye unknown / first frame); "
                  "5-target sets %llu, other formats %llu%s%s, other context %llu, inputs not matched %llu, shaders %d; "
                  "replaced before the draw %llu (last by %s), other draw kind %llu (%s)",
                  (unsigned long long)g_passes, (unsigned long long)g_swapped, (unsigned long long)g_fixed,
                  (unsigned long long)g_skipped, (unsigned long long)g_n_five, (unsigned long long)g_n_five_fmt,
                  g_last_fmt[0] ? " last " : "", g_last_fmt, (unsigned long long)g_n_other_ctx,
                  (unsigned long long)g_n_srv, g_taa_ps_n, (unsigned long long)g_n_reset, g_reset_by,
                  (unsigned long long)g_n_other_draw, g_other_draw);
        // BUG-011: the pass's targets are set but its draw is missed - trace two frames once to see why.
        static bool traced = false;
        if (g_enabled.load() && !traced && g_n_five > 300 && g_passes * 4 < g_n_five) {
            traced = true;
            ctx_hooks::request_trace();
            LOG_I("taa", "TAA pass missed on most frames - tracing two frames (trace_<n>.txt)");
        }
        g_passes = g_swapped = g_fixed = g_skipped = 0;
        g_n_five =g_n_five_fmt = g_n_other_ctx = g_n_srv = g_n_reset = g_n_other_draw = 0;
        g_last_fmt[0] = g_reset_by[0] = g_other_draw[0] = 0;
    }
}

void record_view_cb(const float* f, unsigned count) {
    if (count != kViewCbFloats) return;
    const int eye = camera_override::rendering_eye();
    if (eye < 0) return;
    EyeCams& c = g_cams[eye];
    if (c.frame != g_frame) {  // first upload of this eye's frame: its last frame becomes "previous"
        c.prev = c.latest;
        c.frame = g_frame;
        // Jitter log (VELOCITY trigger prints it): projection [26], [30] are the sub-pixel offsets.
        g_jitter[g_jitter_n % kJitterLog] = {f[26], f[30], eye};
        ++g_jitter_n;
    }
    memcpy(c.latest.vp, f + kVp, sizeof(c.latest.vp));
    memcpy(c.latest.vp_jittered, f + kJitteredVp, sizeof(c.latest.vp_jittered));
    c.latest.valid = true;
}

void on_set_ps(ID3D11DeviceContext* ctx, ID3D11PixelShader* ps) {
    if (ctx == g_imm) g_ps = ps;
}

void on_other_draw(ID3D11DeviceContext* ctx, const char* kind) {
    if (ctx != g_imm || !g_watch) return;
    g_watch = false;
    ++g_n_other_draw;
    snprintf(g_other_draw, sizeof(g_other_draw), "%s", kind);
}

void on_set_targets(ID3D11DeviceContext* ctx, unsigned count, ID3D11RenderTargetView* const* rtvs, const void* caller) {
    if (count == 5 && ctx != g_imm) ++g_n_other_ctx;
    if (ctx != g_imm) return;
    if (g_watch) {
        g_watch = false;
        ++g_n_reset;
        describe_caller(caller, g_reset_by, sizeof(g_reset_by));
        const size_t len = strlen(g_reset_by);
        snprintf(g_reset_by + len, sizeof(g_reset_by) - len, " n=%u", count);
    }
    g_targets_match = false;
    if (count != 5 || !rtvs) return;
    ++g_n_five;
    ID3D11Resource* res[5] = {};
    D3D11_TEXTURE2D_DESC desc[5] = {};
    bool same = true;
    for (int i = 0; i < 5; ++i) {
        if (rtvs[i]) rtvs[i]->GetResource(&res[i]);
        if (res[i]) res[i]->Release();  // the view keeps it alive
        same = same && tex2d(res[i], &desc[i]) && desc[i].Format == kTargetFormats[i] &&
               desc[i].Width == desc[0].Width && desc[i].Height == desc[0].Height;
    }
    if (!same) {
        ++g_n_five_fmt;
        snprintf(g_last_fmt, sizeof(g_last_fmt), "%d,%d,%d,%d,%d @%ux%u", int(desc[0].Format), int(desc[1].Format),
                 int(desc[2].Format), int(desc[3].Format), int(desc[4].Format), desc[0].Width, desc[0].Height);
        return;
    }
    for (int i = 0; i < 5; ++i) g_targets[i] = res[i];
    g_targets_match = true;
    // Watch for what comes next, unless this set is our own restore after the pass.
    HMODULE from = nullptr, self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<const char*>(caller), &from);
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<const char*>(&describe_caller), &self);
    g_watch = from != self;
}

bool before_draw(ID3D11DeviceContext* ctx) {
    if (ctx == g_imm) g_watch = false;
    // The velocity probe and the hands also run with per-eye history off.
    if ((!g_enabled.load() && !g_velocity_probe.load() && !hand_render::enabled()) || ctx != g_imm || !g_targets_match)
        return false;
    ID3D11ShaderResourceView* srv[7] = {};
    ctx->PSGetShaderResources(0, 7, srv);
    D3D11_TEXTURE2D_DESC d{};
    ID3D11Texture2D* in[kHist] = {};
    bool ok = true;
    for (int i = 0; i < kHist && ok; ++i) ok = (in[i] = tex_of(srv[kInSlot[i]], &d)) != nullptr;
    ID3D11Texture2D* motion = ok ? tex_of(srv[kMotionSlot], &d) : nullptr;
    ok = ok && motion && d.Format == DXGI_FORMAT_R16G16_FLOAT && srv[kDepthSlot];
    if (!ok) ++g_n_srv;
    ID3D11ShaderResourceView* depth = srv[kDepthSlot];
    if (depth) depth->AddRef();
    for (auto* v : srv)
        if (v) v->Release();
    bool acted = false;
    const bool probe = ok && g_velocity_probe.exchange(false);
    if (probe) probe_velocity(ctx, motion, "as the game made them");
    if (probe) {
        char line[512] = "";
        size_t len = 0;
        for (unsigned i = 0; i < kJitterLog && g_jitter_n >= kJitterLog; ++i) {
            const Jitter& j = g_jitter[(g_jitter_n + i) % kJitterLog];
            len += snprintf(line + len, sizeof(line) - len, " %c(%.2f,%.2f)", j.eye ? 'R' : 'L', j.x * 1440, j.y * 810);
        }
        LOG_I("taa", "jitter (px), oldest first:%s", line);
        // Where the screen centre (a point ~straight ahead) lands under the recorded cameras, in px from the
        // centre: tells whether "this eye's previous" really is an older camera (BUG-011 / PLAYTEST-5f ghosting).
        const int e = camera_override::rendering_eye();
        if (e >= 0) {
            const EyeCams& own = g_cams[e];
            const EyeCams& oth = g_cams[1 - e];
            float inv[16];
            if (vm::invert4(own.latest.vp, inv)) {
                auto mul = [](const float* m, const float* p, float* o) {
                    for (int r = 0; r < 4; ++r) o[r] = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3] * p[3];
                };
                const float ndc[4] = {0, 0, 0.001f, 1};
                float w[4];
                mul(inv, ndc, w);
                for (float& c : w) c /= w[3];
                auto px = [&](const Cam& c, float* out) {
                    float p[4];
                    mul(c.vp, w, p);
                    out[0] = p[0] / p[3] * 720;
                    out[1] = -p[1] / p[3] * 405;
                };
                float a[2], b[2], c[2];
                px(own.prev, a);
                px(oth.latest, b);
                px(oth.prev, c);
                LOG_I("taa", "centre under cameras (eye %d, frame %llu; own %llu, other %llu): own prev %.1f,%.1f  other latest %.1f,%.1f  other prev %.1f,%.1f px",
                      e, (unsigned long long)g_frame, (unsigned long long)own.frame, (unsigned long long)oth.frame, a[0], a[1], b[0], b[1], c[0], c[1]);
            }
        }
    }
    if (ok && g_enabled.load()) {
        bool known = false;
        for (int i = 0; i < g_taa_ps_n; ++i) known = known || g_taa_ps[i] == g_ps;
        if (!known && g_taa_ps_n < kMaxTaaPs) {
            g_taa_ps[g_taa_ps_n++] = g_ps;
            LOG_I("taa", "TAA pass found (pixel shader %p, #%d) - per-eye history active", static_cast<void*>(g_ps), g_taa_ps_n);
        }
        ++g_passes;
        const int eye = camera_override::rendering_eye();
        const EyeCams& own = g_cams[eye < 0 ? 0 : eye];
        const EyeCams& other = g_cams[eye < 0 ? 0 : 1 - eye];
        // The engine's "previous" camera is the other eye's last frame; this eye's is its own last frame.
        const bool cams_ok = eye >= 0 && own.frame == g_frame && own.latest.valid && own.prev.valid &&
                             other.frame + 1 == g_frame && other.latest.valid;
        if (!cams_ok) {
            invalidate();
            ++g_skipped;
        } else {
            for (int i = 0; i < kHist && ok; ++i) ok = ensure(eye, i, in[i]);
            if (ok && g_motion_fix.load() &&
                motion_fix::apply(g_dev, ctx, depth, motion, own.latest.vp_jittered, other.latest.vp, own.prev.vp,
                                  other.latest.vp_jittered, own.prev.vp_jittered)) {
                ++g_fixed;
                if (probe) probe_velocity(ctx, motion, "after the fix");
            }
            if (ok && g_saved[eye].valid) {
                for (int i = 0; i < kHist; ++i) ctx->CopyResource(in[i], g_saved[eye].tex[i]);
                ++g_swapped;
            } else {
                ++g_skipped;
            }
            if (ok) {
                g_pending_eye = eye;
                acted = true;
            }
        }
    }
    // Hands after the pass, with or without per-eye history: this eye's camera of this frame.
    if (ok && hand_render::enabled()) {
        const int eye = camera_override::rendering_eye();
        const EyeCams* own = eye >= 0 ? &g_cams[eye] : nullptr;
        D3D11_TEXTURE2D_DESC dd{};
        if (own && own->frame == g_frame && own->latest.valid && (g_draw_depth = tex_of(depth, &dd)) != nullptr) {
            memcpy(g_draw_vp, own->latest.vp, sizeof(g_draw_vp));
            g_draw_ok = true;
            acted = true;
        }
    }
    if (depth) depth->Release();
    return acted;
}

void after_draw(ID3D11DeviceContext* ctx) {
    const int eye = g_pending_eye;
    g_pending_eye = -1;
    if (eye >= 0) {
        for (int i = 0; i < kHist; ++i) ctx->CopyResource(g_saved[eye].tex[i], g_targets[kOutTarget[i]]);
        g_saved[eye].valid = true;
    }
    // Hands go into the anti-aliased HDR scene colour (r1), before bloom and tonemap.
    if (g_draw_ok) {
        D3D11_TEXTURE2D_DESC sd{};
        hand_render::draw(g_dev, ctx, tex2d(g_targets[1], &sd), g_draw_depth, g_draw_vp);
        g_draw_ok = false;
    }
}

}  // namespace mecvr::taa_eyes
