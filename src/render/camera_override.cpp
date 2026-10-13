// Stage 1 camera ownership (D-010). The gameplay camera manager calls
// SetCameraData(renderCam, data) once per frame with this frame's camera. We
// replace the transforms in a private copy of `data` - the game's own state is
// never modified, so nothing in game logic feeds back on the head pose.
#include "render/camera_override.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#include <MinHook.h>

#include "core/head_pose.h"
#include "core/sigscan.h"
#include "core/vmath.h"
#include "diagnostics/log.h"

namespace mecvr::camera_override {
namespace {

using SetCameraDataFn = void(__fastcall*)(void* cam, const void* data);

// SetCameraData: compares data rows 0x00-0x30 and 0x80 against the camera, copies 0x90 bytes.
constexpr const char* kSetterSig =
    "0F 28 52 30 0F 28 42 20 0F 28 4A 10 0F C2 41 20 00 0F C2 51 30 00 0F C2 49 10 00 0F 54 D0";
// Gameplay camera manager: stores the last data row, calls the setter, then loads [rbx+20] (FOV).
constexpr const char* kSiteSig = "0F 29 4A 70 0F 29 82 80 00 00 00 E8 ?? ?? ?? ?? F3 0F 10 4B 20 48 89 F1 E8";
constexpr size_t kSiteCallOffset = 11;  // E8 within the site signature
constexpr size_t kDataBytes = 0x90;

SetCameraDataFn g_orig = nullptr;
void* g_setter = nullptr;
uintptr_t g_site_return = 0;  // return address of the gameplay call
uintptr_t g_exe = 0;

std::atomic<bool> g_enabled{true};
std::atomic<PitchSource> g_pitch_source{PitchSource::Auto};

// Diagnostics: who calls the setter (other cameras: menu, cutscenes...).
struct Caller { std::atomic<uintptr_t> ra{0}; std::atomic<uint64_t> calls{0}; uint64_t reported = 0; };
Caller g_callers[32];

// For the roll assertion.
std::atomic<float> g_last_head_roll{0};
std::atomic<ULONGLONG> g_last_override_tick{0};
std::atomic<float> g_last_game_roll{0};
std::atomic<uint64_t> g_assert_failures{0};

void note_caller(uintptr_t ra) {
    for (auto& c : g_callers) {
        uintptr_t cur = c.ra.load();
        if (cur == ra) { ++c.calls; return; }
        if (cur == 0) {
            uintptr_t expected = 0;
            if (c.ra.compare_exchange_strong(expected, ra) || expected == ra) { ++c.calls; return; }
        }
    }
}

// Recent (rendered rotation -> absolute head pose) pairs, so the render thread can
// tell which head pose the frame it is presenting was drawn with.
struct Used { vm::Quat rot; head_pose::Pose abs; };
Used g_used[16];
unsigned g_used_next = 0;
std::mutex g_used_mutex;

void remember(vm::Quat rot, const head_pose::Pose& abs) {
    std::lock_guard lock(g_used_mutex);
    g_used[g_used_next++ % 16] = {rot, abs};
}

void match_rendered(const vm::LinearTransform& view) {
    vm::Vec3 r = view.r(), b = view.b();
    float best = -1;
    head_pose::Pose found;
    std::lock_guard lock(g_used_mutex);
    for (auto& u : g_used) {
        if (!u.abs.valid) continue;
        float score = u.rot.rotate({1, 0, 0}).dot(r) + u.rot.rotate({0, 0, 1}).dot(b);
        if (score > best) { best = score; found = u.abs; }
    }
    if (best > 1.999f) head_pose::set_rendered(found);
}

void apply(uint8_t* data) {
    auto* a = reinterpret_cast<vm::LinearTransform*>(data);
    auto* b = reinterpret_cast<vm::LinearTransform*>(data + 0x40);

    const float game_yaw = vm::yaw_of(*a);
    const float game_pitch = vm::pitch_of(*a);
    g_last_game_roll = vm::roll_of(*a);

    head_pose::Sample s = head_pose::current();
    const head_pose::Pose& head = s.relative;

    // Body frame: game heading only. Pitch from the game only when no headset drives it.
    PitchSource ps = g_pitch_source.load();
    bool game_pitch_on = ps == PitchSource::Game || (ps == PitchSource::Auto && !s.headset);
    vm::Quat body = vm::yaw_quat(game_yaw);
    vm::Quat view = game_pitch_on ? body * vm::Quat::axis_angle({1, 0, 0}, game_pitch) : body;

    vm::Quat final_rot = (view * head.rotation).normalized();
    vm::Vec3 final_pos = a->t() + body.rotate(head.position);

    a->set(final_rot, final_pos);
    b->set(final_rot, final_pos);

    // Expected roll of the result = roll the head contributed.
    vm::LinearTransform expect{};
    expect.set(final_rot, final_pos);
    g_last_head_roll = vm::roll_of(expect);
    g_last_override_tick = GetTickCount64();
    remember(final_rot, s.absolute);
}

void __fastcall setter_detour(void* cam, const void* data) {
    auto ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
    note_caller(ra);
    if (ra != g_site_return || !g_enabled.load() || !data) {
        g_orig(cam, data);
        return;
    }
    alignas(16) uint8_t copy[kDataBytes];
    memcpy(copy, data, kDataBytes);
    apply(copy);
    g_orig(cam, copy);
}

void report_callers() {
    for (auto& c : g_callers) {
        uintptr_t ra = c.ra.load();
        if (!ra) break;
        uint64_t n = c.calls.load();
        if (n != c.reported) {
            LOG_I("camera", "SetCameraData caller exe+%llx: %llu calls (+%llu)%s", (unsigned long long)(ra - g_exe),
                  (unsigned long long)n, (unsigned long long)(n - c.reported),
                  ra == g_site_return ? "  [gameplay camera - overridden]" : "");
            c.reported = n;
        }
    }
}

}  // namespace

bool install() {
    g_exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto setter_pat = sigscan::parse(kSetterSig);
    auto site_pat = sigscan::parse(kSiteSig);
    const uint8_t* setter = sigscan::find_in_module(reinterpret_cast<void*>(g_exe), *setter_pat);
    const uint8_t* site = sigscan::find_in_module(reinterpret_cast<void*>(g_exe), *site_pat);
    if (!setter || !site) {
        LOG_E("camera", "signature not found (setter %p, site %p) - camera override unavailable", setter, site);
        return false;
    }
    const uint8_t* call = site + kSiteCallOffset;
    const uint8_t* call_target = sigscan::rip_relative(call, 1, 5);
    if (call_target != setter) {
        LOG_E("camera", "call site does not call the setter (%p vs %p) - camera override unavailable", call_target, setter);
        return false;
    }
    g_site_return = reinterpret_cast<uintptr_t>(call + 5);
    g_setter = const_cast<uint8_t*>(setter);

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return false;
    st = MH_CreateHook(g_setter, reinterpret_cast<void*>(&setter_detour), reinterpret_cast<void**>(&g_orig));
    if (st == MH_OK) st = MH_EnableHook(g_setter);
    if (st != MH_OK) {
        LOG_E("camera", "hooking SetCameraData failed: %s", MH_StatusToString(st));
        return false;
    }
    LOG_I("camera", "SetCameraData hooked at exe+%llx; gameplay call returns to exe+%llx",
          (unsigned long long)(reinterpret_cast<uintptr_t>(setter) - g_exe), (unsigned long long)(g_site_return - g_exe));
    return true;
}

void set_enabled(bool on) {
    if (g_enabled.exchange(on) != on) LOG_I("camera", "camera override %s", on ? "ON" : "OFF");
}

void set_pitch_source(PitchSource s) { g_pitch_source = s; }

void on_present(float dt) {
    static bool num0_was = false;
    bool num0 = (GetAsyncKeyState(VK_NUMPAD0) & 0x8000) != 0;
    if (num0 && !num0_was) set_enabled(!g_enabled.load());
    num0_was = num0;

    head_pose::sim_update(dt);

    static ULONGLONG last_report = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last_report > 10000) {
        last_report = now;
        report_callers();
        float gr = g_last_game_roll.load() * 57.29578f;
        if (std::fabs(gr) > 1.f) LOG_I("camera", "game wanted %.1f deg of roll this frame; removed", gr);
    }
}

