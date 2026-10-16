// D3D11 device-context hooks shared by:
//  - the frame trace (recon): a TRACE file in the mod folder logs every draw and dispatch of the
//    next two frames with the bound pixel/compute shader, inputs (SRVs) and outputs (RTVs/UAVs) to
//    trace_<n>.txt, per context;
//  - per-eye TAA history (taa_eyes, D-023): told about pixel shaders, render targets and draws.
// Installed at the first Present when per-eye TAA is on, or at the first TRACE request.
#include "render/ctx_hooks.h"

#include <d3d11_4.h>
#include <intrin.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <vector>

#include <MinHook.h>

#include "diagnostics/log.h"
#include "render/cb_capture.h"
#include "render/hand_render.h"
#include "render/hook_guard.h"
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

constexpr int kVersions = 16;  // distinct implementations hooked per method (BUG-011)
SetSrvFn o_ps_srv[kVersions], o_cs_srv[kVersions];
SetPsFn o_ps[kVersions];
SetCsFn o_cs[kVersions];
DrawFn o_draw[kVersions];
DrawIdxFn o_draw_idx[kVersions];
DrawIdxInstFn o_draw_idx_inst[kVersions];
DrawInstFn o_draw_inst[kVersions];
DispatchFn o_dispatch[kVersions];
OmSetFn o_om[kVersions];
OmSetUavFn o_om_uav[kVersions];
CsSetUavFn o_cs_uav[kVersions];
CopyFn o_copy[kVersions];
using DrawAutoFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using DrawIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
using ClearRtvFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, const FLOAT[4]);
using CopyRegionFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*,
                                              UINT, const D3D11_BOX*);
using PredicationFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Predicate*, BOOL);
DrawAutoFn o_draw_auto[kVersions];
DrawIndirectFn o_draw_idx_indirect[kVersions], o_draw_indirect[kVersions];
ClearRtvFn o_clear_rtv[kVersions];
CopyRegionFn o_copy_region[kVersions];
PredicationFn o_predication[kVersions];
using ExecListFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);
ExecListFn o_exec_list[kVersions];
using MapFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using UnmapFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using UpdateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
MapFn o_map[kVersions];
UnmapFn o_unmap[kVersions];
UpdateFn o_update[kVersions];
std::atomic<uint32_t> g_exec_lists{0};

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
using hook_guard::Inside;
std::atomic<uint32_t> g_hits[kVersions][2];  // Draw, OMSetRenderTargets calls per hooked version (BUG-011)

