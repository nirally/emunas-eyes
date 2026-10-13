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
// Gameplay render-view build (D-012): if (0x319c0e0(...)) SetCameraData(renderView, derived). The derived
// camera keeps yaw/pitch but rebuilds "up" from world up, dropping head roll.
constexpr const char* kViewSiteSig = "84 C0 74 0F 48 8D 95 F0 00 00 00 48 89 D9 E8 ?? ?? ?? ?? 8B 47 28 89 83 BC 0F 00 00";
constexpr size_t kViewSiteCallOffset = 14;
constexpr size_t kDataBytes = 0x90;

SetCameraDataFn g_orig = nullptr;
void* g_setter = nullptr;
uintptr_t g_site_return = 0;  // return address of the gameplay call
uintptr_t g_view_site_return = 0;  // return address of the render-view build call (0 if not found)
std::atomic<float> g_height_offset{0};  // meters added to camera height (comfort.height_offset)
std::atomic<bool> g_game_motion{false};     // comfort.game_camera_motion: add the game's own camera rotation (D-014)
std::atomic<bool> g_motion_dirty{false};
std::atomic<bool> g_stereo{false};          // stereo.mode = alternate: one eye per frame (D-017, H2 step 1)
std::atomic<int> g_frame_eye{0};             // eye the current game frame renders
std::atomic<bool> g_stereo_dirty{false};

// Last result of the main override, for re-applying at the render-view build.
std::mutex g_final_mutex;
vm::Vec3 g_game_fwd;      // the game's own camera forward this frame, before our override
ULONGLONG g_final_tick = 0;
// Render-view build stats (reported every 10 s).
std::atomic<uint64_t> g_vs_from_game{0}, g_vs_from_ours{0}, g_vs_passed{0};
std::atomic<float> g_vs_max_angle{0}, g_vs_max_pos{0};
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
struct Used { vm::Quat rot; vm::Vec3 pos; head_pose::Pose abs; int eye; };
std::atomic<uint64_t> g_eye_matched[4];  // presented frames matched to: mono, left, right, unmatched
Used g_used[16];
unsigned g_used_next = 0;
std::mutex g_used_mutex;

void remember(vm::Quat rot, vm::Vec3 pos, const head_pose::Pose& abs, int eye) {
    std::lock_guard lock(g_used_mutex);
    g_used[g_used_next++ % 16] = {rot, pos, abs, eye};
}

void match_rendered(const vm::LinearTransform& view, vm::Vec3 cam_pos) {
    vm::Vec3 r = view.r(), b = view.b();
    float best = -1;
    head_pose::Pose found;
    int found_eye = -1;
    std::lock_guard lock(g_used_mutex);
    for (auto& u : g_used) {
        if (!u.abs.valid) continue;
        // Rotation agreement (max 2) minus position mismatch: separates the two eyes, which
        // share a rotation but sit an IPD apart.
        float score = u.rot.rotate({1, 0, 0}).dot(r) + u.rot.rotate({0, 0, 1}).dot(b) - (u.pos - cam_pos).length();
        if (score > best) { best = score; found = u.abs; found_eye = u.eye; }
    }
    if (best > 1.99f) head_pose::set_rendered(found, found_eye);
    ++g_eye_matched[best > 1.99f ? found_eye + 1 : 3];
}

// Per-site state: the main site sees the raw camera, the render-view site the blended one.
struct ComposeState {
    float baseline = 0;        // tracked look pitch (game thread only)
    float prev_pitch = 0;
    ULONGLONG prev_tick = 0;
    bool primed = false;
    // Stereo pairs (PLAYTEST-2a): the right-eye frame reuses the left-eye frame's head pose and
    // game camera, so the two eyes of a pair differ only by the eye offset.
    bool pair_valid = false;
    vm::Quat pair_rot;
    vm::Vec3 pair_pos;
    head_pose::Pose pair_abs;
};
ComposeState g_main_state, g_view_state;

float angle_deg(vm::Vec3 a, vm::Vec3 b) {
    return std::acos(std::fmin(1.f, std::fmax(-1.f, a.dot(b)))) * 57.29578f;
}

// Builds our view from a game camera in `data` (transform A at +0x00, B at +0x40) and writes it
// back into both transforms. Used at the main site (raw camera; the only one at the menu) and at
// the render-view site (the game's blended camera, what a flat player sees - D-015).
void compose_head(vm::LinearTransform* a, ComposeState& st, vm::Quat& out_rot, vm::Vec3& out_pos,
                  head_pose::Pose& out_abs);
