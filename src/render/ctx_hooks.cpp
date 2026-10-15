// D3D11 device-context hooks shared by:
//  - the frame trace (recon): a TRACE file in the mod folder logs every draw and dispatch of the
//    next two frames with the bound pixel/compute shader, inputs (SRVs) and outputs (RTVs/UAVs) to
//    trace_<n>.txt, per context;
//  - per-eye TAA history (taa_eyes, D-023): told about pixel shaders, render targets and draws.
// Installed at the first Present when per-eye TAA is on, or at the first TRACE request.
#include "render/ctx_hooks.h"

#include <d3d11.h>
#include <intrin.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include <MinHook.h>

#include "diagnostics/log.h"
#include "render/taa_eyes.h"

namespace mecvr::ctx_hooks {
namespace {

constexpr int kSlots = 16;

struct Shadow {  // last state set on one context
    void* ps = nullptr;
    void* cs = nullptr;
    ID3D11Resource* ps_srv[kSlots] = {};
    ID3D11Resource* cs_srv[kSlots] = {};
    ID3D11Resource* rtv[8] = {};
    ID3D11Resource* dsv = nullptr;
    ID3D11Resource* uav[8] = {};
};

std::mutex g_mutex;
std::unordered_map<ID3D11DeviceContext*, Shadow> g_shadow;
std::atomic<bool> g_tracing{false};
std::atomic<bool> g_trace_request{false};  // request_trace()
FILE* g_out = nullptr;
int g_frames_left = 0, g_index = 0;
uint64_t g_seq = 0;
bool g_installed = false;
std::filesystem::path g_dir;

ID3D11Resource* resource_of(ID3D11View* v) {
    if (!v) return nullptr;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    if (r) r->Release();  // the view keeps it alive; we only need the identity
    return r;
}

std::string describe(ID3D11Resource* r) {
    if (!r) return "-";
    char buf[96];
    D3D11_RESOURCE_DIMENSION dim;
    r->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC d;
        static_cast<ID3D11Texture2D*>(r)->GetDesc(&d);
        snprintf(buf, sizeof(buf), "%p(%ux%u f%d%s)", static_cast<void*>(r), d.Width, d.Height, d.Format,
                 d.ArraySize > 1 ? " arr" : "");
    } else if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
        snprintf(buf, sizeof(buf), "%p(buf)", static_cast<void*>(r));
    } else {
        snprintf(buf, sizeof(buf), "%p(dim%d)", static_cast<void*>(r), dim);
    }
    return buf;
}

void emit(ID3D11DeviceContext* ctx, const char* what, bool compute) {
    std::lock_guard lock(g_mutex);
    if (!g_out) return;
    Shadow& s = g_shadow[ctx];
    std::string line;
    char head[96];
    snprintf(head, sizeof(head), "%6llu ctx %p %-8s sh %p in:", (unsigned long long)++g_seq, static_cast<void*>(ctx),
             what, compute ? s.cs : s.ps);
    line = head;
    ID3D11Resource* const* srv = compute ? s.cs_srv : s.ps_srv;
    for (int i = 0; i < kSlots; ++i)
        if (srv[i]) line += " " + std::to_string(i) + "=" + describe(srv[i]);
    line += " | out:";
    if (compute) {
        for (int i = 0; i < 8; ++i)
            if (s.uav[i]) line += " u" + std::to_string(i) + "=" + describe(s.uav[i]);
    } else {
        for (int i = 0; i < 8; ++i)
            if (s.rtv[i]) line += " r" + std::to_string(i) + "=" + describe(s.rtv[i]);
        if (s.dsv) line += " d=" + describe(s.dsv);
    }
    fprintf(g_out, "%s\n", line.c_str());
}

// --- hooks ---------------------------------------------------------------------------------------
using SetSrvFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*);
using SetPsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
using SetCsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11ComputeShader*, ID3D11ClassInstance* const*, UINT);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using DrawIdxFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawIdxInstFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using DrawInstFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
using DispatchFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
using OmSetFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
using OmSetUavFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
                                            ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
using CsSetUavFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
using CopyFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);