template <int V>
void STDMETHODCALLTYPE d_ps_srv(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11ShaderResourceView* const* v) {
    const Inside in;
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        for (UINT i = 0; i < n && start + i < kSlots; ++i) s.ps_srv[start + i] = v ? resource_of(v[i]) : nullptr;
    }
    o_ps_srv[V](c, start, n, v);
}
template <int V>
void STDMETHODCALLTYPE d_cs_srv(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11ShaderResourceView* const* v) {
    const Inside in;
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        for (UINT i = 0; i < n && start + i < kSlots; ++i) s.cs_srv[start + i] = v ? resource_of(v[i]) : nullptr;
    }
    o_cs_srv[V](c, start, n, v);
}
template <int V>
void STDMETHODCALLTYPE d_ps(ID3D11DeviceContext* c, ID3D11PixelShader* sh, ID3D11ClassInstance* const* ci, UINT n) {
    const Inside in;
    if (!in.nested) taa_eyes::on_set_ps(c, sh);
    if (on()) { std::lock_guard lock(g_mutex); g_shadow[c].ps = sh; }
    o_ps[V](c, sh, ci, n);
}
template <int V>
void STDMETHODCALLTYPE d_cs(ID3D11DeviceContext* c, ID3D11ComputeShader* sh, ID3D11ClassInstance* const* ci, UINT n) {
    const Inside in;
    if (on()) { std::lock_guard lock(g_mutex); g_shadow[c].cs = sh; }
    o_cs[V](c, sh, ci, n);
}
template <int V>
void STDMETHODCALLTYPE d_om(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rt, ID3D11DepthStencilView* ds) {
    const Inside in;
    if (!in.nested) g_hits[V][1].fetch_add(1, std::memory_order_relaxed);
    if (!in.nested) taa_eyes::on_set_targets(c, n, rt, _ReturnAddress());
    if (on()) note(c, ("om       n=" + std::to_string(n) + " by " + caller_of(_ReturnAddress())).c_str());
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        for (UINT i = 0; i < 8; ++i) s.rtv[i] = (rt && i < n) ? resource_of(rt[i]) : nullptr;
        s.dsv = resource_of(ds);
    }
    o_om[V](c, n, rt, ds);
}
template <int V>
void STDMETHODCALLTYPE d_om_uav(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rt, ID3D11DepthStencilView* ds,
                                UINT us, UINT un, ID3D11UnorderedAccessView* const* u, const UINT* counts) {
    const Inside in;
    if (!in.nested && n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) taa_eyes::on_set_targets(c, n, rt, _ReturnAddress());
    if (on()) note(c, ("omuav    n=" + std::to_string(n) + " by " + caller_of(_ReturnAddress())).c_str());
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) {
            for (UINT i = 0; i < 8; ++i) s.rtv[i] = (rt && i < n) ? resource_of(rt[i]) : nullptr;
            s.dsv = resource_of(ds);
        }
    }
    o_om_uav[V](c, n, rt, ds, us, un, u, counts);
}
template <int V>
void STDMETHODCALLTYPE d_cs_uav(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11UnorderedAccessView* const* u, const UINT* counts) {
    const Inside in;
    if (on()) {
        std::lock_guard lock(g_mutex);
        Shadow& s = g_shadow[c];
        for (UINT i = 0; i < n && start + i < 8; ++i) s.uav[start + i] = u ? resource_of(u[i]) : nullptr;
    }
    o_cs_uav[V](c, start, n, u, counts);
}
template <int V>
void STDMETHODCALLTYPE d_draw(ID3D11DeviceContext* c, UINT a, UINT b) {
    const Inside in;
    if (!in.nested) g_hits[V][0].fetch_add(1, std::memory_order_relaxed);
    if (on()) emit(c, "draw", false);
    const bool taa = !in.nested && taa_eyes::before_draw(c);
    o_draw[V](c, a, b);
    if (taa) taa_eyes::after_draw(c);
}
template <int V>
void STDMETHODCALLTYPE d_draw_idx(ID3D11DeviceContext* c, UINT a, UINT b, INT d) {
    const Inside in;
    if (!in.nested) taa_eyes::on_other_draw(c, "indexed");
    if (on()) emit(c, "drawidx", false);
    o_draw_idx[V](c, a, b, d);
}
template <int V>
void STDMETHODCALLTYPE d_draw_idx_inst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, INT e, UINT f) {
    const Inside in;
    if (!in.nested) taa_eyes::on_other_draw(c, "indexed inst");
    if (on()) emit(c, "drawidxi", false);
    o_draw_idx_inst[V](c, a, b, d, e, f);
}
template <int V>
void STDMETHODCALLTYPE d_draw_inst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, UINT e) {
    const Inside in;
    if (!in.nested) taa_eyes::on_other_draw(c, "instanced");
    if (on()) emit(c, "drawi", false);
    o_draw_inst[V](c, a, b, d, e);
}
template <int V>
void STDMETHODCALLTYPE d_dispatch(ID3D11DeviceContext* c, UINT x, UINT y, UINT z) {
    const Inside in;
    if (on()) {
        char what[32];
        snprintf(what, sizeof(what), "cs%ux%u", x, y);
        emit(c, what, true);
    }
    o_dispatch[V](c, x, y, z);
}
template <int V>
void STDMETHODCALLTYPE d_copy(ID3D11DeviceContext* c, ID3D11Resource* dst, ID3D11Resource* src) {
    const Inside in;
    if (on()) {
        std::lock_guard lock(g_mutex);
        if (g_out)
            fprintf(g_out, "%6llu ctx %p copy     %s -> %s\n", (unsigned long long)++g_seq, static_cast<void*>(c),
                    describe(src).c_str(), describe(dst).c_str());
    }
    o_copy[V](c, dst, src);
}