void finish(vm::LinearTransform* a, vm::LinearTransform* b, int eye_index, vm::Quat final_rot, vm::Vec3 final_pos,
            head_pose::Pose abs);

void compose(uint8_t* data, ComposeState& st) {
    auto* a = reinterpret_cast<vm::LinearTransform*>(data);
    auto* b = reinterpret_cast<vm::LinearTransform*>(data + 0x40);
    g_last_game_roll = vm::roll_of(*a);

    const int eye_index = g_stereo.load() ? g_frame_eye.load() : -1;
    vm::Quat rot;
    vm::Vec3 pos;
    head_pose::Pose abs;
    if (eye_index == 1 && st.pair_valid) {
        rot = st.pair_rot;
        pos = st.pair_pos;
        abs = st.pair_abs;
        st.pair_valid = false;
    } else {
        compose_head(a, st, rot, pos, abs);
        st.pair_valid = eye_index == 0;
        st.pair_rot = rot;
        st.pair_pos = pos;
        st.pair_abs = abs;
    }
    finish(a, b, eye_index, rot, pos, abs);
}

// The head-level view (no eye offset) from the game camera and the current head pose.
void compose_head(vm::LinearTransform* a, ComposeState& st, vm::Quat& out_rot, vm::Vec3& out_pos,
                  head_pose::Pose& out_abs) {
    const float game_yaw = vm::heading_of(*a);  // not yaw_of: survives rolls (PLAYTEST-1c)
    const float game_pitch = vm::pitch_of(*a);

    head_pose::Sample s = head_pose::current();
    const head_pose::Pose& head = s.relative;

    // Body frame: game heading only. Pitch from the game only when no headset drives it.
    PitchSource ps = g_pitch_source.load();
    bool game_pitch_on = ps == PitchSource::Game || (ps == PitchSource::Auto && !s.headset);
    vm::Quat body = vm::yaw_quat(game_yaw);
    vm::Quat view = game_pitch_on ? body * vm::Quat::axis_angle({1, 0, 0}, game_pitch) : body;

    vm::Quat final_rot = (view * head.rotation).normalized();
    if (g_game_motion.load()) {
        // The game camera relative to its heading = look pitch + animation (rolls, tilts, dips).
        // The look pitch is tracked only while the camera is calm, and only gradually (never a
        // step): a step would show up as a snap, e.g. at the top of a swing (PLAYTEST-1e).
        ULONGLONG now = GetTickCount64();
        float dt = st.prev_tick ? (now - st.prev_tick) / 1000.f : 0.f;
        float pitch_rate = dt > 0 ? std::fabs(game_pitch - st.prev_pitch) / dt : 0.f;
        bool calm = std::fabs(vm::roll_of(*a)) < 2.f / 57.29578f && a->u().y > 0.5f && pitch_rate < 0.5f;
        if (!st.primed) { st.baseline = game_pitch; st.primed = true; }
        else if (calm && dt > 0) {
            float step = 0.35f * dt;  // rad/s cap: catches up with mouse pitch, too slow to see
            float d = game_pitch - st.baseline;
            st.baseline += d > step ? step : d < -step ? -step : d;
        }
        st.prev_pitch = game_pitch;
        st.prev_tick = now;
        float baseline = game_pitch_on ? 0.f : st.baseline;  // simulator keeps the game pitch
        vm::Quat game_rel = vm::yaw_quat(-game_yaw) * vm::quat_of(*a);
        vm::Quat motion = vm::Quat::axis_angle({1, 0, 0}, -baseline) * game_rel;
        final_rot = (body * motion * head.rotation).normalized();
    }
    out_rot = final_rot;
    out_pos = a->t() + body.rotate(head.position) + vm::Vec3{0, g_height_offset.load(), 0};
    out_abs = s.absolute;
}

