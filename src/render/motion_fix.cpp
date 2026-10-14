// Motion-vector correction for per-eye TAA (D-023). See motion_fix.h.
#include "render/motion_fix.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <cstring>

#include "core/vmath.h"
#include "diagnostics/log.h"

namespace mecvr::motion_fix {
namespace {

// Full-screen triangle. For each pixel: world position from depth (inverse of the frame's own
// jittered view-projection), then (clip-space xy under this eye's previous camera) - (under the
// engine's previous camera). The engine stores motion as previous - current in clip space (NDC): its
// still-scene vectors were +-24 "px" when read as UV with an eye separation of ~12 px, and a UV-space
// current - previous correction moved them the wrong way by half (24 -> 36). Adding this moves
// "previous" from the other eye's camera to this eye's. Sky (depth 0, reverse-Z) has no parallax: 0.
constexpr char kHlsl[] = R"(
cbuffer C : register(b13) {  // a high slot; the game's are saved/restored with their ring offsets anyway
    row_major float4x4 inv_cur;
    row_major float4x4 prev_engine;
    row_major float4x4 prev_own;
    row_major float4x4 prev_engine_j;  // the same two cameras with their TAA jitter
    row_major float4x4 prev_own_j;
    float4 size;  // w, h, 1/w, 1/h
    float4 mode;  // x: 1 = write zeros (diagnostics.motion_fix_debug); y: jitter blend (diagnostics.motion_fix_jitter)
};
Texture2D<float> depth : register(t0);
struct V { float4 pos : SV_Position; };
V vs(uint id : SV_VertexID) {
    V o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float2 to_ndc(float4x4 m, float4 p) {
    float4 c = mul(m, p);
    return c.xy / c.w;
}
float2 ps(V i) : SV_Target {
    float d = depth.Load(int3(i.pos.xy, 0));
    if (d <= 0) return 0;
    float2 uv = i.pos.xy * size.zw;
    float4 w = mul(inv_cur, float4(uv.x * 2 - 1, 1 - uv.y * 2, d, 1));
    w /= w.w;
    float2 a = to_ndc(prev_own, w) - to_ndc(prev_engine, w);         // camera only
    float2 b = to_ndc(prev_own_j, w) - to_ndc(prev_engine_j, w);     // camera + jitter difference
    float2 r = a + mode.y * (b - a);
    // A NaN here would spread through TAA history and eye adaptation for good (whole screen white).
    if (any(isnan(r)) || any(isinf(r)) || mode.x > 0.5) r = 0;
    return r;
}
)";

struct Constants {
    float inv_cur[16], prev_engine[16], prev_own[16], prev_engine_j[16], prev_own_j[16], size[4], mode[4];
};

bool g_ready = false, g_failed = false;
bool g_zero_mode = false;  // diagnostics.motion_fix_debug
float g_jitter_mode = 0;   // diagnostics.motion_fix_jitter: 0 camera only, 1 with jitter, -1 opposite
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11Buffer* g_cb = nullptr;
ID3D11BlendState* g_blend = nullptr;
ID3D11DepthStencilState* g_depth = nullptr;
ID3D11RasterizerState* g_raster = nullptr;
ID3D11Texture2D* g_rtv_tex = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

bool compile(ID3D11Device* dev) {
    HMODULE dll = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile_fn = dll ? reinterpret_cast<pD3DCompile>(GetProcAddress(dll, "D3DCompile")) : nullptr;
    if (!compile_fn) { LOG_E("taa", "d3dcompiler_47.dll not available - motion fix off"); return false; }
    ID3DBlob *vs = nullptr, *ps = nullptr, *err = nullptr;
    if (FAILED(compile_fn(kHlsl, sizeof(kHlsl) - 1, "motion_fix", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs, &err)) ||
        FAILED(compile_fn(kHlsl, sizeof(kHlsl) - 1, "motion_fix", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps, &err))) {
        LOG_E("taa", "motion fix shader: %s", err ? static_cast<const char*>(err->GetBufferPointer()) : "compile failed");
        if (err) err->Release();
        if (vs) vs->Release();
        return false;
    }
    bool ok = SUCCEEDED(dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs)) &&
              SUCCEEDED(dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_ps));
    vs->Release();
    ps->Release();
    if (!ok) return false;