template <int V>
void STDMETHODCALLTYPE d_draw_auto(ID3D11DeviceContext* c) {
    const Inside in;
    if (!in.nested) taa_eyes::on_other_draw(c, "auto");
    if (on()) emit(c, "drawauto", false);
    o_draw_auto[V](c);
}
template <int V>
void STDMETHODCALLTYPE d_draw_idx_indirect(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT off) {
    const Inside in;
    if (!in.nested) taa_eyes::on_other_draw(c, "indexed indirect");
    if (on()) emit(c, "drawidxind", false);
    o_draw_idx_indirect[V](c, b, off);
}
template <int V>
void STDMETHODCALLTYPE d_draw_indirect(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT off) {
    const Inside in;
    if (!in.nested) taa_eyes::on_other_draw(c, "indirect");
    if (on()) emit(c, "drawind", false);
    o_draw_indirect[V](c, b, off);
}
template <int V>
void STDMETHODCALLTYPE d_clear_rtv(ID3D11DeviceContext* c, ID3D11RenderTargetView* v, const FLOAT rgba[4]) {
    const Inside in;
    if (on()) note(c, ("clear    " + describe(resource_of(v))).c_str());
    o_clear_rtv[V](c, v, rgba);
}
template <int V>
void STDMETHODCALLTYPE d_copy_region(ID3D11DeviceContext* c, ID3D11Resource* dst, UINT ds, UINT x, UINT y, UINT z,
                                     ID3D11Resource* src, UINT ss, const D3D11_BOX* box) {
    const Inside in;
    if (on()) note(c, ("copyreg  " + describe(src) + " -> " + describe(dst)).c_str());
    o_copy_region[V](c, dst, ds, x, y, z, src, ss, box);
}
template <int V>
void STDMETHODCALLTYPE d_predication(ID3D11DeviceContext* c, ID3D11Predicate* p, BOOL v) {
    const Inside in;
    if (on()) {
        char t[64];
        snprintf(t, sizeof(t), "predication %p value %d", static_cast<void*>(p), v);
        note(c, t);
    }
    o_predication[V](c, p, v);
}

template <int V>
void STDMETHODCALLTYPE d_exec_list(ID3D11DeviceContext* c, ID3D11CommandList* l, BOOL restore) {
    const Inside in;
    g_exec_lists.fetch_add(1, std::memory_order_relaxed);
    if (on()) note(c, "execlist");
    o_exec_list[V](c, l, restore);
}
// Constant-buffer uploads (the view constants say which eye a frame is): cb_capture hooks the version
// that exists at startup; these cover the others.
template <int V>
HRESULT STDMETHODCALLTYPE d_map(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, D3D11_MAP type, UINT flags,
                                D3D11_MAPPED_SUBRESOURCE* out) {
    const Inside in;
    const HRESULT hr = o_map[V](c, r, sub, type, flags, out);
    if (!in.nested) cb_capture::after_map(r, hr, out);
    return hr;
}
template <int V>
void STDMETHODCALLTYPE d_unmap(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub) {
    const Inside in;
    if (!in.nested) cb_capture::before_unmap(r);
    o_unmap[V](c, r, sub);
}
template <int V>
void STDMETHODCALLTYPE d_update(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, const D3D11_BOX* box, const void* data,
                                UINT row, UINT depth) {
    const Inside in;
    if (!in.nested) cb_capture::before_update(r, box, data);
    o_update[V](c, r, sub, box, data, row, depth);
}
template <class T>
void hook(void* const* vtbl, int index, void* detour, T* orig) {
    const MH_STATUS c = MH_CreateHook(vtbl[index], detour, reinterpret_cast<void**>(orig));
    const MH_STATUS e = c == MH_OK ? MH_EnableHook(vtbl[index]) : c;
    // Already created: hooked elsewhere (cb_capture hooks Map / Unmap / UpdateSubresource at startup).
    if (e != MH_OK && e != MH_ERROR_ALREADY_CREATED)
        LOG_W("d3d", "hooking context method %d at %p failed (%d)", index, vtbl[index], int(e));
}