// Moves the head-level view to this frame's eye and writes it into both transforms.
void finish(vm::LinearTransform* a, vm::LinearTransform* b, int eye_index, vm::Quat final_rot, vm::Vec3 final_pos,
            head_pose::Pose abs) {
    // Stereo: move to this frame's eye (offset and rotation in head space, from the runtime).
    vm::Quat head_rot_for_roll = final_rot;
    if (eye_index >= 0) {
        head_pose::Eye e = head_pose::eye(eye_index);
        final_pos = final_pos + final_rot.rotate(e.offset);
        final_rot = (final_rot * e.rotation).normalized();
        abs.position = abs.position + abs.rotation.rotate(e.offset);
        abs.rotation = (abs.rotation * e.rotation).normalized();
    }

    a->set(final_rot, final_pos);
    b->set(final_rot, final_pos);

    // Expected roll of the result = roll the head contributed.
    vm::LinearTransform expect{};
    expect.set(head_rot_for_roll, final_pos);
    g_last_head_roll = vm::roll_of(expect);
    g_last_override_tick = GetTickCount64();
    remember(final_rot, final_pos, abs, eye_index);
}

// Main site: raw camera from the camera manager.
void apply(uint8_t* data) {
    if (g_stereo.load()) g_frame_eye = g_frame_eye.load() ^ 1;
    auto* a = reinterpret_cast<vm::LinearTransform*>(data);
    {
        std::lock_guard lock(g_final_mutex);
        g_game_fwd = (a->b() * -1.f).normalized();
        g_final_tick = GetTickCount64();
    }
    compose(data, g_main_state);
}

// Render-view site (D-012, D-013, D-015): the game's blended camera. Compose from it directly;
// count how far it is from the raw camera (size of the game's blending) for diagnostics.
void reapply_at_view_site(uint8_t* data) {
    auto* a = reinterpret_cast<vm::LinearTransform*>(data);
    vm::Vec3 raw_fwd;
    {
        std::lock_guard lock(g_final_mutex);
        raw_fwd = g_game_fwd;
    }
    float blend = angle_deg(raw_fwd, (a->b() * -1.f).normalized());
    if (blend > g_vs_max_angle.load()) g_vs_max_angle = blend;
    if (blend < 1.f) ++g_vs_from_game; else ++g_vs_from_ours;  // "same as raw" / "blended"
    compose(data, g_view_state);
}

void __fastcall setter_detour(void* cam, const void* data) {
    auto ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
    note_caller(ra);
    if (ra == g_view_site_return && g_view_site_return && g_enabled.load() && data) {
        alignas(16) uint8_t copy[kDataBytes];
        memcpy(copy, data, kDataBytes);
        reapply_at_view_site(copy);
        g_orig(cam, copy);
        return;
    }
    if (ra != g_site_return || !g_enabled.load() || !data) {
        g_orig(cam, data);
        return;
    }
    alignas(16) uint8_t copy[kDataBytes];
    memcpy(copy, data, kDataBytes);
    apply(copy);
    g_orig(cam, copy);
}

bool key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

std::atomic<bool> g_height_dirty{false};

