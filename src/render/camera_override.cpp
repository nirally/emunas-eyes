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
#include "core/yaw_model.h"
#include "input/yaw_drive.h"
#include "render/time_sync.h"
#include "diagnostics/log.h"
#include "diagnostics/time_recon.h"

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

// Camera FOV and aspect setters (render camera +0xe4 vertical FOV, +0xf4 aspect; each recomputes the
// horizontal FOV at +0x290). The aspect setter is the call right after the gameplay SetCameraData.
using SetFloatFn = void(__fastcall*)(void* cam, float v);
constexpr const char* kFovSetterSig = "53 48 83 EC 20 0F 2E 89 E4 00 00 00 48 89 CB 74 ?? F3 0F 11 89 E4 00 00 00";
constexpr const char* kAspectSetterSig = "53 48 83 EC 30 0F 29 74 24 20 0F 28 F1 48 89 CB 0F 2E B1 F4 00 00 00";
SetFloatFn g_fov_orig = nullptr, g_aspect_orig = nullptr;
std::atomic<void*> g_render_cam{nullptr};
std::atomic<bool> g_full_fov{true};
std::atomic<bool> g_fov_test{false};
std::atomic<bool> g_fov_dirty{false};
float g_game_fov = 0, g_game_aspect = 0;  // what the game last asked for (game thread)