// Hooks one version of the context's methods; implementations already hooked are skipped.
std::vector<void*> g_hooked;
template <int V>
void hook_all(void* const* vt) {
    auto h = [&](int i, void* d, auto* o) {
        if (std::find(g_hooked.begin(), g_hooked.end(), vt[i]) != g_hooked.end()) return;
        g_hooked.push_back(vt[i]);
        hook(vt, i, d, o);
    };
    // ID3D11DeviceContext vtable slots.
    h(8, reinterpret_cast<void*>(&d_ps_srv<V>), &o_ps_srv[V]);
    h(9, reinterpret_cast<void*>(&d_ps<V>), &o_ps[V]);
    h(12, reinterpret_cast<void*>(&d_draw_idx<V>), &o_draw_idx[V]);
    h(13, reinterpret_cast<void*>(&d_draw<V>), &o_draw[V]);
    h(20, reinterpret_cast<void*>(&d_draw_idx_inst<V>), &o_draw_idx_inst[V]);
    h(21, reinterpret_cast<void*>(&d_draw_inst<V>), &o_draw_inst[V]);
    h(30, reinterpret_cast<void*>(&d_predication<V>), &o_predication[V]);
    h(33, reinterpret_cast<void*>(&d_om<V>), &o_om[V]);
    h(34, reinterpret_cast<void*>(&d_om_uav<V>), &o_om_uav[V]);
    h(38, reinterpret_cast<void*>(&d_draw_auto<V>), &o_draw_auto[V]);
    h(39, reinterpret_cast<void*>(&d_draw_idx_indirect<V>), &o_draw_idx_indirect[V]);
    h(40, reinterpret_cast<void*>(&d_draw_indirect<V>), &o_draw_indirect[V]);
    h(41, reinterpret_cast<void*>(&d_dispatch<V>), &o_dispatch[V]);
    h(46, reinterpret_cast<void*>(&d_copy_region<V>), &o_copy_region[V]);
    h(47, reinterpret_cast<void*>(&d_copy<V>), &o_copy[V]);
    h(50, reinterpret_cast<void*>(&d_clear_rtv<V>), &o_clear_rtv[V]);
    h(67, reinterpret_cast<void*>(&d_cs_srv<V>), &o_cs_srv[V]);
    h(68, reinterpret_cast<void*>(&d_cs_uav<V>), &o_cs_uav[V]);
    h(69, reinterpret_cast<void*>(&d_cs<V>), &o_cs[V]);
    h(58, reinterpret_cast<void*>(&d_exec_list<V>), &o_exec_list[V]);
    h(14, reinterpret_cast<void*>(&d_map<V>), &o_map[V]);
    h(15, reinterpret_cast<void*>(&d_unmap<V>), &o_unmap[V]);
    h(48, reinterpret_cast<void*>(&d_update<V>), &o_update[V]);
}
using HookAllFn = void (*)(void* const*);
constexpr HookAllFn kHookAll[kVersions] = {hook_all<0>, hook_all<1>, hook_all<2>,  hook_all<3>,  hook_all<4>,  hook_all<5>,
                                           hook_all<6>, hook_all<7>, hook_all<8>,  hook_all<9>,  hook_all<10>, hook_all<11>,
                                           hook_all<12>, hook_all<13>, hook_all<14>, hook_all<15>};