void check_view(const float* v) {
    // Debug readout of the rendered view, every 2 s.
    static ULONGLONG last_dbg = 0;
    if (GetTickCount64() - last_dbg > 2000) {
        last_dbg = GetTickCount64();
        vm::LinearTransform d{};
        for (int k = 0; k < 3; ++k) { d.right[k] = v[k]; d.up[k] = v[4 + k]; d.back[k] = v[8 + k]; }
        LOG_D("camera", "rendered view: yaw %.1f pitch %.1f roll %.1f deg (head roll %.1f, override %s)",
              vm::yaw_of(d) * 57.29578f, vm::pitch_of(d) * 57.29578f, vm::roll_of(d) * 57.29578f,
              g_last_head_roll.load() * 57.29578f, g_enabled.load() ? "on" : "off");
    }
    if (!g_enabled.load() || GetTickCount64() - g_last_override_tick.load() > 100) return;
    vm::LinearTransform m{};
    // View rows are the camera basis rows (D-009).
    for (int k = 0; k < 3; ++k) { m.right[k] = v[k]; m.up[k] = v[4 + k]; m.back[k] = v[8 + k]; }
    match_rendered(m);

    // Only judged while the head itself is level: then any roll in the view came from the game.
    float roll = vm::roll_of(m), expected = g_last_head_roll.load();
    if (std::fabs(expected) > 0.1f / 57.29578f) return;
    if (std::fabs(roll) > 0.5f / 57.29578f) {
        uint64_t n = ++g_assert_failures;
        if (n == 1 || n % 600 == 0)
            LOG_E("assert", "SEV-1 view roll %.2f deg but head roll %.2f deg (failures: %llu)", roll * 57.29578f,
                  expected * 57.29578f, (unsigned long long)n);
    }
}

uint64_t assertion_failures() { return g_assert_failures.load(); }

}  // namespace mecvr::camera_override
