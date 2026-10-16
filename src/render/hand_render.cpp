// Stage 5 hands (D-031, 5b). See hand_render.h.
#include "render/hand_render.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

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
    float4 shape;     // x radius, y half length, z hand (0 left, 1 right), w unused
    float4 nscale;    // xyz 1 / scale, for normals
    float4 eye;       // xyz camera position (world), w near plane (reverse-Z: depth = near / distance)
    float4 probe[4];  // per hand: palm centre, fingertips (world); w = 1 when the hand is shown
    float4 coll;      // x, y depth size in pixels; z gap kept in front of a surface (m); w give-up depth (m), 0 = off
};
Texture2D<float> scene_depth : register(t14);
Texture2D<uint2> scene_stencil : register(t15);

// 5c: how far along the camera ray a point must come back to sit in front of the scene (1 = not at all).
// The nearest surface around the point's pixel counts; Faith's own body (stencil 0x20) and the sky don't.
float pull(float3 p) {
    float4 c = mul(vp, float4(p, 1));
    if (c.w <= 0.02) return 1;
    float2 ndc = c.xy / c.w;
    if (any(abs(ndc) > 0.98)) return 1;
    int2 px = int2((ndc.x * 0.5 + 0.5) * coll.x, (0.5 - ndc.y * 0.5) * coll.y);
    float best = 0;
    [unroll] for (int i = -2; i <= 2; ++i)
        [unroll] for (int j = -2; j <= 2; ++j) {
            int3 q = int3(clamp(px + int2(i, j) * 6, int2(0, 0), int2(coll.xy) - 1), 0);
            if ((scene_stencil.Load(q).g & 0x20) == 0) best = max(best, scene_depth.Load(q));
        }
    if (best <= 0) return 1;
    float target = eye.w / best - coll.z;  // distance of the surface, minus the gap
    if (c.w <= target || c.w - target > coll.w || target <= 0.05) return 1;
    return target / c.w;
}
// Offset that moves hand h (all of it) back towards the camera.
float3 hand_offset(uint h) {
    if (coll.w <= 0 || probe[2 * h].w < 0.5) return 0;
    float k = min(pull(probe[2 * h].xyz), pull(probe[2 * h + 1].xyz));
    return (probe[2 * h].xyz - eye.xyz) * (k - 1);
}

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
    w.xyz += hand_offset((uint)shape.z);
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

// Contact readback: one pixel per hand (2x1 target), the distance the hand was moved back (m).
struct P { float4 pos : SV_Position; float d : DIST; };
P vs_contact(uint id : SV_VertexID) {
    P o;
    o.pos = float4(id == 0 ? -0.5 : 0.5, 0, 0, 1);
    o.d = length(hand_offset(id));
    return o;
}
float ps_contact(P i) : SV_Target { return i.d; }
)";
constexpr UINT kVerts = 6 * 12 * (2 * (4 + 1) - 1);

struct Constants {
    float world[16], vp[16], color[4], shape[4], nscale[4], eye[4], probe[16], coll[4];
};

std::atomic<bool> g_enabled{true};
std::atomic<float> g_size{0.9f};
std::atomic<bool> g_collide{true}, g_touch_interact{true};
bool g_central[2] = {false, false};  // hand in front of the view (5d), from the latest draw
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
ID3D11ShaderResourceView* g_scene_depth = nullptr;    // the scene depth / stencil as the vertex shader reads them (5c)
ID3D11ShaderResourceView* g_scene_stencil = nullptr;
// Contact readback (5c): a 2x1 target (distance each hand was moved back), copied to a ring of staging textures
// and read a few frames later without waiting.
ID3D11VertexShader* g_vs_contact = nullptr;
ID3D11PixelShader* g_ps_contact = nullptr;
ID3D11Texture2D* g_contact = nullptr;
ID3D11RenderTargetView* g_contact_rtv = nullptr;
constexpr int kRing = 4;
ID3D11Texture2D* g_contact_read[kRing] = {};
unsigned g_contact_n = 0;
uint64_t g_frames = 0;

