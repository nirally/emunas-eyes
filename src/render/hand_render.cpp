// Stage 5 hands (D-031). See hand_render.h.
#include "render/hand_render.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <atomic>
#include <cstring>

#include "core/vmath.h"
#include "diagnostics/log.h"
#include "render/camera_override.h"
#include "render/d3d_state.h"

namespace mecvr::hand_render {
namespace {

// A unit cube from SV_VertexID (36 vertices), scaled/placed by `world`, shaded by a simple key light
// along the view plus ambient, so the shape reads in the headset. Reverse-Z depth test against the scene.
constexpr char kHlsl[] = R"(
cbuffer C : register(b13) {
    row_major float4x4 world;
    row_major float4x4 vp;
    float4 color;
};
struct V { float4 pos : SV_Position; float3 n : NORMAL; };
static const float3 kCorner[8] = { float3(-.5,-.5,-.5), float3(.5,-.5,-.5), float3(.5,.5,-.5), float3(-.5,.5,-.5),
                                   float3(-.5,-.5,.5), float3(.5,-.5,.5), float3(.5,.5,.5), float3(-.5,.5,.5) };
static const uint kIdx[36] = { 0,2,1, 0,3,2,  4,5,6, 4,6,7,  0,1,5, 0,5,4,  3,7,6, 3,6,2,  0,4,7, 0,7,3,  1,2,6, 1,6,5 };
static const float3 kN[6] = { float3(0,0,-1), float3(0,0,1), float3(0,-1,0), float3(0,1,0), float3(-1,0,0), float3(1,0,0) };
V vs(uint id : SV_VertexID) {
    V o;
    float4 w = mul(world, float4(kCorner[kIdx[id]], 1));
    o.pos = mul(vp, w);
    o.n = normalize(mul((float3x3)world, kN[id / 6]));
    return o;
}
float4 ps(V i) : SV_Target {
    float l = 0.35 + 0.65 * saturate(dot(normalize(i.n), normalize(float3(0.3, 0.8, 0.5))));
    return float4(color.rgb * l, 1);
}
)";

struct Constants {
    float world[16], vp[16], color[4];
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
ID3D11Texture2D* g_dsv_tex = nullptr;
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
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;  // reverse-Z: nearer = larger
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    return ok && SUCCEEDED(dev->CreateBuffer(&cb, nullptr, &g_cb)) && SUCCEEDED(dev->CreateBlendState(&bd, &g_blend)) &&
           SUCCEEDED(dev->CreateDepthStencilState(&dd, &g_depth)) && SUCCEEDED(dev->CreateRasterizerState(&rd, &g_raster));
}

bool views(ID3D11Device* dev, ID3D11Texture2D* color, ID3D11Texture2D* depth) {
    if (color != g_rtv_tex) {
        if (g_rtv) g_rtv->Release();
        g_rtv = nullptr;
        g_rtv_tex = SUCCEEDED(dev->CreateRenderTargetView(color, nullptr, &g_rtv)) ? color : nullptr;
    }
    if (depth != g_dsv_tex) {
        if (g_dsv) g_dsv->Release();
        g_dsv = nullptr;
        g_dsv_tex = nullptr;
        D3D11_TEXTURE2D_DESC d;
        depth->GetDesc(&d);
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dv.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;  // may stay bound as a shader input
        dv.Format = d.Format == DXGI_FORMAT_R32G8X24_TYPELESS ? DXGI_FORMAT_D32_FLOAT_S8X24_UINT
                    : d.Format == DXGI_FORMAT_R24G8_TYPELESS  ? DXGI_FORMAT_D24_UNORM_S8_UINT
                    : d.Format == DXGI_FORMAT_R32_TYPELESS    ? DXGI_FORMAT_D32_FLOAT
                                                              : d.Format;
        if (SUCCEEDED(dev->CreateDepthStencilView(depth, &dv, &g_dsv))) g_dsv_tex = depth;
        else LOG_W("hands", "no depth view for format %d - hands drawn without occlusion", d.Format);
    }
    return g_rtv != nullptr;
}

// Row-major world matrix: rotation q, scale (sx, sy, sz) along the hand's own axes, then position p.
void world_matrix(vm::Quat q, vm::Vec3 p, vm::Vec3 s, float* m) {
    const vm::Vec3 x = q.rotate({s.x, 0, 0}), y = q.rotate({0, s.y, 0}), z = q.rotate({0, 0, s.z});
    const float r[16] = {x.x, y.x, z.x, p.x, x.y, y.y, z.y, p.y, x.z, y.z, z.z, p.z, 0, 0, 0, 1};
    memcpy(m, r, sizeof(r));
}

void box(ID3D11DeviceContext* ctx, const float* vp, vm::Quat q, vm::Vec3 p, vm::Vec3 size, const float* rgb) {
    Constants k{};
    world_matrix(q, p, size, k.world);
    memcpy(k.vp, vp, sizeof(k.vp));
    memcpy(k.color, rgb, sizeof(float) * 3);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &k, sizeof(k));
    ctx->Unmap(g_cb, 0);
    ctx->Draw(36, 0);
}

}  // namespace

void set_enabled(bool on) { g_enabled = on; }

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
    // 5a markers: a palm-sized box at the grip pose and a thin pointer along the hand's forward (-Z).
    const float kColor[2][3] = {{0.25f, 0.55f, 1.6f}, {1.6f, 0.6f, 0.15f}};  // left blue, right orange (HDR)
    for (int h = 0; h < 2; ++h) {
        if (!hands.valid[h]) continue;
        box(ctx, vp, hands.rot[h], hands.pos[h], {0.05f, 0.09f, 0.11f}, kColor[h]);
        box(ctx, vp, hands.rot[h], hands.pos[h] + hands.rot[h].rotate({0, 0, -0.12f}), {0.012f, 0.012f, 0.14f}, kColor[h]);
    }
    s.restore(ctx);
    ++g_frames;
}

}  // namespace mecvr::hand_render