    D3D11_BUFFER_DESC cb{sizeof(Constants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;  // add to the engine's motion vectors
    bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN;
    D3D11_DEPTH_STENCIL_DESC dd{};  // depth and stencil off
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    return SUCCEEDED(dev->CreateBuffer(&cb, nullptr, &g_cb)) && SUCCEEDED(dev->CreateBlendState(&bd, &g_blend)) &&
           SUCCEEDED(dev->CreateDepthStencilState(&dd, &g_depth)) && SUCCEEDED(dev->CreateRasterizerState(&rd, &g_raster));
}

// Everything the correction draw changes, so the TAA draw runs exactly as the game set it up.
constexpr UINT kCbSlot = 13;

struct State {
    ID3D11DeviceContext1* ctx1 = nullptr;  // for the 11.1 constant-buffer binding (offsets)
    ID3D11InputLayout* layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topo{};
    ID3D11VertexShader* vs = nullptr;
    ID3D11GeometryShader* gs = nullptr;
    ID3D11HullShader* hs = nullptr;
    ID3D11DomainShader* ds = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11ShaderResourceView* srv[8] = {};
    // Catalyst binds constants as windows into a 1 MB ring (11.1 binding with offsets). Restoring with
    // the 11.0 call dropped the offset and the TAA pass read garbage (white screen).
    ID3D11Buffer* cb = nullptr;
    UINT cb_first = 0, cb_num = 0;
    ID3D11RenderTargetView* rtv[8] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    // UAVs bound next to the render targets (pixel-shader outputs). Setting our render target unbinds
    // them; leaving them off broke the TAA pass (screen went white even with the fix writing zeros).
    ID3D11UnorderedAccessView* uav[8] = {};
    UINT rtv_count = 0;
    ID3D11BlendState* blend = nullptr;
    float factor[4] = {};
    UINT mask = 0;
    ID3D11DepthStencilState* depth = nullptr;
    UINT stencil_ref = 0;
    ID3D11RasterizerState* raster = nullptr;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT vp_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

    void save(ID3D11DeviceContext* c) {
        c->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1));
        c->IAGetInputLayout(&layout);
        c->IAGetPrimitiveTopology(&topo);
        c->VSGetShader(&vs, nullptr, nullptr);
        c->GSGetShader(&gs, nullptr, nullptr);
        c->HSGetShader(&hs, nullptr, nullptr);
        c->DSGetShader(&ds, nullptr, nullptr);
        c->PSGetShader(&ps, nullptr, nullptr);
        c->PSGetShaderResources(0, 8, srv);
        if (ctx1) ctx1->PSGetConstantBuffers1(kCbSlot, 1, &cb, &cb_first, &cb_num);
        else c->PSGetConstantBuffers(kCbSlot, 1, &cb);
        c->OMGetRenderTargets(8, rtv, &dsv);
        for (UINT i = 0; i < 8; ++i)
            if (rtv[i]) rtv_count = i + 1;
        if (rtv_count < 8) c->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, rtv_count, 8 - rtv_count, uav);
        c->OMGetBlendState(&blend, factor, &mask);
        c->OMGetDepthStencilState(&depth, &stencil_ref);
        c->RSGetState(&raster);
        c->RSGetViewports(&vp_count, vp);
    }
    void restore(ID3D11DeviceContext* c) {
        c->IASetInputLayout(layout);
        c->IASetPrimitiveTopology(topo);
        c->VSSetShader(vs, nullptr, 0);
        c->GSSetShader(gs, nullptr, 0);
        c->HSSetShader(hs, nullptr, 0);
        c->DSSetShader(ds, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        c->PSSetShaderResources(0, 8, srv);
        if (ctx1 && cb) ctx1->PSSetConstantBuffers1(kCbSlot, 1, &cb, &cb_first, &cb_num);
        else c->PSSetConstantBuffers(kCbSlot, 1, &cb);
        const UINT keep_counts[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};  // keep append/consume counters
        c->OMSetRenderTargetsAndUnorderedAccessViews(rtv_count, rtv, dsv, rtv_count, 8 - rtv_count, uav, keep_counts);
        c->OMSetBlendState(blend, factor, mask);
        c->OMSetDepthStencilState(depth, stencil_ref);
        c->RSSetState(raster);
        c->RSSetViewports(vp_count, vp);
        auto rel = [](IUnknown* p) { if (p) p->Release(); };
        rel(ctx1);
        rel(layout); rel(vs); rel(gs); rel(hs); rel(ds); rel(ps); rel(cb); rel(dsv); rel(blend); rel(depth); rel(raster);
        for (auto* v : srv) rel(v);
        for (auto* v : rtv) rel(v);
        for (auto* v : uav) rel(v);
    }
};

}  // namespace