SetCameraDataFn g_orig = nullptr;
void* g_setter = nullptr;
uintptr_t g_site_return = 0;  // return address of the gameplay call
uintptr_t g_view_site_return = 0;  // return address of the render-view build call (0 if not found)
std::atomic<float> g_height_offset{0};  // meters added to camera height (comfort.height_offset)
std::atomic<bool> g_game_motion{false};     // comfort.game_camera_motion: add the game's own camera rotation (D-014)
std::atomic<bool> g_motion_dirty{false};
std::atomic<bool> g_stereo{false};          // stereo.mode = alternate: one eye per frame (D-017, H2 step 1)
std::atomic<int> g_frame_eye{0};             // eye the current game frame renders
std::atomic<uint64_t> g_main_count{0};        // main-site overrides so far (eye sync checks it advanced)
std::atomic<bool> g_zero_ipd{false};           // diagnostics.zero_ipd: both eyes at the head centre (eye-sync checks)
std::atomic<float> g_ipd_scale{1.f};          // diagnostics.ipd_scale: exaggerate the eye offset (TAA eye-mixing checks)
std::atomic<bool> g_ipd_vertical{false};      // diagnostics.ipd_vertical: eyes above each other (motion-fix y check)
// Mod-owned turning (D-028): the view heading W comes from yaw_model; the game is steered after it.
std::atomic<bool> g_mod_turning{false};    // input.mod_turning (only with a headset); off: the game turns (PLAYTEST-4b)
std::atomic<bool> g_turning_active{false}; // in effect this frame
std::atomic<float> g_turn_x{0};            // turn stick, + = right (render thread -> game thread)
std::atomic<float> g_turn_speed{3.14159f}; // rad/s at full stick (input.turn_speed)
std::atomic<bool> g_snap{false};
std::atomic<float> g_snap_angle{0.785f};
std::atomic<float> g_world_yaw{0};          // W, for both camera sites
std::atomic<bool> g_turning_sim{false};    // diagnostics.mod_turning_sim: also without a headset (tests)
std::atomic<bool> g_body_follows_head{false}; // input.body_follows_head: steer the game to where the head looks
yaw_model::Model g_yaw;
yaw_model::Snap g_snap_state;
uint64_t g_inject_counts = 0, g_game_turns_seen = 0;
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
std::atomic<int> g_render_eye{-1};       // eye of the frame the render thread is drawing now (-1 mono/unknown)
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
    g_render_eye = best > 1.99f ? found_eye : -1;
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
    // Body frame: the mod's heading when it owns turning (D-028), else the game's.
    vm::Quat body = vm::yaw_quat(g_turning_active.load() ? g_world_yaw.load() : game_yaw);
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
    if (eye_index >= 0 && !g_zero_ipd.load()) {
        head_pose::Eye e = head_pose::eye(eye_index);
        e.offset = e.offset * g_ipd_scale.load();
        if (g_ipd_vertical.load()) e.offset = vm::Vec3{0, e.offset.x, 0};
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

// Once per game frame, before composing: advance the turning model and steer the game.
void update_turning(const vm::LinearTransform& game_cam) {
    const head_pose::Sample s = head_pose::current();
    const bool active = g_mod_turning.load() && (s.headset || g_turning_sim.load()) && s.relative.valid;
    g_turning_active = active;
    if (!active) {
        g_yaw.primed = false;  // re-primes without a jump next time
        return;
    }
    static LARGE_INTEGER freq{}, last{};
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    const double t = double(now.QuadPart) / double(freq.QuadPart);
    const float dt = last.QuadPart ? float(double(now.QuadPart - last.QuadPart) / double(freq.QuadPart)) : 0.f;
    last = now;
    const float x = g_turn_x.load();
    float turn = 0;
    if (g_snap.load()) turn = g_snap_state.update(x, g_snap_angle.load());
    else if (std::fabs(x) > 0.15f) turn = -x * g_turn_speed.load() * (dt < 0.1f ? dt : 0.1f);
    const vm::Vec3 f = s.relative.rotation.rotate({0, 0, -1});
    const float head_yaw = std::atan2(-f.x, -f.z);
    const float before = g_yaw.world;
    // Steer the game to the view heading (stick-controlled body), or to where the head looks.
    const float steer_to = g_body_follows_head.load() ? head_yaw : 0.f;
    const int counts = g_yaw.update(t, vm::heading_of(game_cam), steer_to, turn, !time_sync::game_paused());
    if (std::fabs(yaw_model::wrap(g_yaw.world - before - turn)) > 0.01f) ++g_game_turns_seen;
    if (counts && yaw_drive::inject(counts)) g_inject_counts += uint64_t(counts < 0 ? -counts : counts);
    g_world_yaw = g_yaw.world;
}

// Main site: raw camera from the camera manager.
void apply(uint8_t* data) {
    if (g_stereo.load()) g_frame_eye = g_frame_eye.load() ^ 1;
    ++g_main_count;
    auto* a = reinterpret_cast<vm::LinearTransform*>(data);
    update_turning(*a);
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

// tan(half vertical FOV) that covers both eyes' full field of view, symmetric. The horizontal side
// comes from the viewport (16:9), so covering max(tan_x, tan_y) vertically covers both.
// False when the game's own FOV should stand (no headset, setting off, override off).
bool fov_target(float& tan_v) {
    if (!g_full_fov.load() || !g_enabled.load() || !(head_pose::current().headset || g_fov_test.load())) return false;
    float tan_x = 0, tan_y = 0;
    for (int i = 0; i < 2; ++i) {
        head_pose::Eye e = head_pose::eye(i);
        tan_x = std::fmax(tan_x, std::fmax(std::tan(-e.fov[0]), std::tan(e.fov[1])));
        tan_y = std::fmax(tan_y, std::fmax(std::tan(e.fov[2]), std::tan(-e.fov[3])));
    }
    if (tan_x <= 0.1f || tan_y <= 0.1f) return false;
    tan_v = std::fmax(tan_x, tan_y);
    return true;
}

// The engine's mapping from camera FOV to projection is not a plain formula (D-020), so the FOV is
// steered by measurement: the projection actually rendered (view constants) is compared with the
// target and the commanded tan(FOV/2) is scaled by the ratio until they agree.
float g_cmd_tan = 0;                  // commanded tan(camera FOV / 2); 0 = not steering (game thread)
int g_frames_since_change = 0;
int g_off_frames = 0;                 // consecutive frames off target while locked
bool g_locked = false;                // settled: hold the command
float g_learned_tan = 0;              // last settled command, kept across F9 toggles
std::atomic<float> g_meas_tan_v{0};   // tan(half vertical FOV) of the last rendered view

float fov_from_tan(float t) {
    float f = 2.f * std::atan(t);
    return g_game_fov > 3.2f ? f * 57.29578f : f;  // radians here (1.222); degrees if it ever is
}

void __fastcall fov_detour(void* cam, float v) {
    if (cam == g_render_cam.load()) {
        g_game_fov = v;
        if (g_cmd_tan > 0) v = fov_from_tan(g_cmd_tan);
    }
    g_fov_orig(cam, v);
}

void __fastcall aspect_detour(void* cam, float v) {
    if (cam == g_render_cam.load()) g_game_aspect = v;
    g_aspect_orig(cam, v);
}

// Main site, after SetCameraData: steer the render camera to the headset FOV (or back to the game's).
void update_fov(void* cam) {
    if (!g_fov_orig) return;
    auto* c = static_cast<uint8_t*>(cam);
    if (g_render_cam.exchange(cam) != cam || g_game_fov == 0) {
        g_game_fov = *reinterpret_cast<float*>(c + 0xe4);
        g_game_aspect = *reinterpret_cast<float*>(c + 0xf4);
        LOG_I("camera", "render camera: game FOV %.3f, aspect %.4f", g_game_fov, g_game_aspect);
    }
    float target;
    bool on = fov_target(target);
    if (on) {
        if (g_cmd_tan <= 0) {
            // Calm-camera mapping measured in PLAYTEST-2b: rendered vertical FOV = camera FOV. Reuse
            // what was learned last time, so F9 switches instantly.
            g_cmd_tan = g_learned_tan > 0 ? g_learned_tan : target;
            g_locked = g_learned_tan > 0;
            g_frames_since_change = g_off_frames = 0;
            LOG_I("camera", "full field of view: target %.1f deg vertical", 2 * std::atan(target) * 57.2958f);
        } else {
            // Settle once, then hold. The game adds its own FOV kicks (drops, landings); steering against
            // them oscillated (PLAYTEST-2b), so only an error lasting 5 s unlocks the steering again.
            float meas = g_meas_tan_v.load();
            float ratio = meas > 0.05f ? target / meas : 1.f;
            bool off = std::fabs(ratio - 1.f) > (g_locked ? 0.01f : 0.003f);
            if (g_locked) {
                g_off_frames = off ? g_off_frames + 1 : 0;
                if (g_off_frames > 450) {
                    g_locked = false;
                    g_frames_since_change = g_off_frames = 0;
                    LOG_I("camera", "FOV off target for 5 s (rendered %.1f deg) - re-steering", 2 * std::atan(meas) * 57.2958f);
                }
            } else if (++g_frames_since_change >= 6) {
                if (off) {
                    g_cmd_tan = std::fmin(5.7f, std::fmax(0.2f, g_cmd_tan * std::fmin(1.25f, std::fmax(0.8f, ratio))));
                    g_frames_since_change = 0;
                    static int logged = 0;
                    if (logged++ < 20)
                        LOG_D("camera", "FOV steer: rendered %.1f deg, target %.1f -> camera FOV %.3f", 2 * std::atan(meas) * 57.2958f,
                              2 * std::atan(target) * 57.2958f, fov_from_tan(g_cmd_tan));
                } else {
                    g_locked = true;
                    g_learned_tan = g_cmd_tan;
                    LOG_I("camera", "FOV settled: camera FOV %.3f renders %.1f deg", fov_from_tan(g_cmd_tan), 2 * std::atan(meas) * 57.2958f);
                }
            }
        }
        g_fov_orig(cam, fov_from_tan(g_cmd_tan));
    } else if (g_cmd_tan > 0) {
        g_cmd_tan = 0;
        g_fov_orig(cam, g_game_fov);
        LOG_I("camera", "game field of view restored");
    }
}

void __fastcall setter_detour(void* cam, const void* data) {
    auto ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
    note_caller(ra);
    if (ra == g_site_return && g_site_return) time_recon::on_game_frame();
    if (ra == g_view_site_return && g_view_site_return && g_enabled.load() && data) {
        alignas(16) uint8_t copy[kDataBytes];
        memcpy(copy, data, kDataBytes);
        reapply_at_view_site(copy);
        g_orig(cam, copy);
        return;
    }
    if (ra != g_site_return || !g_enabled.load() || !data) {
        g_orig(cam, data);
        if (ra == g_site_return && g_cmd_tan > 0) update_fov(cam);  // override just switched off
        return;
    }
    alignas(16) uint8_t copy[kDataBytes];
    memcpy(copy, data, kDataBytes);
    apply(copy);
    g_orig(cam, copy);
    update_fov(cam);
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

// FOV/aspect setters (optional: without them the picture stays at the game's FOV).
void install_fov_hooks(const uint8_t* site) {
    auto fov_pat = sigscan::parse(kFovSetterSig);
    auto aspect_pat = sigscan::parse(kAspectSetterSig);
    const uint8_t* fov = sigscan::find_in_module(reinterpret_cast<void*>(g_exe), *fov_pat);
    const uint8_t* aspect = sigscan::find_in_module(reinterpret_cast<void*>(g_exe), *aspect_pat);
    const uint8_t* site_aspect = sigscan::rip_relative(site + kSiteCallOffset + 5 + 8, 1, 5);  // movss xmm1,[rbx+20]; mov rcx,rsi; call
    if (!fov || !aspect || aspect != site_aspect) {
        LOG_W("camera", "FOV setters not found (fov %p, aspect %p, site calls %p) - headset FOV unavailable", fov, aspect, site_aspect);
        return;
    }
    MH_STATUS st = MH_CreateHook(const_cast<uint8_t*>(fov), reinterpret_cast<void*>(&fov_detour), reinterpret_cast<void**>(&g_fov_orig));
    if (st == MH_OK) st = MH_EnableHook(const_cast<uint8_t*>(fov));
    if (st == MH_OK)
        st = MH_CreateHook(const_cast<uint8_t*>(aspect), reinterpret_cast<void*>(&aspect_detour), reinterpret_cast<void**>(&g_aspect_orig));
    if (st == MH_OK) st = MH_EnableHook(const_cast<uint8_t*>(aspect));
    if (st != MH_OK) {
        LOG_E("camera", "FOV setter hooks failed: %s", MH_StatusToString(st));
        MH_RemoveHook(const_cast<uint8_t*>(fov));
        MH_RemoveHook(const_cast<uint8_t*>(aspect));
        g_fov_orig = g_aspect_orig = nullptr;
        return;
    }
    LOG_I("camera", "FOV setter exe+%llx, aspect setter exe+%llx hooked", (unsigned long long)(reinterpret_cast<uintptr_t>(fov) - g_exe),
          (unsigned long long)(reinterpret_cast<uintptr_t>(aspect) - g_exe));
}

}  // namespace

bool full_fov() { return g_full_fov.load(); }
bool take_full_fov_changed() { return g_fov_dirty.exchange(false); }
void set_fov_test(bool on) { g_fov_test = on; }

void note_projection(float sy) {
    if (sy > 0.05f) g_meas_tan_v = 1.f / sy;
}

void set_full_fov(bool on) {
    if (g_full_fov.exchange(on) != on) LOG_I("camera", "full field of view setting %s", on ? "on" : "off");
}

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
    install_fov_hooks(site);
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
    bool stereo_key = key(VK_F4) && !key(VK_SHIFT);  // Shift+F4 is eye sync (time_sync)
    static ULONGLONG last_toggle = 0;  // F4/F9: ignore a second press within 0.7 s (key bounce flipped F9 back, PLAYTEST-2b)
    const bool can_toggle = GetTickCount64() - last_toggle > 700;
    if (stereo_key && !stereo_was && can_toggle) {
        last_toggle = GetTickCount64();
        set_stereo(!g_stereo.load());
        g_stereo_dirty = true;
    }
    stereo_was = stereo_key;
    static bool fov_was = false;
    bool fov_key = key(VK_F9) && !key(VK_SHIFT);  // Shift+F9 is per-eye anti-aliasing (taa_eyes)
    if (fov_key && !fov_was && can_toggle) {
        last_toggle = GetTickCount64();
        set_full_fov(!g_full_fov.load());
        g_fov_dirty = true;
    }
    fov_was = fov_key;
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

uint64_t main_site_count() { return g_main_count.load(); }
bool next_frame_is_right_eye() { return g_enabled.load() && g_stereo.load() && g_frame_eye.load() == 0; }
void set_zero_ipd(bool on) { g_zero_ipd = on; }
void set_ipd_scale(float s) { g_ipd_scale = s; }
void set_ipd_vertical(bool on) { g_ipd_vertical = on; }
int rendering_eye() { return g_render_eye.load(); }

}  // namespace mecvr::camera_override

namespace mecvr::camera_override {
float game_heading() {
    std::lock_guard lock(g_final_mutex);
    return std::atan2(-g_game_fwd.x, -g_game_fwd.z);  // 0 = looking down -Z, + = turned left (yaw_of convention)
}
}  // namespace mecvr::camera_override

namespace mecvr::camera_override {
void set_mod_turning(bool on) { g_mod_turning = on; }
void set_turning_sim(bool on) { g_turning_sim = on; }
void set_body_follows_head(bool on) { g_body_follows_head = on; }
float world_heading() { return g_world_yaw.load(); }
void set_turn_input(float x) { g_turn_x = x; }
void set_turn_options(float speed_deg_s, bool snap, float snap_deg) {
    g_turn_speed = speed_deg_s / 57.29578f;
    g_snap = snap;
    g_snap_angle = snap_deg / 57.29578f;
}
bool turning_active() { return g_turning_active.load(); }
void report_turning() {
    if (!g_turning_active.load()) return;
    LOG_I("camera", "turning: view heading %.1f deg, game %.1f deg off it; mouse counts injected %llu; game's own turns %llu frames",
          g_world_yaw.load() * 57.29578f, yaw_model::wrap(game_heading() - g_world_yaw.load()) * 57.29578f,
          (unsigned long long)g_inject_counts, (unsigned long long)g_game_turns_seen);
    g_inject_counts = g_game_turns_seen = 0;
}
}  // namespace mecvr::camera_override
