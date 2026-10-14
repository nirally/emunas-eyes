// Eye sync: hold game time still on right-eye frames (D-021). See time_sync.h.
//
// The frame function (exe+0x2a2d470 in 1.0.3.47248) does, once per game frame and before the camera:
//   timer->update()                         exe+0x32f5aa0
//   dt    = timer[+0x58]                    (seconds, scaled)
//   ticks = timer[+0x3c] - timer[+0x40]     (fixed 30 Hz simulation steps to run now)
//   alpha = timer[+0x230] * timer[+0x68]    (interpolation between steps, x time scale)
//   if (ticks) simulate()
// Freezing a frame = skip update(), dt 0, ticks 0; alpha keeps the left frame's value. The timer's
// stopwatches don't move, so the next update measures both frames, and the tick target is computed
// from absolute time, so the simulation catches up on the next (left-eye) frame.
#include "render/time_sync.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstring>

#include <MinHook.h>

#include "core/sigscan.h"
#include "diagnostics/log.h"
#include "render/camera_override.h"

namespace mecvr::time_sync {
namespace {

// mov rcx,[rsi+60]; call update; mov rcx,[rsi+60]; call dt; cvt...; call ticks; mov r15d,eax; ...; call alpha
constexpr const char* kSiteSig =
    "48 8B 4E 60 E8 ?? ?? ?? ?? 48 8B 4E 60 E8 ?? ?? ?? ?? 45 0F 57 D2 F2 44 0F 5A D0 "
    "48 8B 4E 60 E8 ?? ?? ?? ?? 41 89 C7 48 8B 4E 60 E8";
constexpr size_t kUpdateCall = 4, kDtCall = 0x0d, kTicksCall = 0x1f, kAlphaCall = 0x2b;
constexpr uint8_t kDtGetter[] = {0xF2, 0x0F, 0x10, 0x41, 0x58, 0xC3};            // movsd xmm0,[rcx+58]; ret
constexpr uint8_t kTicksGetter[] = {0x8B, 0x41, 0x3C, 0x2B, 0x41, 0x40, 0xC3};   // eax=[rcx+3c]-[rcx+40]
constexpr uint8_t kAlphaGetter[] = {0xF2, 0x0F, 0x10, 0x81, 0x30, 0x02, 0x00, 0x00,
                                    0xF2, 0x0F, 0x59, 0x41, 0x68, 0xC3};         // [rcx+230]*[rcx+68]
constexpr int kDt = 0x58, kDtRaw = 0x60, kTickTarget = 0x3c, kTickDone = 0x40;

using UpdateFn = void (*)(uint8_t*);
UpdateFn g_orig = nullptr;
uintptr_t g_site_return = 0;
std::atomic<bool> g_enabled{true}, g_dirty{false};
// dt reported on held frames (diagnostics.sync_hold_dt). Not 0: on a dt == 0 frame the game poses
// Faith's body differently (PLAYTEST-2d: body flickered; frame diffs: held pairs 1.3% different at
// 0, identical at 0.1 ms). The next frame gives it back, so game time stays exact.
std::atomic<double> g_hold_dt{0.0001};
double g_owed = 0;  // game thread: dt handed out on the last held frame
bool g_installed = false;

// Game thread only.
uint64_t g_main_seen = 0;
bool g_froze_last = false;
std::atomic<uint64_t> g_frames{0}, g_frozen{0}, g_ticks_held{0};
std::atomic<int32_t> g_tick_now{0};  // simulation tick reached (stats: should advance 30 per second)
std::atomic<bool> g_paused{false};  // the game's own dt was 0 on the last normal frame (menus, pause)

void __fastcall update_detour(uint8_t* timer) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != g_site_return) { g_orig(timer); return; }
    ++g_frames;
    // Freeze only a frame the camera site will render as the right eye, only if the camera site ran
    // since the last update (no camera → the eye never flips, and every frame would freeze), and
    // never two frames in a row.
    const uint64_t main = camera_override::main_site_count();
    const bool camera_ran = main != g_main_seen;
    g_main_seen = main;
    const bool freeze = g_enabled.load() && camera_ran && !g_froze_last && camera_override::next_frame_is_right_eye();
    g_froze_last = freeze;
    if (!freeze) {
        g_orig(timer);
        g_paused = *reinterpret_cast<double*>(timer + kDt) <= 0.0;
        if (g_owed > 0) {
            for (int off : {kDt, kDtRaw}) {
                auto& dt = *reinterpret_cast<double*>(timer + off);
                if (dt > g_owed) dt -= g_owed;
            }
            g_owed = 0;
        }
        g_tick_now = *reinterpret_cast<int32_t*>(timer + kTickTarget);
        return;
    }
    ++g_frozen;
    auto& target = *reinterpret_cast<int32_t*>(timer + kTickTarget);
    auto& done = *reinterpret_cast<int32_t*>(timer + kTickDone);
    if (target != done) g_ticks_held += 1;  // this frame would otherwise have repeated last frame's ticks
    done = target;
    // The fields still hold the left frame's dt; 0 there means the game is paused (time scale 0)
    // and the held frame must stay at exactly 0 too.
    g_owed = *reinterpret_cast<double*>(timer + kDt) > 0 ? g_hold_dt.load() : 0.0;
    *reinterpret_cast<double*>(timer + kDt) = g_owed;
    *reinterpret_cast<double*>(timer + kDtRaw) = g_owed;
}

