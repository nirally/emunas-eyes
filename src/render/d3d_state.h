#pragma once
// Saves and restores the D3D11 pipeline state our own draws change (motion_fix, hand_render), so the
// game's next draw runs exactly as the game set it up. Constants go on slot kCbSlot (VS and PS) with
// their 11.1 ring offsets kept (D-023: dropping the offset broke the TAA pass); OM UAVs are kept too.
#include <d3d11_1.h>

namespace mecvr::d3d_state {

constexpr UINT kCbSlot = 13;  // a high slot; the game's binding there is saved/restored anyway


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
    ID3D11Buffer* cb = nullptr;      // PS slot kCbSlot
    UINT cb_first = 0, cb_num = 0;
    ID3D11Buffer* vcb = nullptr;     // VS slot kCbSlot
    UINT vcb_first = 0, vcb_num = 0;
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
        if (ctx1) ctx1->VSGetConstantBuffers1(kCbSlot, 1, &vcb, &vcb_first, &vcb_num);
        else c->VSGetConstantBuffers(kCbSlot, 1, &vcb);
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
        if (ctx1 && vcb) ctx1->VSSetConstantBuffers1(kCbSlot, 1, &vcb, &vcb_first, &vcb_num);
        else c->VSSetConstantBuffers(kCbSlot, 1, &vcb);
        const UINT keep_counts[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u};  // keep append/consume counters
        c->OMSetRenderTargetsAndUnorderedAccessViews(rtv_count, rtv, dsv, rtv_count, 8 - rtv_count, uav, keep_counts);
        c->OMSetBlendState(blend, factor, mask);
        c->OMSetDepthStencilState(depth, stencil_ref);
        c->RSSetState(raster);
        c->RSSetViewports(vp_count, vp);
        auto rel = [](IUnknown* p) { if (p) p->Release(); };
        rel(ctx1);
        rel(layout); rel(vs); rel(gs); rel(hs); rel(ds); rel(ps); rel(cb); rel(vcb); rel(dsv); rel(blend); rel(depth); rel(raster);
        for (auto* v : srv) rel(v);
        for (auto* v : rtv) rel(v);
        for (auto* v : uav) rel(v);
    }
};

}  // namespace mecvr::d3d_state