SetSrvFn o_ps_srv, o_cs_srv;
SetPsFn o_ps;
SetCsFn o_cs;
DrawFn o_draw;
DrawIdxFn o_draw_idx;
DrawIdxInstFn o_draw_idx_inst;
DrawInstFn o_draw_inst;
DispatchFn o_dispatch;
OmSetFn o_om;
OmSetUavFn o_om_uav;
CsSetUavFn o_cs_uav;
CopyFn o_copy;
using DrawAutoFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using DrawIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
using ClearRtvFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, const FLOAT[4]);
using CopyRegionFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*,
                                              UINT, const D3D11_BOX*);
using PredicationFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Predicate*, BOOL);
DrawAutoFn o_draw_auto;
DrawIndirectFn o_draw_idx_indirect, o_draw_indirect;
ClearRtvFn o_clear_rtv;
CopyRegionFn o_copy_region;
PredicationFn o_predication;

// Trace lines for calls that aren't draws (BUG-011: what happens between the TAA targets and its draw).
void note(ID3D11DeviceContext* c, const char* text) {
    std::lock_guard lock(g_mutex);
    if (g_out) fprintf(g_out, "%6llu ctx %p %s\n", (unsigned long long)++g_seq, static_cast<void*>(c), text);
}
std::string caller_of(const void* addr) {
    HMODULE m = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<const char*>(addr), &m) && m)
        GetModuleFileNameA(m, path, sizeof(path));
    const char* base = strrchr(path, '\\');
    char buf[160];
    snprintf(buf, sizeof(buf), "%s+%llx", base ? base + 1 : path,
             (unsigned long long)(static_cast<const char*>(addr) - reinterpret_cast<const char*>(m)));
    return buf;
}

bool on() { return g_tracing.load(std::memory_order_relaxed); }