bool init(ID3D11Device* dev) {
    HMODULE dll = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = dll ? reinterpret_cast<pD3DCompile>(GetProcAddress(dll, "D3DCompile")) : nullptr;
    if (!compile) return false;
    ID3DBlob *vs = nullptr, *ps = nullptr, *vc = nullptr, *pc = nullptr, *err = nullptr;
    if (FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "hands", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs, &err)) ||
        FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "hands", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps, &err)) ||
        FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "hands", nullptr, nullptr, "vs_contact", "vs_5_0", 0, 0, &vc, &err)) ||
        FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "hands", nullptr, nullptr, "ps_contact", "ps_5_0", 0, 0, &pc, &err))) {
        LOG_E("hands", "shader: %s", err ? static_cast<const char*>(err->GetBufferPointer()) : "compile failed");
        if (err) err->Release();
        for (ID3DBlob* b : {vs, ps, vc})
            if (b) b->Release();
        return false;
    }
    bool ok = SUCCEEDED(dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs)) &&
              SUCCEEDED(dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_ps)) &&
              SUCCEEDED(dev->CreateVertexShader(vc->GetBufferPointer(), vc->GetBufferSize(), nullptr, &g_vs_contact)) &&
              SUCCEEDED(dev->CreatePixelShader(pc->GetBufferPointer(), pc->GetBufferSize(), nullptr, &g_ps_contact));
    for (ID3DBlob* b : {vs, ps, vc, pc}) b->Release();
    D3D11_TEXTURE2D_DESC ct{2, 1, 1, 1, DXGI_FORMAT_R32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    ok = ok && SUCCEEDED(dev->CreateTexture2D(&ct, nullptr, &g_contact)) &&
         SUCCEEDED(dev->CreateRenderTargetView(g_contact, nullptr, &g_contact_rtv));
    ct.Usage = D3D11_USAGE_STAGING;
    ct.BindFlags = 0;
    ct.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (auto*& t : g_contact_read) ok = ok && SUCCEEDED(dev->CreateTexture2D(&ct, nullptr, &t));
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
        // 5c: the scene depth and stencil for the vertex shader (stencil 0x20 = Faith's own body).
        if (g_scene_depth) g_scene_depth->Release();
        if (g_scene_stencil) g_scene_stencil->Release();
        g_scene_depth = g_scene_stencil = nullptr;
        if (d.Format == DXGI_FORMAT_R32G8X24_TYPELESS) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
            sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sv.Texture2D.MipLevels = 1;
            sv.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            const bool a = SUCCEEDED(dev->CreateShaderResourceView(depth, &sv, &g_scene_depth));
            sv.Format = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
            const bool b = SUCCEEDED(dev->CreateShaderResourceView(depth, &sv, &g_scene_stencil));
            if (!a || !b) LOG_W("hands", "can't read the scene depth/stencil - hands pass through walls");
        } else {
            LOG_W("hands", "scene depth format %d not handled - hands pass through walls", int(d.Format));
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

// STENCIL trigger (5c recon): the scene's depth-stencil right after TAA, saved as stencil_<n>.bmp (each value
// its own colour) and depth_<n>.bmp (distance, white = near), plus per stencil value: pixel count, distance
// range and mean. Tells whether Faith's own body/arms are marked apart from the world.
std::atomic<bool> g_stencil_req{false};
std::filesystem::path g_dir;
int g_stencil_n = 0;

void save_bmp24(const std::filesystem::path& file, UINT w, UINT h, const std::vector<uint8_t>& rgb) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"wb") != 0 || !f) return;
    const UINT row = (w * 3 + 3) & ~3u, size = 54 + row * h;
    uint8_t hd[54] = {'B', 'M'};
    memcpy(hd + 2, &size, 4);
    const uint32_t off = 54, info = 40, planes_bpp = 1 | (24 << 16);
    memcpy(hd + 10, &off, 4);
    memcpy(hd + 14, &info, 4);
    memcpy(hd + 18, &w, 4);
    memcpy(hd + 22, &h, 4);
    memcpy(hd + 26, &planes_bpp, 4);
    fwrite(hd, 1, 54, f);
    std::vector<uint8_t> line(row, 0);
    for (UINT y = 0; y < h; ++y) {
        const uint8_t* src = rgb.data() + size_t(h - 1 - y) * w * 3;
        for (UINT x = 0; x < w; ++x) {
            line[x * 3] = src[x * 3 + 2];
            line[x * 3 + 1] = src[x * 3 + 1];
            line[x * 3 + 2] = src[x * 3];
        }
        fwrite(line.data(), 1, row, f);
    }
    fclose(f);
}

