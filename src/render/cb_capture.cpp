#include "render/cb_capture.h"

#include <d3d11.h>
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <MinHook.h>

#include "diagnostics/log.h"
#include "render/cam_hunt.h"
#include "render/ctx_hooks.h"
#include "render/faith_bones.h"
#include "render/hook_guard.h"
#include "render/camera_override.h"
#include "render/taa_eyes.h"
#include "render/xr_session.h"

namespace mecvr::cb_capture {
namespace {

using MapFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
                                          D3D11_MAPPED_SUBRESOURCE*);
using UnmapFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using UpdateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*,
                                          const void*, UINT, UINT);

MapFn g_map_orig = nullptr;
UnmapFn g_unmap_orig = nullptr;
UpdateFn g_update_orig = nullptr;

std::filesystem::path g_dir;
bool g_recon = false;  // capture + camera hunt tools (developer)
std::atomic<bool> g_armed{false};     // capture during the next full frame
std::atomic<bool> g_capturing{false};
int g_capture_index = 0;
std::mutex g_mutex;

struct Pending { void* data; UINT size; };
std::unordered_map<ID3D11Resource*, Pending> g_pending;  // mapped, not yet unmapped
std::unordered_map<ID3D11Resource*, Pending> g_big_pending;  // big dynamic buffers while tracing (Stage 7 recon)

struct Record { std::string how; ID3D11Resource* res; std::vector<float> floats; };
std::vector<Record> g_records;

// The per-view constant buffer: [0..7] misc + screen size, [8..23] view, [24..39] projection.
constexpr UINT kViewCbBytes = 1136;
bool looks_like_view_cb(const float* f) {
    auto z = [](float v) { return std::fabs(v) < 1e-4f; };
    bool screen = f[4] > 100.f && f[5] > 100.f && std::fabs(f[6] * f[4] - 1.f) < 1e-3f;
    bool view_affine = z(f[20]) && z(f[21]) && z(f[22]) && std::fabs(f[23] - 1.f) < 1e-4f;
    bool proj = z(f[25]) && z(f[28]) && std::fabs(f[38] + 1.f) < 1e-3f;
    return screen && view_affine && proj;
}
std::atomic<int> g_views_this_frame{0};
std::atomic<float> g_proj_sx{0}, g_proj_sy{0};  // last view projection scale (for the FOV log)
uint64_t g_views_hist[5] = {};  // frames with 0,1,2,3,4+ view-constant uploads

// Latest per-view constants as the game wrote them (before the per-eye TAA patch), for MEMFIND.
float g_last_view[kViewCbBytes / 4];
bool g_have_last_view = false;

// VIEWCB trigger: the next kViewDumps view constant buffers, all floats (finding the previous-frame
// matrices TAA reprojects with, D-023).
constexpr int kViewDumps = 4;
std::atomic<int> g_view_dumps_left{0};
FILE* g_view_dump = nullptr;
int g_view_dump_index = 0;
std::atomic<uint64_t> g_present_count{0};

void watch_view(const void* data, UINT size) {
    if (size != kViewCbBytes) return;
    float f[40];
    memcpy(f, data, sizeof(f));
    if (!looks_like_view_cb(f)) return;
    {
        std::lock_guard lock(g_mutex);
        memcpy(g_last_view, data, sizeof(g_last_view));
        g_have_last_view = true;
    }
    ++g_views_this_frame;
    camera_override::check_view(f + 8);
    if (g_view_dumps_left.load() > 0) {  // after check_view, so the eye printed is this frame's
        std::lock_guard lock(g_mutex);
        if (g_view_dump) {
            const float* all = static_cast<const float*>(data);
            fprintf(g_view_dump, "\n# view CB at present %llu, eye %d\n", (unsigned long long)g_present_count.load(),
                    camera_override::rendering_eye());
            for (UINT i = 0; i < size / 4; ++i) fprintf(g_view_dump, "%s%4u:%12.6f", i % 4 ? " " : "\n", i, all[i]);
            fprintf(g_view_dump, "\n");
            if (--g_view_dumps_left == 0) {
                fclose(g_view_dump);
                g_view_dump = nullptr;
                LOG_I("capture", "view CB dump %d written", g_view_dump_index);
            }
        }
    }
    xr_session::set_game_projection(f[24], f[29]);
    camera_override::note_projection(f[29]);
    g_proj_sx = f[24];
    g_proj_sy = f[29];
    if (g_recon) cam_hunt::on_view_matrix(f + 8, g_dir);
}

