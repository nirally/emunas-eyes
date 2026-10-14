// Recon tool (Stage 2 eye sync): finds how the game measures frame time. Hooks the Windows
// clock functions, counts calls per (function, thread, caller) over a window of game frames,
// and writes time_recon.txt sorted by count. A caller on the game thread with ~1 call per
// frame is a frame-timer candidate. The hooks stay installed but pass straight through
// outside the window.
#include "diagnostics/time_recon.h"

#include <windows.h>

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <intrin.h>
#include <mutex>
#include <vector>

#include "diagnostics/log.h"

namespace mecvr::time_recon {
namespace {

enum Fn { kQpc, kTick64, kTick, kTimeGet, kSysTime, kSysTimePrecise, kClockWrap, kFnCount };
const char* const kFnNames[kFnCount] = {"QueryPerformanceCounter", "GetTickCount64", "GetTickCount",
                                        "timeGetTime", "GetSystemTimeAsFileTime",
                                        "GetSystemTimePreciseAsFileTime", "exe clock wrapper 2a9fcd0"};

struct Entry {
    int fn;
    DWORD tid;
    uintptr_t ra;
    uintptr_t up[3];  // return addresses further up the stack (clock wrapper only)
    uint64_t count;
};

constexpr uint64_t kStartFrame = 300, kEndFrame = 900;  // ~5 s in, ~10 s window at the menu

std::filesystem::path g_out;
std::atomic<bool> g_installed{false}, g_recording{false}, g_done{false};
std::atomic<uint64_t> g_frames{0};
std::atomic<DWORD> g_game_tid{0};
std::mutex g_mutex;
std::vector<Entry> g_entries;

void note(int fn, void* ra, const uintptr_t* up = nullptr) {
    if (!g_recording.load(std::memory_order_relaxed)) return;
    const DWORD tid = GetCurrentThreadId();
    const auto addr = reinterpret_cast<uintptr_t>(ra);
    uintptr_t u[3] = {};
    if (up) memcpy(u, up, sizeof(u));
    std::lock_guard lock(g_mutex);
    for (auto& e : g_entries)
        if (e.fn == fn && e.tid == tid && e.ra == addr && !memcmp(e.up, u, sizeof(u))) { ++e.count; return; }
    if (g_entries.size() < 4096) g_entries.push_back({fn, tid, addr, {u[0], u[1], u[2]}, 1});
}

using QpcFn = BOOL(WINAPI*)(LARGE_INTEGER*);
using Tick64Fn = ULONGLONG(WINAPI*)();
using TickFn = DWORD(WINAPI*)();
using SysTimeFn = void(WINAPI*)(FILETIME*);
QpcFn o_qpc;
Tick64Fn o_tick64;
TickFn o_tick, o_timeget;
SysTimeFn o_systime, o_systime_precise;

BOOL WINAPI d_qpc(LARGE_INTEGER* v) { note(kQpc, _ReturnAddress()); return o_qpc(v); }
ULONGLONG WINAPI d_tick64() { note(kTick64, _ReturnAddress()); return o_tick64(); }
DWORD WINAPI d_tick() { note(kTick, _ReturnAddress()); return o_tick(); }
DWORD WINAPI d_timeget() { note(kTimeGet, _ReturnAddress()); return o_timeget(); }
void WINAPI d_systime(FILETIME* f) { note(kSysTime, _ReturnAddress()); o_systime(f); }
void WINAPI d_systime_precise(FILETIME* f) { note(kSysTimePrecise, _ReturnAddress()); o_systime_precise(f); }
// The engine's now() (exe+0x2a9fcd0, 1.0.3.47248): returns the raw QPC count. First recon run showed
// it called ~22k times per frame on the game thread, so the frame timer is one of its callers.
using WrapFn = int64_t (*)();
WrapFn o_wrap;
int64_t d_wrap() {
    if (g_recording.load(std::memory_order_relaxed)) {
        void* frames[5] = {};
        // frames[0] = inside d_wrap, [1] = the caller (same as _ReturnAddress), [2..4] = further up.
        RtlCaptureStackBackTrace(0, 5, frames, nullptr);
        const uintptr_t up[3] = {reinterpret_cast<uintptr_t>(frames[2]), reinterpret_cast<uintptr_t>(frames[3]),
                                 reinterpret_cast<uintptr_t>(frames[4])};
        note(kClockWrap, _ReturnAddress(), up);
    }
    return o_wrap();
}
// The game timer update (exe+0x32f5aa0): once per game frame, before the camera. Logs its fields
// once a second: +0x28 mode, +0x38 tick rate, +0x3c/+0x40 tick target/current, +0x44 frame count,
// +0x58 dt (scaled), +0x60 dt (unscaled), +0x68 time scale, +0xc0/+0xc8 tick base.
using TimerFn = void (*)(uint8_t*);
TimerFn o_timer;
void d_timer(uint8_t* t) {
    o_timer(t);
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 1000) return;
    last = now;
    auto i32 = [t](int off) { return *reinterpret_cast<int32_t*>(t + off); };
    auto f64 = [t](int off) { return *reinterpret_cast<double*>(t + off); };
    const uint8_t* cfg = *reinterpret_cast<uint8_t**>(t + 0x30);
    LOG_I("recon", "timer %p: mode %d rate %d ticks %d/%d frame %d dt %.5f raw %.5f scale %.3f base %.4f/%lld | "
          "cfg +2c %.2f +34 %.2f +38 %.2f +44 %d +50 %d +54 %d +55 %d +58 %d +5a %d", t, i32(0x28), i32(0x38),
          i32(0x3c), i32(0x40), i32(0x44), f64(0x58), f64(0x60), f64(0x68), f64(0xc0),
          *reinterpret_cast<long long*>(t + 0xc8), *reinterpret_cast<const float*>(cfg + 0x2c),
          *reinterpret_cast<const float*>(cfg + 0x34), *reinterpret_cast<const float*>(cfg + 0x38),
          *reinterpret_cast<const int32_t*>(cfg + 0x44), *reinterpret_cast<const int32_t*>(cfg + 0x50), cfg[0x54],
          cfg[0x55], cfg[0x58], cfg[0x5a]);
}
constexpr uintptr_t kTimerRva = 0x32f5aa0;
constexpr uint8_t kTimerBytes[] = {0x48, 0x89, 0xE0, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x01};
constexpr uintptr_t kWrapRva = 0x2a9fcd0;
constexpr uint8_t kWrapBytes[] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xFF, 0x15};