void stencil_probe(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* depth, const float* vp) {
    D3D11_TEXTURE2D_DESC d;
    depth->GetDesc(&d);
    if (d.Format != DXGI_FORMAT_R32G8X24_TYPELESS) { LOG_W("hands", "stencil probe: depth format %d not handled", int(d.Format)); return; }
    D3D11_TEXTURE2D_DESC s = d;
    s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0;
    s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    s.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(dev->CreateTexture2D(&s, nullptr, &st))) return;
    ctx->CopyResource(st, depth);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) { st->Release(); return; }
    // Reverse-Z infinite: depth = near / view distance; near = the view-projection's z row constant.
    const float near_z = std::fabs(vp[11]) > 1e-6f ? vp[11] : 0.1f;
    const UINT w = d.Width / 2, h = d.Height / 2;
    std::vector<uint8_t> sc(size_t(w) * h * 3), dc(size_t(w) * h * 3);
    uint64_t count[256] = {};
    double sum[256] = {};
    float lo[256], hi[256];
    for (int i = 0; i < 256; ++i) { lo[i] = 1e9f; hi[i] = 0; }
    for (UINT y = 0; y < h; ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(m.pData) + size_t(y * 2) * m.RowPitch;
        for (UINT x = 0; x < w; ++x) {
            float z;
            memcpy(&z, row + size_t(x * 2) * 8, 4);
            const uint8_t sv = row[size_t(x * 2) * 8 + 4];
            const float dist = z > 0 ? near_z / z : 1e4f;
            ++count[sv];
            if (dist < 1e4f) { sum[sv] += dist; lo[sv] = std::fmin(lo[sv], dist); hi[sv] = std::fmax(hi[sv], dist); }
            uint8_t* p = &sc[(size_t(y) * w + x) * 3];
            p[0] = uint8_t(sv * 97 + 40); p[1] = uint8_t(sv * 57 + 90); p[2] = uint8_t(sv * 151 + 10);
            if (sv == 0) p[0] = p[1] = p[2] = 0;
            const uint8_t g = uint8_t(255.f / (1.f + dist));  // 1 m -> 128, 0.3 m -> 196
            uint8_t* q = &dc[(size_t(y) * w + x) * 3];
            q[0] = q[1] = q[2] = g;
        }
    }
    ctx->Unmap(st, 0);
    st->Release();
    const int n = ++g_stencil_n;
    wchar_t name[40];
    swprintf_s(name, L"stencil_%d.bmp", n);
    save_bmp24(g_dir / name, w, h, sc);
    swprintf_s(name, L"depth_%d.bmp", n);
    save_bmp24(g_dir / name, w, h, dc);
    LOG_I("hands", "stencil probe %d (%ux%u, near %.4f):", n, w, h, near_z);
    for (int i = 0; i < 256; ++i)
        if (count[i])
            LOG_I("hands", "  stencil %3d (0x%02x): %6.2f%% of pixels, distance %.2f..%.2f m, mean %.2f", i, i,
                  100.0 * count[i] / (double(w) * h), lo[i] < 1e9f ? lo[i] : 0.f, hi[i],
                  count[i] ? sum[i] / count[i] : 0.0);
}