bool near0(float v) { return std::fabs(v) < 1e-4f; }

// m is 16 floats as laid out in the buffer.
std::string classify(const float* m) {
    std::string tags;
    // Projection (either major order): off-diagonals of the 2x2 top-left are zero,
    // scale terms plausible, and a +-1 in the w-row/column.
    bool zeros = near0(m[1]) && near0(m[4]) && near0(m[3]) && near0(m[7]);
    bool scales = std::fabs(m[0]) > 0.2f && std::fabs(m[0]) < 10.f && std::fabs(m[5]) > 0.2f && std::fabs(m[5]) < 10.f;
    bool w_col = std::fabs(std::fabs(m[11]) - 1.f) < 1e-3f && near0(m[15]);
    bool w_row = std::fabs(std::fabs(m[14]) - 1.f) < 1e-3f && near0(m[15]);
    if (zeros && scales && (w_col || w_row)) {
        char b[96];
        snprintf(b, sizeof(b), " PROJ?(sx=%.4f sy=%.4f %s)", m[0], m[5], w_col ? "w-col" : "w-row");
        tags += b;
    }
    // Rigid transform: 3x3 rotation with unit rows, plus 0001 row or column.
    auto len3 = [](const float* r, int stride) {
        return std::sqrt(r[0] * r[0] + r[stride] * r[stride] + r[2 * stride] * r[2 * stride]);
    };
    bool rows_unit = std::fabs(len3(m, 1) - 1) < 1e-3f && std::fabs(len3(m + 4, 1) - 1) < 1e-3f &&
                     std::fabs(len3(m + 8, 1) - 1) < 1e-3f;
    bool aff_col = near0(m[3]) && near0(m[7]) && near0(m[11]) && std::fabs(m[15] - 1) < 1e-4f;
    bool aff_row = near0(m[12]) && near0(m[13]) && near0(m[14]) && std::fabs(m[15] - 1) < 1e-4f;
    if (rows_unit && (aff_col || aff_row)) {
        char b[128];
        if (aff_col) snprintf(b, sizeof(b), " RIGID(t=%.2f,%.2f,%.2f row-major-translation)", m[12], m[13], m[14]);
        else snprintf(b, sizeof(b), " RIGID(t=%.2f,%.2f,%.2f col-major-translation)", m[3], m[7], m[11]);
        tags += b;
    }
    return tags;
}

void record(const char* how, ID3D11Resource* res, const void* data, UINT size) {
    Record r{how, res, {}};
    r.floats.resize(size / 4);
    memcpy(r.floats.data(), data, (size / 4) * 4);
    std::lock_guard lock(g_mutex);
    g_records.push_back(std::move(r));
}

bool is_constant_buffer(ID3D11Resource* res, UINT* size) {
    D3D11_RESOURCE_DIMENSION dim;
    res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return false;
    D3D11_BUFFER_DESC d;
    static_cast<ID3D11Buffer*>(res)->GetDesc(&d);
    *size = d.ByteWidth;
    return (d.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
}

}  // namespace

void after_map(ID3D11Resource* res, HRESULT hr, const D3D11_MAPPED_SUBRESOURCE* out) {
    UINT size = 0;
    if (SUCCEEDED(hr) && out && is_constant_buffer(res, &size) &&
        (g_capturing || size == kViewCbBytes || (faith_bones::active() && size == 1048576))) {
        std::lock_guard lock(g_mutex);
        g_pending[res] = {out->pData, size};
    } else if (SUCCEEDED(hr) && out && size >= 256 * 1024 && (ctx_hooks::tracing() || faith_bones::active())) {
        std::lock_guard lock(g_mutex);
        g_big_pending[res] = {out->pData, size};  // Stage 7 recon: Faith's bone data
    }
}