// The context's methods come in versions: each interface the game may hold (ID3D11DeviceContext..4) has
// its own method table, and each table changes in place when the device's thread-safe mode is switched
// on - which an OpenXR runtime (SteamVR) does. The game's draws then bypassed hooks on the plain table
// (BUG-011). Every table is read in both modes (switching the mode briefly) and every distinct
// implementation is hooked.
void install(IDXGISwapChain* chain) {
    ID3D11Device* dev = nullptr;
    if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev)))) return;
    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    constexpr int kMethods = 70;
    struct Table { void* entries[kMethods]; };
    std::vector<Table> tables;
    std::vector<IUnknown*> faces;
    const IID ids[] = {__uuidof(ID3D11DeviceContext), __uuidof(ID3D11DeviceContext1), __uuidof(ID3D11DeviceContext2),
                       __uuidof(ID3D11DeviceContext3), __uuidof(ID3D11DeviceContext4)};
    for (const IID& id : ids) {
        IUnknown* p = nullptr;
        if (SUCCEEDED(ctx->QueryInterface(id, reinterpret_cast<void**>(&p)))) faces.push_back(p);
    }
    auto snapshot = [&] {
        for (IUnknown* p : faces) {
            Table t;
            memcpy(t.entries, *reinterpret_cast<void***>(p), sizeof(t.entries));
            bool seen = false;
            for (const Table& o : tables) seen = seen || memcmp(o.entries, t.entries, sizeof(t.entries)) == 0;
            if (!seen) tables.push_back(t);
        }
    };
    snapshot();
    BOOL was = FALSE;
    ID3D11Multithread* mt = nullptr;
    if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&mt)))) {
        was = mt->SetMultithreadProtected(!mt->GetMultithreadProtected());
        snapshot();
        mt->SetMultithreadProtected(was);
        mt->Release();
    }
    // The runtime also keeps other copies of the method table (in its read-only data) and can switch the
    // context's entries to them while the game draws - in the headset the game's draws went through
    // versions never seen in the table at Present (PLAYTEST-5e). Every such copy is found by its first
    // entries (QueryInterface, AddRef, Release are shared) with all methods inside the runtime's code.
    size_t found = 0;
    HMODULE rt = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<const char*>(tables[0].entries[13]), &rt) && rt) {
        auto* base = reinterpret_cast<const uint8_t*>(rt);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        const auto* sec = IMAGE_FIRST_SECTION(nt);
        const uint8_t* code_lo = nullptr;
        const uint8_t* code_hi = nullptr;
        for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s)
            if (sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                const uint8_t* lo = base + sec[s].VirtualAddress;
                const uint8_t* hi = lo + sec[s].Misc.VirtualSize;
                if (!code_lo || lo < code_lo) code_lo = lo;
                if (!code_hi || hi > code_hi) code_hi = hi;
            }
        for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
            if ((sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE) || !(sec[s].Characteristics & IMAGE_SCN_MEM_READ)) continue;
            auto* p = reinterpret_cast<void* const*>(base + sec[s].VirtualAddress);
            const size_t n = sec[s].Misc.VirtualSize / sizeof(void*);
            for (size_t i = 0; i + kMethods <= n; ++i) {
                if (p[i] != tables[0].entries[0] || p[i + 1] != tables[0].entries[1] || p[i + 2] != tables[0].entries[2]) continue;
                bool code = true;
                for (int k = 0; k < kMethods && code; ++k) {
                    auto* e = static_cast<const uint8_t*>(p[i + k]);
                    code = e >= code_lo && e < code_hi;
                }
                if (!code) continue;
                Table t;
                memcpy(t.entries, p + i, sizeof(t.entries));
                bool seen = false;
                for (const Table& o : tables) seen = seen || memcmp(o.entries, t.entries, sizeof(t.entries)) == 0;
                if (!seen) { tables.push_back(t); ++found; }
            }
        }
    }
    if (tables.size() > kVersions) LOG_W("d3d", "%zu method tables, only %d hooked", tables.size(), kVersions);
    std::string draws;
    for (size_t i = 0; i < tables.size() && i < kVersions; ++i) {
        kHookAll[i](tables[i].entries);
        char b[24];
        snprintf(b, sizeof(b), " %p", tables[i].entries[13]);
        draws += b;
    }
    for (IUnknown* p : faces) p->Release();
    ctx->Release();
    dev->Release();
    g_installed = true;
    LOG_I("d3d", "device-context hooks installed: %zu interfaces, %zu method tables (%zu from the runtime's data; thread-safe mode %s), %zu methods hooked; Draw at%s",
          faces.size(), tables.size(), found, was ? "on" : "off", g_hooked.size(), draws.c_str());
}

}  // namespace