void STDMETHODCALLTYPE d_ps_srv(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11ShaderResourceView* const* v) {
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        for (UINT i = 0; i < n && start + i < kSlots; ++i) s.ps_srv[start + i] = v ? resource_of(v[i]) : nullptr;
    }
    o_ps_srv(c, start, n, v);
}
void STDMETHODCALLTYPE d_cs_srv(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11ShaderResourceView* const* v) {
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        for (UINT i = 0; i < n && start + i < kSlots; ++i) s.cs_srv[start + i] = v ? resource_of(v[i]) : nullptr;
    }
    o_cs_srv(c, start, n, v);
}
void STDMETHODCALLTYPE d_ps(ID3D11DeviceContext* c, ID3D11PixelShader* sh, ID3D11ClassInstance* const* ci, UINT n) {
    taa_eyes::on_set_ps(c, sh);
    if (on()) { std::lock_guard lock(g_mutex); g_shadow[c].ps = sh; }
    o_ps(c, sh, ci, n);
}
void STDMETHODCALLTYPE d_cs(ID3D11DeviceContext* c, ID3D11ComputeShader* sh, ID3D11ClassInstance* const* ci, UINT n) {
    if (on()) { std::lock_guard lock(g_mutex); g_shadow[c].cs = sh; }
    o_cs(c, sh, ci, n);
}
void STDMETHODCALLTYPE d_om(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rt, ID3D11DepthStencilView* ds) {
    taa_eyes::on_set_targets(c, n, rt, _ReturnAddress());
    if (on()) note(c, ("om       n=" + std::to_string(n) + " by " + caller_of(_ReturnAddress())).c_str());
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        for (UINT i = 0; i < 8; ++i) s.rtv[i] = (rt && i < n) ? resource_of(rt[i]) : nullptr;
        s.dsv = resource_of(ds);
    }
    o_om(c, n, rt, ds);
}
void STDMETHODCALLTYPE d_om_uav(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rt, ID3D11DepthStencilView* ds,
                                UINT us, UINT un, ID3D11UnorderedAccessView* const* u, const UINT* counts) {
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) taa_eyes::on_set_targets(c, n, rt, _ReturnAddress());
    if (on()) note(c, ("omuav    n=" + std::to_string(n) + " by " + caller_of(_ReturnAddress())).c_str());
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) {
            for (UINT i = 0; i < 8; ++i) s.rtv[i] = (rt && i < n) ? resource_of(rt[i]) : nullptr;
            s.dsv = resource_of(ds);
        }
    }
    o_om_uav(c, n, rt, ds, us, un, u, counts);
}
void STDMETHODCALLTYPE d_cs_uav(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11UnorderedAccessView* const* u, const UINT* counts) {
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        for (UINT i = 0; i < n && start + i < 8; ++i) s.uav[start + i] = u ? resource_of(u[i]) : nullptr;
    }
    o_cs_uav(c, start, n, u, counts);
}
void STDMETHODCALLTYPE d_draw(ID3D11DeviceContext* c, UINT a, UINT b) {
    if (on()) emit(c, "draw", false);
    const bool taa = taa_eyes::before_draw(c);
    o_draw(c, a, b);
    if (taa) taa_eyes::after_draw(c);
}
void STDMETHODCALLTYPE d_draw_idx(ID3D11DeviceContext* c, UINT a, UINT b, INT d) {
    taa_eyes::on_other_draw(c, "indexed");
    if (on()) emit(c, "drawidx", false);
    o_draw_idx(c, a, b, d);
}
void STDMETHODCALLTYPE d_draw_idx_inst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, INT e, UINT f) {
    taa_eyes::on_other_draw(c, "indexed inst");
    if (on()) emit(c, "drawidxi", false);
    o_draw_idx_inst(c, a, b, d, e, f);
}
void STDMETHODCALLTYPE d_draw_inst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, UINT e) {
    taa_eyes::on_other_draw(c, "instanced");
    if (on()) emit(c, "drawi", false);
    o_draw_inst(c, a, b, d, e);
}
void STDMETHODCALLTYPE d_dispatch(ID3D11DeviceContext* c, UINT x, UINT y, UINT z) {
    if (on()) {
        char what[32];
        snprintf(what, sizeof(what), "cs%ux%u", x, y);
        emit(c, what, true);
    }
    o_dispatch(c, x, y, z);
}
void STDMETHODCALLTYPE d_copy(ID3D11DeviceContext* c, ID3D11Resource* dst, ID3D11Resource* src) {
    if (on()) {
        std::lock_guard lock(g_mutex);
        if (g_out)
            fprintf(g_out, "%6llu ctx %p copy     %s -> %s\n", (unsigned long long)++g_seq, static_cast<void*>(c),
                    describe(src).c_str(), describe(dst).c_str());
    }
    o_copy(c, dst, src);
}

void STDMETHODCALLTYPE d_draw_auto(ID3D11DeviceContext* c) {
    taa_eyes::on_other_draw(c, "auto");
    if (on()) emit(c, "drawauto", false);
    o_draw_auto(c);
}
void STDMETHODCALLTYPE d_draw_idx_indirect(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT off) {
    taa_eyes::on_other_draw(c, "indexed indirect");
    if (on()) emit(c, "drawidxind", false);
    o_draw_idx_indirect(c, b, off);
}
void STDMETHODCALLTYPE d_draw_indirect(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT off) {
    taa_eyes::on_other_draw(c, "indirect");
    if (on()) emit(c, "drawind", false);
    o_draw_indirect(c, b, off);
}
void STDMETHODCALLTYPE d_clear_rtv(ID3D11DeviceContext* c, ID3D11RenderTargetView* v, const FLOAT rgba[4]) {
    if (on()) note(c, ("clear    " + describe(resource_of(v))).c_str());
    o_clear_rtv(c, v, rgba);
}
void STDMETHODCALLTYPE d_copy_region(ID3D11DeviceContext* c, ID3D11Resource* dst, UINT ds, UINT x, UINT y, UINT z,
                                     ID3D11Resource* src, UINT ss, const D3D11_BOX* box) {
    if (on()) note(c, ("copyreg  " + describe(src) + " -> " + describe(dst)).c_str());
    o_copy_region(c, dst, ds, x, y, z, src, ss, box);
}
void STDMETHODCALLTYPE d_predication(ID3D11DeviceContext* c, ID3D11Predicate* p, BOOL v) {
    if (on()) {
        char t[64];
        snprintf(t, sizeof(t), "predication %p value %d", static_cast<void*>(p), v);
        note(c, t);
    }
    o_predication(c, p, v);
}