bool apply(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* depth_srv, ID3D11Texture2D* motion,
           const float* cur_vp_jittered, const float* prev_engine_vp, const float* prev_own_vp,
           const float* prev_engine_vp_j, const float* prev_own_vp_j) {
    if (g_failed || !depth_srv || !motion) return false;
    if (!g_ready) {
        if (!compile(dev)) { g_failed = true; return false; }
        g_ready = true;
        LOG_I("taa", "motion fix shader ready");
    }
    if (motion != g_rtv_tex) {
        if (g_rtv) g_rtv->Release();
        g_rtv = nullptr;
        g_rtv_tex = nullptr;
        if (FAILED(dev->CreateRenderTargetView(motion, nullptr, &g_rtv))) {
            LOG_E("taa", "motion vectors can't be a render target - motion fix off");
            g_failed = true;
            return false;
        }
        g_rtv_tex = motion;
    }
    D3D11_TEXTURE2D_DESC d;
    motion->GetDesc(&d);
    Constants k{};
    if (!vm::invert4(cur_vp_jittered, k.inv_cur)) return false;
    memcpy(k.prev_engine, prev_engine_vp, sizeof(k.prev_engine));
    memcpy(k.prev_own, prev_own_vp, sizeof(k.prev_own));
    k.size[0] = float(d.Width);
    k.size[1] = float(d.Height);
    k.size[2] = 1.f / d.Width;
    k.size[3] = 1.f / d.Height;
    k.mode[0] = g_zero_mode ? 1.f : 0.f;
    k.mode[1] = g_jitter_mode;
    memcpy(k.prev_engine_j, prev_engine_vp_j, sizeof(k.prev_engine_j));
    memcpy(k.prev_own_j, prev_own_vp_j, sizeof(k.prev_own_j));
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    memcpy(m.pData, &k, sizeof(k));
    ctx->Unmap(g_cb, 0);

    State s;
    s.save(ctx);
    D3D11_VIEWPORT vp{0, 0, float(d.Width), float(d.Height), 0, 1};
    const float zero[4] = {};
    ctx->OMSetRenderTargets(1, &g_rtv, nullptr);  // unbinds the motion SRV (slot 1); restored below
    ctx->OMSetBlendState(g_blend, zero, 0xffffffff);
    ctx->OMSetDepthStencilState(g_depth, 0);
    ctx->RSSetState(g_raster);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_vs, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(g_ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &depth_srv);
    ctx->PSSetConstantBuffers(kCbSlot, 1, &g_cb);
    ctx->Draw(3, 0);
    s.restore(ctx);
    return true;
}

}  // namespace mecvr::motion_fix

namespace mecvr::motion_fix {
void set_debug_zero(bool on) { g_zero_mode = on; }
void set_jitter_mode(float m) { g_jitter_mode = m; LOG_I("taa", "motion fix jitter mode %.0f", m); }
}  // namespace mecvr::motion_fix