// 5c contact: a buzz when a hand first meets a surface; 10 s log of how often and how far hands were held back.
bool g_touching[2] = {false, false};
unsigned g_touch_frames[2] = {0, 0}, g_touch_starts[2] = {0, 0}, g_contact_frames = 0;
float g_touch_max[2] = {0, 0};
ULONGLONG g_touch_log = 0;
unsigned g_presses[2] = {0, 0}, g_flicks[2] = {0, 0};
bool g_pressing[2] = {false, false}, g_flicked[2] = {false, false};
void note_contact(int h, float pushed) {
    // 5d: pressing in (over 1 cm) on something in front of you holds Interact; released when the hand comes away.
    // Arriving fast (a flick, PLAYTEST-5k) taps an attack instead - the door slam - and holds nothing.
    // Contact flickers frame to frame (eyes alternate; PLAYTEST-5l: a new press every ~20 ms while held), so a
    // press only ends after 150 ms without contact.
    static ULONGLONG last_contact[2] = {0, 0};
    const ULONGLONG now = GetTickCount64();
    const bool contact = g_touch_interact.load() && g_central[h] && (g_pressing[h] ? pushed > 0.004f : pushed > 0.010f);
    if (contact) last_contact[h] = now;
    const bool press = contact || (g_pressing[h] && now - last_contact[h] < 150);
    if (press && !g_pressing[h]) {
        const float speed = hand_pose::recent_speed(h), flick = hand_pose::flick_speed();
        const char* what;
        // The fist landing just after a punch (6C) is the punch itself (PLAYTEST-6a: the enemy's body was
        // "touched" and Interact pressed after most punches): no slam, no Interact. g_flicked = hold nothing.
        g_flicked[h] = true;
        if (hand_pose::punch_recent(400)) {
            what = "right after a punch: nothing more";
        } else if (flick > 0 && speed >= flick) {
            hand_pose::flick_pulse();
            ++g_flicks[h];
            what = "flick: attack (door slam)";
        } else {
            g_flicked[h] = false;
            ++g_presses[h];
            what = "Interact held";
        }
        LOG_I("hands", "%s hand touched something in front at %.1f m/s -> %s", h ? "right" : "left", speed, what);
    }
    hand_pose::set_touch_press(h, press && !g_flicked[h]);
    g_pressing[h] = press;
    if (!g_touching[h] && pushed > 0.008f) {
        g_touching[h] = true;
        ++g_touch_starts[h];
        hand_pose::contact_pulse(h);
    } else if (g_touching[h] && pushed < 0.003f) {
        g_touching[h] = false;
    }
    if (pushed > 0.003f) ++g_touch_frames[h];
    g_touch_max[h] = std::fmax(g_touch_max[h], pushed);
    if (h == 1) ++g_contact_frames;
    if (GetTickCount64() - g_touch_log < 10000) return;
    g_touch_log = GetTickCount64();
    LOG_I("hands", "walls: left held back in %u of %u frames (max %.0f mm, %u touches, %u interact presses, %u flicks), right %u (max %.0f mm, %u touches, %u presses, %u flicks)",
          g_touch_frames[0], g_contact_frames, g_touch_max[0] * 1000, g_touch_starts[0], g_presses[0], g_flicks[0],
          g_touch_frames[1], g_touch_max[1] * 1000, g_touch_starts[1], g_presses[1], g_flicks[1]);
    g_presses[0] = g_presses[1] = g_flicks[0] = g_flicks[1] = 0;
    g_touch_frames[0] = g_touch_frames[1] = g_touch_starts[0] = g_touch_starts[1] = g_contact_frames = 0;
    g_touch_max[0] = g_touch_max[1] = 0;
}

}  // namespace

