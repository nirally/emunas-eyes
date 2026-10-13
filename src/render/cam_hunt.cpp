#include "render/cam_hunt.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "diagnostics/log.h"
#include "diagnostics/hwbp.h"

namespace mecvr::cam_hunt {
namespace {

DWORD WINAPI analyse_thread(LPVOID);

struct Vec3 { float x, y, z; };

enum class Phase { Idle, Scanning, Poking, Verify, Done };
std::atomic<Phase> g_phase{Phase::Idle};
std::vector<uintptr_t> g_feeders;   // candidates proven to move the view
std::mutex g_mutex;

std::filesystem::path g_dir;
Vec3 g_cam{};                      // camera position at scan time
float g_rot[9] = {};               // view rotation rows at scan time
std::vector<uintptr_t> g_hits;     // addresses of matching xyz triplets
size_t g_poke_index = 0;           // candidate under test
int g_poke_frames = 0;
Vec3 g_saved{};                    // original value at the candidate
float g_view_y_before = 0;         // camera y from the view matrix before poking
FILE* g_report = nullptr;
float g_last_cam_y = 0;
std::atomic<bool> g_hunt_requested{false};
std::atomic<bool> g_trace_running{false};

// Stable path found at the main menu on two launches (D-009): [[exe+0x23e7f78]+0x400]+0x70 = game camera position.
constexpr uintptr_t kCamSlotRva = 0x23e7f78;
constexpr uintptr_t kCamNodeOffset = 0x400;
constexpr uintptr_t kCamPosOffset = 0x70;

Vec3 camera_position(const float* v) {
    // view = [R | t] with translation in column 3; camera = -R^T t
    float t0 = v[3], t1 = v[7], t2 = v[11];
    return {-(v[0] * t0 + v[4] * t1 + v[8] * t2), -(v[1] * t0 + v[5] * t1 + v[9] * t2),
            -(v[2] * t0 + v[6] * t1 + v[10] * t2)};
}

bool close(float a, float b) { return std::fabs(a - b) < 0.002f; }

size_t scan_region(uintptr_t b, uintptr_t e, Vec3 c, uintptr_t* out, size_t cap) {
    size_t n = 0;
    __try {
        for (uintptr_t p = (b + 3) & ~uintptr_t(3); p + 12 <= e && n < cap; p += 4) {
            const float* f = reinterpret_cast<const float*>(p);
            if (close(f[0], c.x) && close(f[1], c.y) && close(f[2], c.z)) out[n++] = p;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

bool safe_read(uintptr_t p, void* out, size_t n) {
    __try { memcpy(out, reinterpret_cast<const void*>(p), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool safe_write(uintptr_t p, const void* in, size_t n) {
    __try { memcpy(reinterpret_cast<void*>(p), in, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void scan_all(Vec3 c) {
    std::vector<uintptr_t> hits;
    uintptr_t buf[256];
    uintptr_t cur = 0x10000;
    MEMORY_BASIC_INFORMATION mbi{};
    auto self = reinterpret_cast<uintptr_t>(&scan_all);
    while (VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi))) {
        uintptr_t b = reinterpret_cast<uintptr_t>(mbi.BaseAddress), e = b + mbi.RegionSize;
        bool rw = mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) &&
                  !(mbi.Protect & PAGE_GUARD);
        if (rw && mbi.RegionSize < (1ull << 30) && !(self >= b && self < e)) {
            size_t n = scan_region(b, e, c, buf, 256);
            hits.insert(hits.end(), buf, buf + n);
        }
        if (e <= cur) break;
        cur = e;
    }
    std::lock_guard lock(g_mutex);
    g_hits = std::move(hits);
}

// Rotation rows sit in the three 16-byte rows before the position (Frostbite
// LinearTransform: right, up, forward, trans). Accept either sign.
bool row_matches(const float* row, const float* want) {
    auto eq = [](const float* a, const float* b, float s) {
        return std::fabs(a[0] - s * b[0]) < 0.01f && std::fabs(a[1] - s * b[1]) < 0.01f && std::fabs(a[2] - s * b[2]) < 0.01f;
    };
    return eq(row, want, 1.f) || eq(row, want, -1.f);
}

bool is_transform(uintptr_t a) {
    float m[12];
    if (!safe_read(a - 48, m, sizeof(m))) return false;
    return row_matches(m, g_rot) && row_matches(m + 4, g_rot + 3) && row_matches(m + 8, g_rot + 6);
}

void filter_transforms() {
    std::lock_guard lock(g_mutex);
    std::vector<uintptr_t> keep;
    for (uintptr_t a : g_hits) if (is_transform(a)) keep.push_back(a);
    g_hits.swap(keep);
}

void filter_stable() {
    std::lock_guard lock(g_mutex);
    std::vector<uintptr_t> keep;
    for (uintptr_t a : g_hits) {
        Vec3 v{};
        if (safe_read(a, &v, sizeof(v)) && close(v.x, g_cam.x) && close(v.y, g_cam.y) && close(v.z, g_cam.z) && is_transform(a))
            keep.push_back(a);
    }
    g_hits.swap(keep);
}

DWORD WINAPI scan_thread(LPVOID) {
    LOG_I("camhunt", "scanning memory for camera position (%.3f, %.3f, %.3f)...", g_cam.x, g_cam.y, g_cam.z);
    scan_all(g_cam);
    LOG_I("camhunt", "%zu candidate copies found", g_hits.size());
    filter_transforms();
    LOG_I("camhunt", "%zu are full transforms (rotation next to position)", g_hits.size());
    Sleep(2000);
    filter_stable();
    LOG_I("camhunt", "%zu still hold the camera after 2 s (live copies)", g_hits.size());
    if (_wfopen_s(&g_report, (g_dir / L"camhunt.txt").c_str(), L"w") == 0 && g_report) {
        fprintf(g_report, "camera position %.4f %.4f %.4f\n%zu candidates\n", g_cam.x, g_cam.y, g_cam.z, g_hits.size());
        for (size_t i = 0; i < g_hits.size(); ++i) {
            float ctx[24] = {};
            safe_read(g_hits[i] - 48, ctx, sizeof(ctx));
            MEMORY_BASIC_INFORMATION mbi{};
            VirtualQuery(reinterpret_cast<void*>(g_hits[i]), &mbi, sizeof(mbi));
            fprintf(g_report, "\n#%zu %llx (%s)\n", i, (unsigned long long)g_hits[i],
                    mbi.Type == MEM_IMAGE ? "image" : mbi.Type == MEM_PRIVATE ? "private" : "mapped");
            for (int r = 0; r < 6; ++r)
                fprintf(g_report, "   %+4d: %11.4f %11.4f %11.4f %11.4f\n", -48 + r * 16, ctx[r * 4], ctx[r * 4 + 1],
                        ctx[r * 4 + 2], ctx[r * 4 + 3]);
        }
        fprintf(g_report, "\n# poke test (+3.0 on y for 20 frames each; view y should rise if the copy feeds rendering)\n");
        fflush(g_report);
    }
    g_poke_index = 0;
    g_phase = g_hits.empty() ? Phase::Done : Phase::Poking;
    return 0;
}

// Every 8-aligned qword in readable memory within [scan_lo, scan_hi) whose value lies in [lo, hi).
size_t scan_range(uintptr_t b, uintptr_t e, uintptr_t lo, uintptr_t hi, uintptr_t* out_where, uintptr_t* out_val,
                  size_t cap) {
    size_t n = 0;
    __try {
        for (uintptr_t p = (b + 7) & ~uintptr_t(7); p + 8 <= e && n < cap; p += 8) {
            uint64_t v = *reinterpret_cast<const uint64_t*>(p);
            if (v >= lo && v < hi) { out_where[n] = p; out_val[n] = static_cast<uintptr_t>(v); ++n; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

std::vector<std::pair<uintptr_t, uintptr_t>> find_pointers_into(uintptr_t lo, uintptr_t hi, uintptr_t scan_lo,
                                                                uintptr_t scan_hi) {
    std::vector<std::pair<uintptr_t, uintptr_t>> out;
    uintptr_t where[128], val[128];
    uintptr_t cur = scan_lo < 0x10000 ? 0x10000 : scan_lo;
    MEMORY_BASIC_INFORMATION mbi{};
    auto self = reinterpret_cast<uintptr_t>(&find_pointers_into);
    while (cur < scan_hi && VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi))) {
        uintptr_t b = reinterpret_cast<uintptr_t>(mbi.BaseAddress), e = b + mbi.RegionSize;
        if (e > scan_hi) e = scan_hi;
        bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && mbi.Protect &&
                  mbi.RegionSize < (1ull << 30) && !(self >= b && self < e);
        if (ok) {
            size_t n = scan_range(b < cur ? cur : b, e, lo, hi, where, val, 128);
            for (size_t k = 0; k < n && out.size() < 4096; ++k) out.emplace_back(where[k], val[k]);
        }
        if (e <= cur) break;
        cur = e;
    }
    return out;
}

std::string module_of(uint64_t v) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(v), &mod))
        return {};
    char name[MAX_PATH];
    GetModuleFileNameA(mod, name, MAX_PATH);
    const char* base = strrchr(name, '\\');
    char buf[MAX_PATH + 32];
    snprintf(buf, sizeof(buf), "%s+%llx", base ? base + 1 : name,
             (unsigned long long)(v - reinterpret_cast<uintptr_t>(mod)));
    return buf;
}

// After the poke test: who feeds whom, struct context, and which code writes each feeder.
DWORD WINAPI analyse_thread(LPVOID) {
    const auto out = g_dir / L"camtrace.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, out.c_str(), L"a") != 0 || !f) return 0;
    fprintf(f, "\n==================== hunt ====================\n");
    fprintf(f, "camera position %.4f %.4f %.4f, %zu feeders\n", g_cam.x, g_cam.y, g_cam.z, g_feeders.size());

    // 1) Relationship + persistence: write +5 y once, sample every feeder ~30 ms later.
    for (size_t i = 0; i < g_feeders.size(); ++i) {
        Vec3 orig{};
        safe_read(g_feeders[i], &orig, sizeof(orig));
        Vec3 v = orig;
        v.y += 5.f;
        safe_write(g_feeders[i], &v, sizeof(v));
        Sleep(30);
        fprintf(f, "\nwrote +5 y once to feeder %zu (%llx); 30 ms later:\n", i, (unsigned long long)g_feeders[i]);
        for (size_t j = 0; j < g_feeders.size(); ++j) {
            Vec3 now{};
            safe_read(g_feeders[j], &now, sizeof(now));
            const char* note = "";
            if (j == i) note = std::fabs(now.y - g_cam.y) < 0.01f ? "  <- game rewrote it" : "  <- our value persisted";
            fprintf(f, "   feeder %zu y = %.3f (%+.3f vs camera)%s\n", j, now.y, now.y - g_cam.y, note);
        }
        safe_write(g_feeders[i], &orig, sizeof(orig));
        Sleep(30);
    }

    // 2) Struct context: 0x200 bytes before, 0x100 after; qwords flagged when they point into a module.
    for (size_t i = 0; i < g_feeders.size(); ++i) {
        fprintf(f, "\n## context of feeder %zu (%llx), offsets relative to the position\n", i,
                (unsigned long long)g_feeders[i]);
        for (int off = -0x200; off < 0x100; off += 8) {
            uint64_t q = 0;
            if (!safe_read(g_feeders[i] + off, &q, 8)) {
                fprintf(f, "  %+5x  <unreadable>\n", off);
                continue;
            }
            float fl[2];
            memcpy(fl, &q, 8);
            std::string mod = module_of(q);
            fprintf(f, "  %+5x  %016llx  %12.4f %12.4f  %s\n", off, (unsigned long long)q, fl[0], fl[1], mod.c_str());
        }
    }
    fclose(f);

    // 3) Static pointer paths: pointers inside the exe image that lead to each feeder,
    //    directly (level 1) or through one heap object (level 2).
    if (_wfopen_s(&f, out.c_str(), L"a") == 0 && f) {
        auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
        const uintptr_t exe_end = exe + nt->OptionalHeader.SizeOfImage;
        for (size_t i = 0; i < g_feeders.size(); ++i) {
            const uintptr_t lo = g_feeders[i] - 0x800, hi = g_feeders[i] + 0x40;
            fprintf(f, "\n## pointer paths to feeder %zu (%llx); window -0x800..+0x40\n", i, (unsigned long long)g_feeders[i]);
            auto l1 = find_pointers_into(lo, hi, exe, exe_end);
            for (auto& [where, val] : l1)
                fprintf(f, "  L1 exe+%llx -> %llx (feeder %+lld)\n", (unsigned long long)(where - exe),
                        (unsigned long long)val, (long long)(g_feeders[i] - val));
            if (l1.empty()) {
                auto heap = find_pointers_into(lo, hi, 0, ~uintptr_t(0));
                int shown = 0;
                for (auto& [h, hval] : heap) {
                    if (h >= exe && h < exe_end) continue;
                    auto l2 = find_pointers_into(h - 0x400, h + 8, exe, exe_end);
                    for (auto& [where, val] : l2) {
                        fprintf(f, "  L2 exe+%llx -> %llx; [+%llx] -> %llx (feeder %+lld)\n",
                                (unsigned long long)(where - exe), (unsigned long long)val,
                                (unsigned long long)(h - val), (unsigned long long)hval, (long long)(g_feeders[i] - hval));
                        if (++shown >= 40) break;
                    }
                    if (shown >= 40) break;
                }
                if (!shown) fprintf(f, "  none found (%zu heap refs, no static parent)\n", heap.size());
            }
            fflush(f);
        }
        fclose(f);
    }

    // 4) Which code writes the position y of each feeder.
    for (size_t i = 0; i < g_feeders.size(); ++i) {
        char label[32];
        snprintf(label, sizeof(label), "feeder %zu y", i);
        hwbp::trace_writes(g_feeders[i] + 4, 1500, out, label);
    }
    LOG_I("camhunt", "analysis done, see camtrace.txt");
    return 0;
}

// Resolves the menu-found pointer chain. 0 if any link is unreadable.
uintptr_t resolve_chain_pos() {
    auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint64_t slot = 0, node = 0;
    if (!safe_read(exe + kCamSlotRva, &slot, 8) || !slot) return 0;
    if (!safe_read(static_cast<uintptr_t>(slot) + kCamNodeOffset, &node, 8) || !node) return 0;
    return static_cast<uintptr_t>(node) + kCamPosOffset;
}

// F9: while the camera moves, check the chain still tracks it and record which
// code writes the chain camera and every feeder from the last hunt.
DWORD WINAPI trace_thread(LPVOID) {
    const auto out = g_dir / L"camtrace.txt";
    uintptr_t chain = resolve_chain_pos();
    Vec3 v{};
    bool readable = chain && safe_read(chain, &v, sizeof(v));
    float view_y = g_last_cam_y;
    LOG_I("camhunt", "F9 trace: chain -> %llx %s (%.3f, %.3f, %.3f); view y now %.3f", (unsigned long long)chain,
          readable ? "" : "UNREADABLE", v.x, v.y, v.z, view_y);
    FILE* f = nullptr;
    if (_wfopen_s(&f, out.c_str(), L"a") == 0 && f) {
        fprintf(f, "\n==================== F9 trace ====================\nchain [[exe+%llx]+%llx]+%llx -> %llx = (%.4f, %.4f, %.4f); view camera y %.4f\n",
                (unsigned long long)kCamSlotRva, (unsigned long long)kCamNodeOffset, (unsigned long long)kCamPosOffset,
                (unsigned long long)chain, v.x, v.y, v.z, view_y);
        fclose(f);
    }
    if (readable) hwbp::trace_writes(chain + 4, 1500, out, "chain camera y");
    std::vector<uintptr_t> feeders;
    {
        std::lock_guard lock(g_mutex);
        feeders = g_feeders;
    }
    for (size_t i = 0; i < feeders.size(); ++i) {
        char label[48];
        snprintf(label, sizeof(label), "hunt feeder %zu y", i);
        hwbp::trace_writes(feeders[i] + 4, 1500, out, label);
    }
    LOG_I("camhunt", "F9 trace done");
    g_trace_running = false;
    return 0;
}

void poll_keys() {
    static bool f8_was = false, f9_was = false;
    bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    std::error_code ec;
    static int file_poll = 0;
    bool file_hunt = false, file_trace = false;
    if (++file_poll % 30 == 0 && !g_dir.empty()) {
        file_hunt = std::filesystem::remove(g_dir / L"HUNT", ec);
        file_trace = std::filesystem::remove(g_dir / L"TRACE", ec);
    }
    if ((f8 && !f8_was) || file_hunt) {
        Phase p = g_phase.load();
        if (p == Phase::Idle || p == Phase::Done) {
            std::lock_guard lock(g_mutex);
            g_hits.clear();
            g_feeders.clear();
            g_poke_index = 0;
            g_poke_frames = 0;
            g_phase = Phase::Idle;
            g_hunt_requested = true;
            LOG_I("camhunt", "hunt requested (F8) - keep the camera still until 'analysis done'");
        } else {
            LOG_W("camhunt", "F8 ignored: a hunt is already running");
        }
    }
    if (((f9 && !f9_was) || file_trace) && !g_trace_running.exchange(true)) {
        if (HANDLE t = CreateThread(nullptr, 0, trace_thread, nullptr, 0, nullptr)) CloseHandle(t);
        else g_trace_running = false;
    }
    f8_was = f8;
    f9_was = f9;
}

}  // namespace

void set_output_dir(const std::filesystem::path& dir) { g_dir = dir; }

void on_view_matrix(const float* view, const std::filesystem::path& out_dir) {
    Vec3 c = camera_position(view);
    g_last_cam_y = c.y;
    if (g_dir.empty()) g_dir = out_dir;
    if (g_phase == Phase::Idle && g_hunt_requested.exchange(false)) {
        g_dir = out_dir;
        g_cam = c;
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3; ++k) g_rot[r * 3 + k] = view[r * 4 + k];
        g_phase = Phase::Scanning;
        if (HANDLE t = CreateThread(nullptr, 0, scan_thread, nullptr, 0, nullptr)) CloseHandle(t);
    }
}

void on_present() {
    poll_keys();
    if (g_phase != Phase::Poking) return;
    std::lock_guard lock(g_mutex);
    if (g_poke_index >= g_hits.size() || g_poke_index >= 400) {
        g_phase = Phase::Done;
        LOG_I("camhunt", "poke test finished, %zu feeders; analysing", g_feeders.size());
        if (g_report) { fclose(g_report); g_report = nullptr; }
        if (!g_feeders.empty())
            if (HANDLE t = CreateThread(nullptr, 0, analyse_thread, nullptr, 0, nullptr)) CloseHandle(t);
        return;
    }
    uintptr_t a = g_hits[g_poke_index];
    if (g_poke_frames == 0) {
        if (!safe_read(a, &g_saved, sizeof(g_saved))) { ++g_poke_index; return; }
        g_view_y_before = g_last_cam_y;
    }
    if (g_poke_frames < 20) {
        Vec3 v = g_saved;
        v.y += 3.0f;
        safe_write(a, &v, sizeof(v));  // re-applied every frame in case the game rewrites it
        ++g_poke_frames;
        return;
    }
    // Restore, then judge by the view y seen during the poke.
    safe_write(a, &g_saved, sizeof(g_saved));
    float dy = g_last_cam_y - g_view_y_before;
    bool moved = std::fabs(dy - 3.0f) < 0.5f;
    if (g_report) {
        fprintf(g_report, "#%zu %llx: view y %+.3f %s\n", g_poke_index, (unsigned long long)a, dy,
                moved ? "<== FEEDS RENDERING" : "");
        fflush(g_report);
    }
    if (moved) LOG_I("camhunt", "candidate #%zu at %llx feeds rendering (view moved %+.2f m)", g_poke_index, (unsigned long long)a, dy);
    if (moved) g_feeders.push_back(a);
    g_poke_frames = 0;
    ++g_poke_index;
}

}  // namespace mecvr::cam_hunt