void before_unmap(ID3D11Resource* res) {
    Pending p{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_pending.find(res);
        if (it != g_pending.end()) { p = it->second; g_pending.erase(it); }
    }
    Pending big{};
    {
        std::lock_guard lock(g_mutex);
        auto it = g_big_pending.find(res);
        if (it != g_big_pending.end()) { big = it->second; g_big_pending.erase(it); }
    }
    if (big.data) {
        if (ctx_hooks::tracing()) ctx_hooks::on_big_write(res, big.data, big.size);
        faith_bones::on_bone_write(res, big.data, big.size);
    }
    if (p.data) {
        watch_view(p.data, p.size);
        faith_bones::on_cb_write(res, p.data, p.size);
        if (p.size == kViewCbBytes && looks_like_view_cb(static_cast<const float*>(p.data)))
            taa_eyes::record_view_cb(static_cast<const float*>(p.data), p.size / 4);
        if (g_capturing) record("Map", res, p.data, p.size);
    }
}

void before_update(ID3D11Resource* res, const D3D11_BOX* box, const void* data) {
    UINT size = 0;
    if (data && !box && is_constant_buffer(res, &size)) {
        watch_view(data, size);
        if (g_capturing) record("UpdateSubresource", res, data, size);
        if (size == kViewCbBytes && looks_like_view_cb(static_cast<const float*>(data)))
            taa_eyes::record_view_cb(static_cast<const float*>(data), size / 4);
    }
}

namespace {

HRESULT STDMETHODCALLTYPE map_detour(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub, D3D11_MAP type,
                                     UINT flags, D3D11_MAPPED_SUBRESOURCE* out) {
    const hook_guard::Inside in;
    const HRESULT hr = g_map_orig(ctx, res, sub, type, flags, out);
    if (!in.nested) after_map(res, hr, out);
    return hr;
}
void STDMETHODCALLTYPE unmap_detour(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub) {
    const hook_guard::Inside in;
    if (!in.nested) before_unmap(res);
    g_unmap_orig(ctx, res, sub);
}
void STDMETHODCALLTYPE update_detour(ID3D11DeviceContext* ctx, ID3D11Resource* res, UINT sub, const D3D11_BOX* box,
                                     const void* data, UINT row, UINT depth) {
    const hook_guard::Inside in;
    if (!in.nested) before_update(res, box, data);
    g_update_orig(ctx, res, sub, box, data, row, depth);
}

void write_capture() {
    std::vector<Record> recs;
    {
        std::lock_guard lock(g_mutex);
        recs.swap(g_records);
        g_pending.clear();
    }
    wchar_t name[64];
    swprintf_s(name, L"capture_%d.txt", ++g_capture_index);
    FILE* f = nullptr;
    if (_wfopen_s(&f, (g_dir / name).c_str(), L"w") != 0 || !f) return;
    fprintf(f, "# one frame, %zu constant-buffer writes\n", recs.size());
    int flagged = 0;
    for (size_t i = 0; i < recs.size(); ++i) {
        auto& r = recs[i];
        fprintf(f, "\n[%zu] %s buf=%p bytes=%zu\n", i, r.how.c_str(), r.res, r.floats.size() * 4);
        for (size_t m = 0; m + 16 <= r.floats.size(); m += 4) {  // 16-byte aligned 4x4 candidates
            std::string tags = classify(&r.floats[m]);
            if (tags.empty()) continue;
            ++flagged;
            fprintf(f, "  @float %zu:%s\n", m, tags.c_str());
            for (int row = 0; row < 4; ++row)
                fprintf(f, "     %12.5f %12.5f %12.5f %12.5f\n", r.floats[m + row * 4], r.floats[m + row * 4 + 1],
                        r.floats[m + row * 4 + 2], r.floats[m + row * 4 + 3]);
        }
        size_t n = r.floats.size() < 64 ? r.floats.size() : 64;
        fprintf(f, "  raw:");
        for (size_t k = 0; k < n; ++k) fprintf(f, "%s%.4g", k % 8 ? " " : "\n   ", r.floats[k]);
        fprintf(f, "\n");
    }
    fclose(f);
    LOG_I("capture", "wrote capture_%d.txt: %zu CB writes, %d matrix candidates", g_capture_index, recs.size(), flagged);
}

}  // namespace