bool key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

}  // namespace

bool install() {
    auto* exe = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto pat = sigscan::parse(kSiteSig);
    const uint8_t* site = pat ? sigscan::find_in_module(exe, *pat) : nullptr;
    if (!site) {
        LOG_E("sync", "game timer call site not found - eyes stay one frame apart");
        return false;
    }
    auto target = [site](size_t off) { return sigscan::rip_relative(site + off, 1, 5); };
    if (memcmp(target(kDtCall), kDtGetter, sizeof(kDtGetter)) != 0 ||
        memcmp(target(kTicksCall), kTicksGetter, sizeof(kTicksGetter)) != 0 ||
        memcmp(target(kAlphaCall), kAlphaGetter, sizeof(kAlphaGetter)) != 0) {
        LOG_E("sync", "game timer layout differs (getters at exe+%llx) - eyes stay one frame apart",
              (unsigned long long)(site - exe));
        return false;
    }
    auto* update = const_cast<uint8_t*>(target(kUpdateCall));
    g_site_return = reinterpret_cast<uintptr_t>(site + kUpdateCall + 5);
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return false;
    st = MH_CreateHook(update, reinterpret_cast<void*>(&update_detour), reinterpret_cast<void**>(&g_orig));
    if (st == MH_OK) st = MH_EnableHook(update);
    if (st != MH_OK) {
        LOG_E("sync", "hooking the game timer failed: %s", MH_StatusToString(st));
        return false;
    }
    g_installed = true;
    LOG_I("sync", "game timer exe+%llx hooked; frame call returns to exe+%llx (eye sync %s)",
          (unsigned long long)(update - exe), (unsigned long long)(g_site_return - reinterpret_cast<uintptr_t>(exe)),
          g_enabled.load() ? "on" : "off");
    return true;
}

void set_enabled(bool on) {
    g_enabled = on;
    LOG_I("sync", "eye sync %s", on ? "ON (world held still for the right eye)" : "OFF (eyes one frame apart)");
}
bool enabled() { return g_enabled.load(); }
bool take_changed() { return g_dirty.exchange(false); }
void set_hold_dt(double s) { g_hold_dt = s; LOG_I("sync", "held frames report dt %.6f s", s); }

void on_present() {
    if (!g_installed) return;
    static bool was = false;
    static ULONGLONG last_toggle = 0;
    const bool now = key(VK_F4) && key(VK_SHIFT);
    if (now && !was && GetTickCount64() - last_toggle > 700) {
        last_toggle = GetTickCount64();
        set_enabled(!g_enabled.load());
        g_dirty = true;
    }
    was = now;

    static ULONGLONG last_report = GetTickCount64();
    if (GetTickCount64() - last_report >= 10000) {
        last_report = GetTickCount64();
        const uint64_t f = g_frames.exchange(0), z = g_frozen.exchange(0), h = g_ticks_held.exchange(0);
        static int32_t last_tick = 0;
        const int32_t tick = g_tick_now.load();
        LOG_I("sync", "eye sync: %llu of %llu game frames held still (%llu held back a tick); simulation +%d ticks",
              (unsigned long long)z, (unsigned long long)f, (unsigned long long)h, tick - last_tick);
        last_tick = tick;
    }
}

}  // namespace mecvr::time_sync

namespace mecvr::time_sync {
bool game_paused() { return g_paused.load(); }
}  // namespace mecvr::time_sync
