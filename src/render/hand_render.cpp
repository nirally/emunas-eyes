// Stage 5 hands (D-031, 5b). See hand_render.h.
#include "render/hand_render.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>

#include "core/vmath.h"
#include "diagnostics/log.h"
#include "render/camera_override.h"
#include "render/d3d_state.h"

namespace mecvr::hand_render {
namespace {

// One capsule per draw, built from SV_VertexID: radius r, straight part from z = -half to +half (local),
// hemispherical caps; `world` places it (rotation, per-axis scale, mirror for the left hand, position).
// Lit by a key light from above/behind the viewer plus ambient and a soft rim, so the shape reads in the
// headset. Reverse-Z depth test and write, against a copy of the scene depth (fingers hide each other).
constexpr char kHlsl[] = R"(
cbuffer C : register(b13) {
    row_major float4x4 world;
    row_major float4x4 vp;
    float4 color;
    float4 shape;   // x radius, y half length, zw unused
    float4 nscale;  // xyz 1 / scale, for normals
    float4 eye;     // xyz camera position (world)
};
struct V { float4 pos : SV_Position; float3 n : NORMAL; float3 w : WORLDPOS; };
static const uint kAround = 12, kCap = 4, kRings = 2 * (kCap + 1);
static const uint2 kCorner[6] = { uint2(0,0), uint2(1,0), uint2(1,1), uint2(0,0), uint2(1,1), uint2(0,1) };
V vs(uint id : SV_VertexID) {
    uint quad = id / 6;
    uint2 c = kCorner[id % 6];
    uint ring = quad / kAround + c.x, seg = (quad % kAround + c.y) % kAround;
    float theta = 6.2831853 * seg / kAround;
    // rings 0..kCap: bottom cap (pole -> equator); kCap+1..: top cap (equator -> pole)
    bool top = ring > kCap;
    float phi = top ? 1.5707963 * (ring - kCap - 1) / kCap : -1.5707963 + 1.5707963 * ring / kCap;
    float3 n = float3(cos(phi) * cos(theta), cos(phi) * sin(theta), sin(phi));
    float3 p = n * shape.x + float3(0, 0, top ? shape.y : -shape.y);
    V o;
    float4 w = mul(world, float4(p, 1));
    o.w = w.xyz;
    o.pos = mul(vp, w);
    o.n = normalize(mul((float3x3)world, n * nscale.xyz));
    return o;
}
float4 ps(V i) : SV_Target {
    float3 n = normalize(i.n);
    float3 v = normalize(eye.xyz - i.w);
    float key = saturate(dot(n, normalize(float3(0.3, 0.8, 0.5))));
    float fill = 0.5 + 0.5 * dot(n, v);
    float rim = pow(1 - saturate(dot(n, v)), 3);
    float3 c = color.rgb * (0.30 + 0.55 * key + 0.25 * fill) + rim * 0.06;
    return float4(c, 1);
}
)";
constexpr UINT kVerts = 6 * 12 * (2 * (4 + 1) - 1);

struct Constants {
    float world[16], vp[16], color[4], shape[4], nscale[4], eye[4];
};

std::atomic<bool> g_enabled{true};
bool g_ready = false, g_failed = false;
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11Buffer* g_cb = nullptr;
ID3D11BlendState* g_blend = nullptr;
ID3D11DepthStencilState* g_depth = nullptr;
ID3D11RasterizerState* g_raster = nullptr;
ID3D11Texture2D* g_rtv_tex = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
ID3D11Texture2D* g_depth_src = nullptr;   // the scene depth the copy was made for
ID3D11Texture2D* g_depth_copy = nullptr;  // our writable copy of it
ID3D11DepthStencilView* g_dsv = nullptr;
uint64_t g_frames = 0;

bool init(ID3D11Device* dev) {
    HMODULE dll = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = dll ? reinterpret_cast<pD3DCompile>(GetProcAddress(dll, "D3DCompile")) : nullptr;
    if (!compile) return false;
    ID3DBlob *vs = nullptr, *ps = nullptr, *err = nullptr;
    if (FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "hands", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs, &err)) ||
        FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "hands", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps, &err))) {
        LOG_E("hands", "shader: %s", err ? static_cast<const char*>(err->GetBufferPointer()) : "compile failed");
        if (err) err->Release();
        if (vs) vs->Release();
        return false;
    }
    bool ok = SUCCEEDED(dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs)) &&
              SUCCEEDED(dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_ps));
    vs->Release();
    ps->Release();
    D3D11_BUFFER_DESC cb{sizeof(Constants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;  // opaque
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;  // our own copy: the hand's parts hide each other
    dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;   // reverse-Z: nearer = larger
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    return ok && SUCCEEDED(dev->CreateBuffer(&cb, nullptr, &g_cb)) && SUCCEEDED(dev->CreateBlendState(&bd, &g_blend)) &&
           SUCCEEDED(dev->CreateDepthStencilState(&dd, &g_depth)) && SUCCEEDED(dev->CreateRasterizerState(&rd, &g_raster));
}

DXGI_FORMAT dsv_format(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R32G8X24_TYPELESS ? DXGI_FORMAT_D32_FLOAT_S8X24_UINT
           : f == DXGI_FORMAT_R24G8_TYPELESS  ? DXGI_FORMAT_D24_UNORM_S8_UINT
           : f == DXGI_FORMAT_R32_TYPELESS    ? DXGI_FORMAT_D32_FLOAT
           : f == DXGI_FORMAT_R16_TYPELESS    ? DXGI_FORMAT_D16_UNORM
                                              : f;
}

bool views(ID3D11Device* dev, ID3D11Texture2D* color, ID3D11Texture2D* depth) {
    if (color != g_rtv_tex) {
        if (g_rtv) g_rtv->Release();
        g_rtv = nullptr;
        g_rtv_tex = SUCCEEDED(dev->CreateRenderTargetView(color, nullptr, &g_rtv)) ? color : nullptr;
    }
    if (depth != g_depth_src) {
        if (g_dsv) g_dsv->Release();
        if (g_depth_copy) g_depth_copy->Release();
        g_dsv = nullptr;
        g_depth_copy = nullptr;
        g_depth_src = nullptr;
        D3D11_TEXTURE2D_DESC d;
        depth->GetDesc(&d);
        D3D11_TEXTURE2D_DESC c = d;
        c.Usage = D3D11_USAGE_DEFAULT;
        c.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        c.CPUAccessFlags = 0;
        c.MiscFlags = 0;
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dv.Format = dsv_format(d.Format);
        if (SUCCEEDED(dev->CreateTexture2D(&c, nullptr, &g_depth_copy)) &&
            SUCCEEDED(dev->CreateDepthStencilView(g_depth_copy, &dv, &g_dsv))) {
            g_depth_src = depth;
            LOG_I("hands", "depth copy %ux%u format %d", d.Width, d.Height, int(d.Format));
        } else {
            LOG_W("hands", "no depth copy for format %d - hands drawn without occlusion", int(d.Format));
            if (g_depth_copy) g_depth_copy->Release();
            g_depth_copy = nullptr;
            g_depth_src = depth;  // don't retry every frame
        }
    }
    return g_rtv != nullptr;
}

// --- the hand (5b) ---
// Built in "hand space" for a right hand, from the OpenXR grip pose: origin = the centre of the
// controller's handle; X = grip X (the palm faces -X, onto the handle), Y = grip -Z (along the handle,
// towards the index finger and thumb), Z = grip +Y, so straight fingers point along -Z. Fingers curl about
// their own Y, towards the palm (-X), around the handle. The left hand is the mirror image (x -> -x).
// Sizes in metres (a gloved adult hand).
struct M3 { vm::Vec3 c[3]; };  // columns: local x, y, z axes

M3 m3_of(vm::Quat q) { return {{q.rotate({1, 0, 0}), q.rotate({0, 1, 0}), q.rotate({0, 0, 1})}}; }
vm::Quat rx(float a) { return vm::Quat::axis_angle({1, 0, 0}, a); }
vm::Quat ry(float a) { return vm::Quat::axis_angle({0, 1, 0}, a); }
vm::Quat rz(float a) { return vm::Quat::axis_angle({0, 0, 1}, a); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }

struct Part {
    vm::Quat rot;  // hand space; the capsule's axis is its local Z
    vm::Vec3 centre;
    float radius, half;
    vm::Vec3 scale{1, 1, 1};
    bool accent = false;
};

struct Finger {
    vm::Vec3 knuckle;
    float spread;      // about hand X (+ = towards the thumb side)
    float len[3], rad[3];
};
const Finger kFingers[4] = {
    {{0.036f, 0.032f, -0.010f}, 0.08f, {0.042f, 0.026f, 0.021f}, {0.0098f, 0.0092f, 0.0086f}},   // index
    {{0.038f, 0.011f, -0.014f}, 0.0f, {0.046f, 0.029f, 0.022f}, {0.0100f, 0.0094f, 0.0088f}},    // middle
    {{0.036f, -0.010f, -0.010f}, -0.06f, {0.043f, 0.027f, 0.021f}, {0.0095f, 0.0090f, 0.0084f}}, // ring
    {{0.032f, -0.030f, -0.002f}, -0.14f, {0.034f, 0.021f, 0.019f}, {0.0086f, 0.0081f, 0.0076f}}, // pinky
};

// A chain of three bones from `start`, base orientation `base`, each bent by its angle about local `axis`.
int chain(Part* out, vm::Vec3 start, vm::Quat base, const float* len, const float* rad, const float* bend, vm::Vec3 axis) {
    vm::Vec3 p = start;
    vm::Quat r = base;
    for (int i = 0; i < 3; ++i) {
        r = (r * vm::Quat::axis_angle(axis, bend[i])).normalized();
        const vm::Vec3 dir = r.rotate({0, 0, -1});
        out[i] = {r, p + dir * (len[i] * 0.5f), rad[i], len[i] * 0.5f};
        p = p + dir * len[i];
    }
    return 3;
}

int build_hand(const hand_pose::Fingers& f, Part* parts) {
    int n = 0;
    // Palm: a flattened capsule beside the handle; wrist and cuff behind it.
    parts[n++] = {vm::Quat{}, {0.040f, 0.000f, 0.030f}, 0.044f, 0.020f, {0.36f, 1.0f, 0.80f}};
    parts[n++] = {vm::Quat{}, {0.042f, -0.004f, 0.095f}, 0.030f, 0.012f, {0.70f, 1.0f, 1.0f}};
    parts[n++] = {vm::Quat{}, {0.042f, -0.004f, 0.108f}, 0.0305f, 0.004f, {0.72f, 1.02f, 1.0f}, true};  // wrist band
    parts[n++] = {vm::Quat{}, {0.042f, -0.004f, 0.150f}, 0.028f, 0.035f, {0.74f, 1.0f, 1.0f}};  // forearm (sleeve)
    for (int k = 0; k < 4; ++k) {
        const float c = k == 0 ? f.index : f.others;
        const float bend[3] = {lerp(0.15f, 1.35f, c), lerp(0.18f, 1.60f, c), lerp(0.08f, 0.95f, c)};
        n += chain(parts + n, kFingers[k].knuckle, rx(kFingers[k].spread), kFingers[k].len, kFingers[k].rad, bend, {0, 1, 0});
    }
    // Thumb: from the base of the palm, forwards, up and in over the top of the controller; resting on the
    // buttons it lies lower and bends at both joints.
    const float t = f.thumb;
    const vm::Quat base = (ry(lerp(0.55f, 0.75f, t)) * rx(lerp(0.75f, 0.45f, t)) * rz(-0.4f)).normalized();
    const float len[3] = {0.040f, 0.032f, 0.027f}, rad[3] = {0.0125f, 0.0108f, 0.0098f};
    const float bend[3] = {0.0f, lerp(-0.10f, -0.45f, t), lerp(-0.12f, -0.50f, t)};
    n += chain(parts + n, {0.030f, 0.020f, 0.050f}, base, len, rad, bend, {1, 0, 0});
    return n;
}

constexpr int kMaxParts = 4 + 4 * 3 + 3;

}  // namespace