void report_view_site() {
    uint64_t g = g_vs_from_game.exchange(0), o = g_vs_from_ours.exchange(0), p = g_vs_passed.exchange(0);
    if (g || o || p)
        LOG_I("camera", "render-view build: composed %llu frames (%llu equal to the raw camera, %llu blended by the game; max blend %.1f deg)",
              (unsigned long long)(g + o + p), (unsigned long long)g, (unsigned long long)o, g_vs_max_angle.exchange(0));
        g_vs_max_pos = 0;
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
    if (auto vpat = sigscan::parse(kViewSiteSig)) {
        const uint8_t* vs = sigscan::find_in_module(reinterpret_cast<void*>(g_exe), *vpat);
        if (vs && sigscan::rip_relative(vs + kViewSiteCallOffset, 1, 5) == setter) {
            g_view_site_return = reinterpret_cast<uintptr_t>(vs + kViewSiteCallOffset + 5);
            LOG_I("camera", "render-view build site found, returns to exe+%llx", (unsigned long long)(g_view_site_return - g_exe));
        } else {
            LOG_W("camera", "render-view build site not found - head roll may be lost in gameplay");
        }
    }
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
    // Hotkeys. F-keys work on 60% keyboards; numpad keys are alternatives and work
    // with Num Lock either way (Num Lock off turns Numpad0 into Insert).
    static bool toggle_was = false, up_was = false, down_was = false;
    bool toggle = key(VK_F7) || key(VK_NUMPAD0) || key(VK_INSERT);
    if (toggle && !toggle_was) set_enabled(!g_enabled.load());
    toggle_was = toggle;
    static bool stereo_was = false;
    bool stereo_key = key(VK_F4);
    if (stereo_key && !stereo_was) {
        set_stereo(!g_stereo.load());
        g_stereo_dirty = true;
    }
    stereo_was = stereo_key;
    static bool motion_was = false;
    bool motion = key(VK_F5);
    if (motion && !motion_was) {
        set_game_motion(!g_game_motion.load());
        g_motion_dirty = true;
    }
    motion_was = motion;
    bool up = key(VK_F3), dn = key(VK_F2);
    if ((up && !up_was) || (dn && !down_was)) {
        float h = g_height_offset.load() + (up ? 0.02f : -0.02f);
        set_height_offset(h);
        g_height_dirty = true;
        LOG_I("camera", "height offset %+.2f m (F3 up / F2 down)", g_height_offset.load());
    }
    up_was = up;
    down_was = dn;

    head_pose::sim_update(dt);

    static ULONGLONG last_report = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last_report > 10000) {
        last_report = now;
        report_callers();
        report_view_site();
        uint64_t m0 = g_eye_matched[0].exchange(0), m1 = g_eye_matched[1].exchange(0), m2 = g_eye_matched[2].exchange(0),
                 m3 = g_eye_matched[3].exchange(0);
        LOG_I("camera", "presented frames: mono %llu, left %llu, right %llu, unmatched %llu", (unsigned long long)m0,
              (unsigned long long)m1, (unsigned long long)m2, (unsigned long long)m3);
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
        float cam_y = -(v[1] * v[3] + v[5] * v[7] + v[9] * v[11]);
        LOG_D("camera", "rendered view: y %.2f yaw %.1f pitch %.1f roll %.1f deg (head roll %.1f, override %s)", cam_y,
              vm::yaw_of(d) * 57.29578f, vm::pitch_of(d) * 57.29578f, vm::roll_of(d) * 57.29578f,
              g_last_head_roll.load() * 57.29578f, g_enabled.load() ? "on" : "off");
        head_pose::Sample hs = head_pose::current();
        LOG_D("camera", "head offset from recentre: x %+.2f y %+.2f z %+.2f m (z<0 = forward)", hs.relative.position.x,
              hs.relative.position.y, hs.relative.position.z);
    }
    if (!g_enabled.load() || GetTickCount64() - g_last_override_tick.load() > 100) return;
    vm::LinearTransform m{};
    // View rows are the camera basis rows (D-009).
    for (int k = 0; k < 3; ++k) { m.right[k] = v[k]; m.up[k] = v[4 + k]; m.back[k] = v[8 + k]; }
    vm::Vec3 cam{-(v[0] * v[3] + v[4] * v[7] + v[8] * v[11]), -(v[1] * v[3] + v[5] * v[7] + v[9] * v[11]),
                 -(v[2] * v[3] + v[6] * v[7] + v[10] * v[11])};
    match_rendered(m, cam);

    // Only judged while the head itself is level: then any roll in the view came from the game.
    float roll = vm::roll_of(m), expected = g_last_head_roll.load();
    if (std::fabs(expected) > 0.1f / 57.29578f) return;
    if (g_game_motion.load()) return;  // the game's roll is wanted in this mode
    if (std::fabs(roll) > 0.5f / 57.29578f) {
        uint64_t n = ++g_assert_failures;
        if (n == 1 || n % 600 == 0)
            LOG_E("assert", "SEV-1 view roll %.2f deg but head roll %.2f deg (failures: %llu)", roll * 57.29578f,
                  expected * 57.29578f, (unsigned long long)n);
    }
}

uint64_t assertion_failures() { return g_assert_failures.load(); }

void set_height_offset(float meters) {
    if (meters < -0.5f) meters = -0.5f;
    if (meters > 0.8f) meters = 0.8f;
    g_height_offset = meters;
}

float height_offset() { return g_height_offset.load(); }

bool take_height_changed() { return g_height_dirty.exchange(false); }

void set_game_motion(bool on) {
    if (g_game_motion.exchange(on) != on)
        LOG_I("camera", "game camera motion %s (F5)", on ? "ON - rolls, tilts and dips are felt" : "OFF - head only");
}

bool game_motion() { return g_game_motion.load(); }

bool take_motion_changed() { return g_motion_dirty.exchange(false); }

void set_stereo(bool on) {
    if (g_stereo.exchange(on) != on) LOG_I("camera", "stereo %s", on ? "ON (alternate eyes)" : "OFF (mono)");
}

bool stereo() { return g_stereo.load(); }

bool take_stereo_changed() { return g_stereo_dirty.exchange(false); }

}  // namespace mecvr::camera_override