void on_present(IDXGISwapChain* chain, const std::filesystem::path& dir) {
    static int poll = 0;
    static ULONGLONG last_hits = GetTickCount64();
    if (g_installed && GetTickCount64() - last_hits >= 10000) {
        last_hits = GetTickCount64();
        char line[256] = "";
        size_t len = 0;
        for (int v = 0; v < kVersions; ++v) {
            const uint32_t d = g_hits[v][0].exchange(0), o = g_hits[v][1].exchange(0);
            if (d || o) len += snprintf(line + len, sizeof(line) - len, " v%d draw %u om %u;", v, d, o);
        }
        // The context's state now: thread-safe mode, and current methods that aren't hooked (BUG-011).
        BOOL safe = -1;
        char miss[256] = "";
        ID3D11Device* dev = nullptr;
        if (SUCCEEDED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev)))) {
            ID3D11DeviceContext* ctx = nullptr;
            dev->GetImmediateContext(&ctx);
            ID3D11Multithread* mt = nullptr;
            if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&mt)))) {
                safe = mt->GetMultithreadProtected();
                mt->Release();
            }
            void* const* t = *reinterpret_cast<void***>(ctx);
            size_t ml = 0;
            for (int i : {9, 12, 13, 20, 21, 33, 34})
                if (std::find(g_hooked.begin(), g_hooked.end(), t[i]) == g_hooked.end())
                    ml += snprintf(miss + ml, sizeof(miss) - ml, " %d=%p", i, t[i]);
            ctx->Release();
            dev->Release();
        }
        LOG_I("d3d", "context calls seen per method version /10 s:%s command lists run %u; thread-safe %d; not hooked:%s",
              line[0] ? line : " none;", g_exec_lists.exchange(0), safe, miss[0] ? miss : " none");
    }
    if (!g_installed && (taa_eyes::enabled() || hand_render::enabled())) install(chain);
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
    // BUG-011 experiment: MTPROTECT file -> turn on the device's thread-safe mode (as an OpenXR runtime may)
    // and log whether the context's method table moves away from the hooked functions.
    if (std::filesystem::remove(dir / L"MTPROTECT", ec)) {
        ID3D11Device* dev = nullptr;
        if (SUCCEEDED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev)))) {
            ID3D11DeviceContext* ctx = nullptr;
            dev->GetImmediateContext(&ctx);
            ID3D11Multithread* mt = nullptr;
            void* before = *reinterpret_cast<void**>(ctx);
            void* draw_before = (*reinterpret_cast<void***>(ctx))[13];
            BOOL was = FALSE;
            if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&mt)))) {
                was = mt->SetMultithreadProtected(TRUE);
                mt->Release();
            }
            LOG_I("d3d", "thread-safe mode on (was %d): context table %p -> %p, Draw %p -> %p", was, before,
                  *reinterpret_cast<void**>(ctx), draw_before, (*reinterpret_cast<void***>(ctx))[13]);
            void* const* t = *reinterpret_cast<void***>(ctx);
            char miss[512] = "";
            size_t len = 0;
            for (int i : {8, 9, 12, 13, 20, 21, 33, 34, 41, 47})
                if (std::find(g_hooked.begin(), g_hooked.end(), t[i]) == g_hooked.end())
                    len += snprintf(miss + len, sizeof(miss) - len, " %d=%p", i, t[i]);
            LOG_I("d3d", "methods now not hooked:%s", miss[0] ? miss : " none");
            ctx->Release();
            dev->Release();
        }
    }
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