void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled.load(); }

void draw(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* scene_color, ID3D11Texture2D* depth,
          const float* vp) {
    if (!g_enabled.load() || g_failed || !scene_color || !depth) return;
    const camera_override::Hands hands = camera_override::rendered_hands();
    if (!hands.valid[0] && !hands.valid[1]) return;
    if (!g_ready) {
        if (!init(dev)) { g_failed = true; LOG_E("hands", "hand drawing unavailable"); return; }
        g_ready = true;
        LOG_I("hands", "hand drawing ready");
    }
    if (!views(dev, scene_color, depth)) return;
    D3D11_TEXTURE2D_DESC d;
    scene_color->GetDesc(&d);

    d3d_state::State s;
    s.save(ctx);
    if (g_depth_copy) ctx->CopyResource(g_depth_copy, depth);
    const D3D11_VIEWPORT viewport{0, 0, float(d.Width), float(d.Height), 0, 1};
    const float zero[4] = {};
    ctx->OMSetRenderTargets(1, &g_rtv, g_dsv);
    ctx->OMSetBlendState(g_blend, zero, 0xffffffff);
    ctx->OMSetDepthStencilState(g_depth, 0);
    ctx->RSSetState(g_raster);
    ctx->RSSetViewports(1, &viewport);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_vs, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(g_ps, nullptr, 0);
    ctx->VSSetConstantBuffers(d3d_state::kCbSlot, 1, &g_cb);
    ctx->PSSetConstantBuffers(d3d_state::kCbSlot, 1, &g_cb);

    // The camera position from the view-projection (where its x, y and w rows are zero), for the rim light.
    vm::Vec3 eye{};
    {
        float inv[16];
        // The camera maps to clip (0, 0, z, 0), so it is the inverse's third column (homogeneous).
        if (vm::invert4(vp, inv) && std::fabs(inv[14]) > 1e-12f) eye = {inv[2] / inv[14], inv[6] / inv[14], inv[10] / inv[14]};
    }

    // Glove: near-black charcoal with a thin dark-red wrist band (Faith's colours), in the scene's HDR range.
    const float kGlove[3] = {0.065f, 0.062f, 0.066f}, kCuff[3] = {0.32f, 0.030f, 0.028f};
    const vm::Quat to_grip = rx(-1.5707963f);  // hand space -> grip space (Y = grip -Z, Z = grip +Y)
    Part parts[kMaxParts];
    for (int h = 0; h < 2; ++h) {
        if (!hands.valid[h]) continue;
        const int n = build_hand(hands.fingers[h], parts);
        const vm::Quat hand_rot = (hands.rot[h] * to_grip).normalized();
        const M3 H = m3_of(hand_rot);
        const float mirror = h == 0 ? -1.f : 1.f;  // left hand: x -> -x in hand space
        for (int i = 0; i < n; ++i) {
            const Part& p = parts[i];
            // world = H * Mirror * R_part * Scale, then position
            const M3 R = m3_of(p.rot);
            vm::Vec3 col[3];
            const float sc[3] = {p.scale.x, p.scale.y, p.scale.z};
            for (int k = 0; k < 3; ++k) {
                vm::Vec3 v = R.c[k] * sc[k];
                v.x *= mirror;
                col[k] = H.c[0] * v.x + H.c[1] * v.y + H.c[2] * v.z;
            }
            vm::Vec3 c = p.centre;
            c.x *= mirror;
            const vm::Vec3 pos = hands.pos[h] + H.c[0] * c.x + H.c[1] * c.y + H.c[2] * c.z;
            Constants k{};
            const float w[16] = {col[0].x, col[1].x, col[2].x, pos.x, col[0].y, col[1].y, col[2].y, pos.y,
                                 col[0].z, col[1].z, col[2].z, pos.z, 0, 0, 0, 1};
            memcpy(k.world, w, sizeof(w));
            memcpy(k.vp, vp, sizeof(k.vp));
            const float* rgb = p.accent ? kCuff : kGlove;
            memcpy(k.color, rgb, sizeof(float) * 3);
            k.shape[0] = p.radius;
            k.shape[1] = p.half;
            k.nscale[0] = 1.f / p.scale.x;
            k.nscale[1] = 1.f / p.scale.y;
            k.nscale[2] = 1.f / p.scale.z;
            k.eye[0] = eye.x;
            k.eye[1] = eye.y;
            k.eye[2] = eye.z;
            D3D11_MAPPED_SUBRESOURCE m;
            if (FAILED(ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) continue;
            memcpy(m.pData, &k, sizeof(k));
            ctx->Unmap(g_cb, 0);
            ctx->Draw(kVerts, 0);
        }
    }
    s.restore(ctx);
    ++g_frames;
}

}  // namespace mecvr::hand_render