void set_enabled(bool on) { g_enabled = on; }
void set_collide(bool on) { g_collide = on; }
void set_touch_interact(bool on) { g_touch_interact = on; }
bool enabled() { return g_enabled.load(); }
void stencil_probe_next(const std::filesystem::path& dir) {
    g_dir = dir;
    g_stencil_req = true;
}
void set_size(float s) { g_size = s < 0.5f ? 0.5f : s > 1.5f ? 1.5f : s; }

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
    if (g_stencil_req.exchange(false)) stencil_probe(dev, ctx, depth, vp);

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
    const float size = g_size.load();

    // Shared constants: camera, and per hand the collision probes (palm centre, fingertips) (5c).
    Constants base{};
    memcpy(base.vp, vp, sizeof(base.vp));
    base.eye[0] = eye.x;
    base.eye[1] = eye.y;
    base.eye[2] = eye.z;
    base.eye[3] = vp[11];  // reverse-Z infinite: the z row is (0, 0, 0, near)
    M3 Hm[2];
    float mirror[2] = {-1.f, 1.f};  // left hand: x -> -x in hand space
    for (int h = 0; h < 2; ++h) {
        if (!hands.valid[h]) continue;
        Hm[h] = m3_of((hands.rot[h] * to_grip).normalized());
        auto world_of = [&](vm::Vec3 c) {
            c = c * size;
            c.x *= mirror[h];
            return hands.pos[h] + Hm[h].c[0] * c.x + Hm[h].c[1] * c.y + Hm[h].c[2] * c.z;
        };
        const vm::Vec3 palm = world_of({0.040f, 0.0f, 0.030f}), tips = world_of({0.020f, 0.005f, -0.075f});
        // In front of the view (within ~40 deg of the view direction; the w row of the view-projection is forward).
        const vm::Vec3 fwd = vm::Vec3{vp[12], vp[13], vp[14]}.normalized();
        g_central[h] = (palm - eye).normalized().dot(fwd) > 0.766f;
        const float pr[8] = {palm.x, palm.y, palm.z, 1, tips.x, tips.y, tips.z, 1};
        memcpy(base.probe + h * 8, pr, sizeof(pr));
    }
    const bool collide = g_collide.load() && g_scene_depth && g_scene_stencil;
    D3D11_TEXTURE2D_DESC dd{};
    depth->GetDesc(&dd);
    base.coll[0] = float(dd.Width);
    base.coll[1] = float(dd.Height);
    base.coll[2] = 0.015f;                // gap kept in front of a surface
    base.coll[3] = collide ? 0.30f : 0.f; // deeper than this, the hand is left inside (hidden by the wall)
    ID3D11ShaderResourceView* old_srv[2] = {};
    ctx->VSGetShaderResources(14, 2, old_srv);
    ID3D11ShaderResourceView* srv[2] = {g_scene_depth, g_scene_stencil};
    if (collide) ctx->VSSetShaderResources(14, 2, srv);

    Part parts[kMaxParts];
    for (int h = 0; h < 2; ++h) {
        if (!hands.valid[h]) continue;
        const int n = build_hand(hands.fingers[h], parts);
        const M3& H = Hm[h];
        for (int i = 0; i < n; ++i) {
            const Part& p = parts[i];
            // world = H * Mirror * R_part * Scale, then position
            const M3 R = m3_of(p.rot);
            vm::Vec3 col[3];
            const float sc[3] = {p.scale.x, p.scale.y, p.scale.z};
            for (int k = 0; k < 3; ++k) {
                vm::Vec3 v = R.c[k] * (sc[k] * size);
                v.x *= mirror[h];
                col[k] = H.c[0] * v.x + H.c[1] * v.y + H.c[2] * v.z;
            }
            vm::Vec3 c = p.centre * size;
            c.x *= mirror[h];
            const vm::Vec3 pos = hands.pos[h] + H.c[0] * c.x + H.c[1] * c.y + H.c[2] * c.z;
            Constants k = base;
            const float w[16] = {col[0].x, col[1].x, col[2].x, pos.x, col[0].y, col[1].y, col[2].y, pos.y,
                                 col[0].z, col[1].z, col[2].z, pos.z, 0, 0, 0, 1};
            memcpy(k.world, w, sizeof(w));
            const float* rgb = p.accent ? kCuff : kGlove;
            memcpy(k.color, rgb, sizeof(float) * 3);
            k.shape[0] = p.radius;  // the size is in the world matrix
            k.shape[1] = p.half;
            k.shape[2] = float(h);
            k.nscale[0] = 1.f / p.scale.x;
            k.nscale[1] = 1.f / p.scale.y;
            k.nscale[2] = 1.f / p.scale.z;
            D3D11_MAPPED_SUBRESOURCE m;
            if (FAILED(ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) continue;
            memcpy(m.pData, &k, sizeof(k));
            ctx->Unmap(g_cb, 0);
            ctx->Draw(kVerts, 0);
        }
    }

    // Contact readback (5c): how far each hand was moved back this frame, read a few frames later.
    if (collide) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, &base, sizeof(base));
            ctx->Unmap(g_cb, 0);
            const D3D11_VIEWPORT one{0, 0, 2, 1, 0, 1};
            ctx->OMSetRenderTargets(1, &g_contact_rtv, nullptr);
            ctx->RSSetViewports(1, &one);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
            ctx->VSSetShader(g_vs_contact, nullptr, 0);
            ctx->PSSetShader(g_ps_contact, nullptr, 0);
            ctx->Draw(2, 0);
            ctx->CopyResource(g_contact_read[g_contact_n % kRing], g_contact);
            ++g_contact_n;
            if (g_contact_n >= kRing) {
                ID3D11Texture2D* t = g_contact_read[g_contact_n % kRing];  // the oldest copy
                if (SUCCEEDED(ctx->Map(t, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
                    float pushed[2];
                    memcpy(pushed, m.pData, sizeof(pushed));
                    ctx->Unmap(t, 0);
                    for (int h = 0; h < 2; ++h) note_contact(h, hands.valid[h] ? pushed[h] : 0.f);
                }
            }
        }
    }
    ctx->VSSetShaderResources(14, 2, old_srv);
    for (auto* v : old_srv)
        if (v) v->Release();
    s.restore(ctx);
    ++g_frames;
}

}  // namespace mecvr::hand_render