bool install(const std::filesystem::path& out_dir, bool recon) {
    g_dir = out_dir;
    g_recon = recon;
    cam_hunt::set_output_dir(out_dir);
    auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(
        GetProcAddress(GetModuleHandleW(L"d3d11.dll"), "D3D11CreateDevice"));
    if (!create) return false;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    if (FAILED(create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
        return false;
    void** vt = *reinterpret_cast<void***>(ctx);
    void* map_t = vt[14];
    void* unmap_t = vt[15];
    void* update_t = vt[48];
    ctx->Release();
    dev->Release();

    bool ok = MH_CreateHook(map_t, reinterpret_cast<void*>(&map_detour), reinterpret_cast<void**>(&g_map_orig)) == MH_OK &&
              MH_CreateHook(unmap_t, reinterpret_cast<void*>(&unmap_detour), reinterpret_cast<void**>(&g_unmap_orig)) == MH_OK &&
              MH_CreateHook(update_t, reinterpret_cast<void*>(&update_detour), reinterpret_cast<void**>(&g_update_orig)) == MH_OK;
    if (!ok || MH_EnableHook(map_t) != MH_OK || MH_EnableHook(unmap_t) != MH_OK || MH_EnableHook(update_t) != MH_OK) {
        LOG_E("capture", "context hooks failed");
        return false;
    }
    LOG_I("capture", "constant-buffer capture ready: press F10 in game or create mecvr\\CAPTURE");
    return true;
}

void on_present() {
    int v = g_views_this_frame.exchange(0);
    ++g_present_count;
    static int view_poll = 0;
    if (++view_poll % 30 == 0 && g_view_dumps_left.load() == 0) {
        std::error_code ec;
        if (std::filesystem::remove(g_dir / L"VIEWCB", ec)) {
            wchar_t name[32];
            swprintf_s(name, L"viewcb_%d.txt", ++g_view_dump_index);
            std::lock_guard lock(g_mutex);
            if (_wfopen_s(&g_view_dump, (g_dir / name).c_str(), L"w") == 0 && g_view_dump) g_view_dumps_left = kViewDumps;
        }
    }
    ++g_views_hist[v > 4 ? 4 : v];
    static ULONGLONG last = 0;
    if (GetTickCount64() - last > 10000) {
        last = GetTickCount64();
        LOG_I("capture", "view-constant uploads per frame: 0:%llu 1:%llu 2:%llu 3:%llu 4+:%llu", g_views_hist[0], g_views_hist[1],
              g_views_hist[2], g_views_hist[3], g_views_hist[4]);
        if (g_proj_sx.load() > 0.01f && g_proj_sy.load() > 0.01f)
            LOG_I("capture", "game projection: %.1f x %.1f deg (aspect %.3f)", 2 * std::atan(1.f / g_proj_sx.load()) * 57.2958f,
                  2 * std::atan(1.f / g_proj_sy.load()) * 57.2958f, g_proj_sy.load() / g_proj_sx.load());
        for (auto& h : g_views_hist) h = 0;
    }
    if (!g_recon) return;
    cam_hunt::on_present();
    // Frame boundary: finish an in-flight capture, or start one if armed.
    if (g_capturing) {
        g_capturing = false;
        write_capture();
    }
    static bool key_was_down = false;
    bool key_down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (key_down && !key_was_down) g_armed = true;
    key_was_down = key_down;
    static int file_poll = 0;
    if (++file_poll % 30 == 0) {
        std::error_code ec;
        if (std::filesystem::remove(g_dir / L"CAPTURE", ec)) g_armed = true;
    }
    if (g_armed.exchange(false)) {
        std::lock_guard lock(g_mutex);
        g_records.clear();
        g_pending.clear();
        g_capturing = true;
    }
}

}  // namespace mecvr::cb_capture

namespace mecvr::cb_capture {
bool last_view_cb(float* out) {
    std::lock_guard lock(g_mutex);
    if (!g_have_last_view) return false;
    memcpy(out, g_last_view, sizeof(g_last_view));
    return true;
}
}  // namespace mecvr::cb_capture