template <class T>
void hook(const wchar_t* module, const char* name, void* detour, T* orig) {
    HMODULE m = GetModuleHandleW(module);
    void* target = m ? reinterpret_cast<void*>(GetProcAddress(m, name)) : nullptr;
    if (!target) { LOG_W("recon", "time recon: %s not found", name); return; }
    if (MH_CreateHook(target, detour, reinterpret_cast<void**>(orig)) != MH_OK || MH_EnableHook(target) != MH_OK)
        LOG_W("recon", "time recon: hooking %s failed", name);
}

// "exe+rva" or "module+rva" for a code address.
void describe(uintptr_t addr, char* buf, size_t n) {
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(addr), &m) && m) {
        char path[MAX_PATH];
        GetModuleFileNameA(m, path, MAX_PATH);
        const char* base = strrchr(path, '\\');
        base = base ? base + 1 : path;
        if (m == GetModuleHandleW(nullptr)) base = "exe";
        snprintf(buf, n, "%s+%llx", base, (unsigned long long)(addr - reinterpret_cast<uintptr_t>(m)));
    } else {
        snprintf(buf, n, "%llx", (unsigned long long)addr);
    }
}

void write_report() {
    std::vector<Entry> entries;
    {
        std::lock_guard lock(g_mutex);
        entries = g_entries;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.count > b.count; });
    const double frames = double(kEndFrame - kStartFrame);
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_out.c_str(), L"w") != 0 || !f) return;
    fprintf(f, "# time recon: %.0f game frames, game thread %lu (thread that runs the main camera site)\n", frames,
            g_game_tid.load());
    fprintf(f, "# calls/frame  calls  thread  function  caller\n");
    for (const auto& e : entries) {
        char where[300];
        describe(e.ra, where, sizeof(where));
        fprintf(f, "%10.2f %8llu %6lu%s %-32s %s", e.count / frames, (unsigned long long)e.count, e.tid,
                e.tid == g_game_tid.load() ? "*" : " ", kFnNames[e.fn], where);
        for (uintptr_t up : e.up) {
            if (!up) break;
            describe(up, where, sizeof(where));
            fprintf(f, " <- %s", where);
        }
        fputc('\n', f);
    }
    fclose(f);
    LOG_I("recon", "time recon: %zu call sites written to %ls", entries.size(), g_out.c_str());
}

}  // namespace

void install(const std::filesystem::path& out) {
    g_out = out;
    g_installed = true;
    hook(L"kernel32.dll", "QueryPerformanceCounter", &d_qpc, &o_qpc);
    hook(L"kernel32.dll", "GetTickCount64", &d_tick64, &o_tick64);
    hook(L"kernel32.dll", "GetTickCount", &d_tick, &o_tick);
    hook(L"winmm.dll", "timeGetTime", &d_timeget, &o_timeget);
    hook(L"kernel32.dll", "GetSystemTimeAsFileTime", &d_systime, &o_systime);
    hook(L"kernel32.dll", "GetSystemTimePreciseAsFileTime", &d_systime_precise, &o_systime_precise);
    auto* wrap = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + kWrapRva;
    if (memcmp(wrap, kWrapBytes, sizeof(kWrapBytes)) == 0) {
        if (MH_CreateHook(wrap, reinterpret_cast<void*>(&d_wrap), reinterpret_cast<void**>(&o_wrap)) != MH_OK ||
            MH_EnableHook(wrap) != MH_OK)
            LOG_W("recon", "time recon: hooking the clock wrapper failed");
    } else {
        LOG_W("recon", "time recon: clock wrapper bytes differ - not hooked");
    }
    auto* timer = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + kTimerRva;
    if (memcmp(timer, kTimerBytes, sizeof(kTimerBytes)) == 0) {
        if (MH_CreateHook(timer, reinterpret_cast<void*>(&d_timer), reinterpret_cast<void**>(&o_timer)) != MH_OK ||
            MH_EnableHook(timer) != MH_OK)
            LOG_W("recon", "time recon: hooking the game timer failed");
    } else {
        LOG_W("recon", "time recon: game timer bytes differ - not hooked");
    }
    LOG_I("recon", "time recon armed: records game frames %llu..%llu", (unsigned long long)kStartFrame,
          (unsigned long long)kEndFrame);
}

void on_game_frame() {
    if (!g_installed.load(std::memory_order_relaxed) || g_done.load(std::memory_order_relaxed)) return;
    g_game_tid = GetCurrentThreadId();
    const uint64_t n = ++g_frames;
    if (n == kStartFrame) g_recording = true;
    if (n == kEndFrame) {
        g_recording = false;
        g_done = true;
        write_report();
    }
}

}  // namespace mecvr::time_recon