template <class T>
void hook(void** vtbl, int index, void* detour, T* orig) {
    if (MH_CreateHook(vtbl[index], detour, reinterpret_cast<void**>(orig)) == MH_OK) MH_EnableHook(vtbl[index]);
    else LOG_W("trace", "hooking context method %d failed", index);
}

void install(IDXGISwapChain* chain) {
    ID3D11Device* dev = nullptr;
    if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev)))) return;
    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    void** vt = *reinterpret_cast<void***>(ctx);
    // ID3D11DeviceContext vtable slots.
    hook(vt, 8, reinterpret_cast<void*>(&d_ps_srv), &o_ps_srv);
    hook(vt, 9, reinterpret_cast<void*>(&d_ps), &o_ps);
    hook(vt, 12, reinterpret_cast<void*>(&d_draw_idx), &o_draw_idx);
    hook(vt, 13, reinterpret_cast<void*>(&d_draw), &o_draw);
    hook(vt, 20, reinterpret_cast<void*>(&d_draw_idx_inst), &o_draw_idx_inst);
    hook(vt, 21, reinterpret_cast<void*>(&d_draw_inst), &o_draw_inst);
    hook(vt, 33, reinterpret_cast<void*>(&d_om), &o_om);
    hook(vt, 34, reinterpret_cast<void*>(&d_om_uav), &o_om_uav);
    hook(vt, 41, reinterpret_cast<void*>(&d_dispatch), &o_dispatch);
    hook(vt, 47, reinterpret_cast<void*>(&d_copy), &o_copy);
    hook(vt, 30, reinterpret_cast<void*>(&d_predication), &o_predication);
    hook(vt, 38, reinterpret_cast<void*>(&d_draw_auto), &o_draw_auto);
    hook(vt, 39, reinterpret_cast<void*>(&d_draw_idx_indirect), &o_draw_idx_indirect);
    hook(vt, 40, reinterpret_cast<void*>(&d_draw_indirect), &o_draw_indirect);
    hook(vt, 46, reinterpret_cast<void*>(&d_copy_region), &o_copy_region);
    hook(vt, 50, reinterpret_cast<void*>(&d_clear_rtv), &o_clear_rtv);
    hook(vt, 67, reinterpret_cast<void*>(&d_cs_srv), &o_cs_srv);
    hook(vt, 68, reinterpret_cast<void*>(&d_cs_uav), &o_cs_uav);
    hook(vt, 69, reinterpret_cast<void*>(&d_cs), &o_cs);
    ctx->Release();
    dev->Release();
    g_installed = true;
    LOG_I("d3d", "device-context hooks installed");
}

}  // namespace

void on_present(IDXGISwapChain* chain, const std::filesystem::path& dir) {
    static int poll = 0;
    if (!g_installed && taa_eyes::enabled()) install(chain);
    taa_eyes::on_present(chain);
    if (g_tracing.load()) {
        std::lock_guard lock(g_mutex);
        if (g_out) fprintf(g_out, "------ present (frame boundary) ------\n");
        if (--g_frames_left <= 0) {
            g_tracing = false;
            if (g_out) { fclose(g_out); g_out = nullptr; }
            g_shadow.clear();
            LOG_I("trace", "frame trace %d written (%llu calls)", g_index, (unsigned long long)g_seq);
        }
        return;
    }
    if (++poll % 30) return;
    std::error_code ec;
    const bool asked = g_trace_request.exchange(false);
    if (!std::filesystem::remove(dir / L"TRACE", ec) && !asked) return;
    if (!g_installed) install(chain);
    if (!g_installed) return;
    wchar_t name[32];
    swprintf_s(name, L"trace_%d.txt", ++g_index);
    std::lock_guard lock(g_mutex);
    if (_wfopen_s(&g_out, (dir / name).c_str(), L"w") != 0) g_out = nullptr;
    g_seq = 0;
    g_frames_left = 3;  // the rest of this frame + two full frames
    g_tracing = g_out != nullptr;
}

void request_trace() { g_trace_request = true; }

}  // namespace mecvr::ctx_hooks
