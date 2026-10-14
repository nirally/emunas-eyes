// Entry point. DllMain only sets up forwarding and starts a worker thread;
// everything else happens on that thread, outside the loader lock.
#include <windows.h>
#include <tlhelp32.h>

#include <MinHook.h>

#include <filesystem>
#include <string>

#include "core/config.h"
#include "core/game_version.h"
#include "core/proxy.h"
#include "core/window_title.h"
#include "diagnostics/log.h"
#include "diagnostics/typeinfo_dump.h"
#include "diagnostics/time_recon.h"
#include "diagnostics/mem_find.h"
#include "render/ctx_hooks.h"

namespace mecvr::recon { void probe_strings(); }
namespace mecvr::code_dump { void poll(const std::filesystem::path& dir); }
namespace mecvr::view_recon { void poll(const std::filesystem::path& dir); }
struct IDXGISwapChain;
namespace mecvr::screenshot { void on_present(IDXGISwapChain* chain, const std::filesystem::path& dir); }
namespace mecvr::bug_report { void poll(const std::filesystem::path& mod_dir, const std::string& settings_json); }
#include "render/camera_override.h"
#include "render/cb_capture.h"
#include "render/present_hook.h"
#include "render/stereo_views.h"
#include "render/time_sync.h"
#include "render/taa_eyes.h"
#include "render/hand_render.h"
#include "core/hand_pose.h"
#include "input/xinput_hook.h"
#include "input/xr_input.h"
#include "input/yaw_drive.h"
#include "render/motion_fix.h"
#include "render/xr_session.h"

namespace fs = std::filesystem;
using namespace mecvr;

namespace {

HMODULE g_self = nullptr;
proxy::Role g_role = proxy::Role::Unknown;
config::Store g_config;

fs::path mod_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_self, path, MAX_PATH);
    return fs::path(path).parent_path() / L"mecvr";
}

const char* role_name(proxy::Role r) {
    switch (r) {
        case proxy::Role::Version: return "version.dll";
        case proxy::Role::DInput8: return "dinput8.dll";
        case proxy::Role::NvCamera: return "NvCameraSDK64.dll";
        case proxy::Role::NvCameraMissingOriginal: return "NvCameraSDK64.dll (ORIGINAL MISSING)";
        default: return "unknown";
    }
}

// Parent pid via the toolhelp process list (0 if not found). Tells launcher stages apart in the log.
DWORD parent_pid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{sizeof(pe)};
    DWORD self = GetCurrentProcessId(), parent = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (pe.th32ProcessID == self) { parent = pe.th32ParentProcessID; break; }
    CloseHandle(snap);
    return parent;
}

// Retries until the game window exists (it appears a few seconds after load).
void tag_window_async(const wchar_t* text) {
    struct Arg { std::wstring text; };
    auto* arg = new Arg{text};
    CreateThread(nullptr, 0,
                 [](LPVOID p) -> DWORD {
                     auto* a = static_cast<Arg*>(p);
                     for (int i = 0; i < 300; ++i) {
                         if (window_title::tag(a->text.c_str())) {
                             LOG_I("core", "window title tagged");
                             break;
                         }
                         Sleep(1000);
                     }
                     delete a;
                     return 0;
                 },
                 arg, 0, nullptr);
}

// Logs which proxied exports the game has called since the last report.
void log_export_calls() {
    static unsigned long long last[proxy::kExportCount] = {};
    for (int i = 0; i < proxy::kExportCount; ++i) {
        unsigned long long n = proxy::call_count(i);
        if (n != last[i]) {
            LOG_I("proxy", "export %s called %llu times (+%llu)", proxy::export_name(i), n, n - last[i]);
            last[i] = n;
        }
    }
}

camera_override::PitchSource pitch_source_from(const std::string& s) {
    if (s == "game") return camera_override::PitchSource::Game;
    if (s == "head") return camera_override::PitchSource::Head;
    return camera_override::PitchSource::Auto;
}

// Render thread, start of every Present.
fs::path g_dir_for_frame;

void on_frame(IDXGISwapChain* chain) {
    xr_session::on_present(chain);
    screenshot::on_present(chain, g_dir_for_frame);
    ctx_hooks::on_present(chain, g_dir_for_frame);
    static LARGE_INTEGER freq{}, last{};
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    float dt = last.QuadPart ? float(now.QuadPart - last.QuadPart) / float(freq.QuadPart) : 0.f;
    last = now;
    camera_override::on_present(dt);
    time_sync::on_present();
    cb_capture::on_present();
}

void set_hooks_enabled(bool on) {
    MH_STATUS st = on ? MH_EnableHook(MH_ALL_HOOKS) : MH_DisableHook(MH_ALL_HOOKS);
    LOG_I("core", "hooks %s (%s)", on ? "enabled" : "disabled", MH_StatusToString(st));
}

DWORD WINAPI main_thread(LPVOID) {
    const fs::path dir = mod_dir();
    {
        std::error_code ec;  // keep the previous session's log (playtest logs were lost on relaunch)
        std::filesystem::rename(dir / L"mecvr.log", dir / L"mecvr.prev.log", ec);
    }
    log::init(dir / L"mecvr.log");
    LOG_I("core", "MEC VR %s starting, loaded as %s, pid %lu", MECVR_VERSION, role_name(g_role),
          GetCurrentProcessId());
    {
        const wchar_t* wcmd = GetCommandLineW();
        std::string cmd(WideCharToMultiByte(CP_UTF8, 0, wcmd, -1, nullptr, 0, nullptr, nullptr), '\0');
        if (!cmd.empty()) WideCharToMultiByte(CP_UTF8, 0, wcmd, -1, cmd.data(), int(cmd.size()), nullptr, nullptr);
        LOG_I("core", "parent pid %lu, command line: %s", parent_pid(), cmd.c_str());
    }

    if (g_role == proxy::Role::NvCameraMissingOriginal)
        LOG_E("core", "mecvr\\NvCameraSDK64_orig.dll not found - photo mode calls are stubbed. Re-run the installer.");

    const std::string exe_ver = game_version::host_exe_version();
    LOG_I("core", "game exe version %s (target %s)", exe_ver.c_str(), game_version::kTarget);

    // Safe mode: step aside completely, the game runs flat and untouched.
    std::string safe_reason;
    if (fs::exists(dir / L"SAFE_MODE")) safe_reason = "SAFE_MODE file present";
    else if (exe_ver != game_version::kTarget) safe_reason = "game version mismatch - hooks were built for " + std::string(game_version::kTarget);
    else if (!g_config.load(dir / L"schema.json", dir / L"config.json")) safe_reason = "schema.json missing or invalid";

    if (!safe_reason.empty()) {
        LOG_W("core", "SAFE MODE: %s. Mod is inactive; game runs normally.", safe_reason.c_str());
        tag_window_async(L"[MEC VR " MECVR_VERSION L" - SAFE MODE]");
        return 0;
    }

    log::set_level(log::level_from_string(g_config.get_string("diagnostics.log_level"), log::Level::Info));

    // Wait for the game's own D3D11 before touching graphics.
    for (int i = 0; i < 600 && !GetModuleHandleW(L"d3d11.dll"); ++i) Sleep(100);

    bool hooks_ok = false;
    if (g_config.get_bool("features.present_hook")) {
        hooks_ok = present_hook::install();
        if (hooks_ok) {
            cb_capture::install(dir, g_config.get_bool("diagnostics.cb_capture"));
            xr_session::configure(dir, g_config.get_bool("features.xr"));
            g_dir_for_frame = dir;
            if (stereo_views::install()) stereo_views::set_enabled(g_config.get_bool("diagnostics.stereo_experiment"));
            // Installed always so the flag can switch it live; the flag decides whether it acts.
            if (camera_override::install()) {
                camera_override::set_enabled(g_config.get_bool("features.camera_override"));
                camera_override::set_pitch_source(pitch_source_from(g_config.get_string("camera.pitch_source")));
                camera_override::set_height_offset(static_cast<float>(g_config.get_number("comfort.height_offset")));
                camera_override::set_game_motion(g_config.get_bool("comfort.game_camera_motion"));
                camera_override::set_stereo(g_config.get_string("stereo.mode") == "alternate");
                camera_override::set_full_fov(g_config.get_bool("rendering.full_fov"));
                camera_override::set_fov_test(g_config.get_bool("diagnostics.fov_test"));
                camera_override::set_zero_ipd(g_config.get_bool("diagnostics.zero_ipd"));
                camera_override::set_ipd_scale(static_cast<float>(g_config.get_number("diagnostics.ipd_scale")));
                camera_override::set_ipd_vertical(g_config.get_bool("diagnostics.ipd_vertical"));
                time_sync::set_enabled(g_config.get_bool("stereo.sync"));
                time_sync::set_hold_dt(g_config.get_number("diagnostics.sync_hold_dt"));
                time_sync::install();
                camera_override::set_mod_turning(g_config.get_bool("input.mod_turning"));
                camera_override::set_roomscale(g_config.get_bool("comfort.roomscale"));
                hand_render::set_enabled(g_config.get_bool("hands.show"));
                hand_pose::set_sim(g_config.get_bool("diagnostics.hand_sim"));
                camera_override::set_room_sim(g_config.get_bool("diagnostics.room_sim"));
                camera_override::set_turning_sim(g_config.get_bool("diagnostics.mod_turning_sim"));
                camera_override::set_body_follows_head(g_config.get_bool("input.body_follows_head"));
                camera_override::set_turn_options(static_cast<float>(g_config.get_number("input.turn_speed")), g_config.get_bool("input.snap_turn"),
                                                  static_cast<float>(g_config.get_number("input.snap_angle")));
                xr_input::set_left_handed(g_config.get_bool("input.left_handed"));
                xr_input::set_stick_pitch(g_config.get_bool("input.stick_pitch"));
                xr_input::set_physical_crouch(g_config.get_bool("comfort.physical_crouch"),
                                              static_cast<float>(g_config.get_number("comfort.crouch_depth")));
                taa_eyes::set_enabled(g_config.get_bool("rendering.taa_per_eye"));
                taa_eyes::set_motion_fix(g_config.get_bool("diagnostics.taa_motion_fix"));
                motion_fix::set_debug_zero(g_config.get_bool("diagnostics.motion_fix_debug"));
                motion_fix::set_jitter_mode(static_cast<float>(g_config.get_number("diagnostics.motion_fix_jitter")));
            }
            present_hook::set_frame_callback(&on_frame);
            if (g_config.get_bool("diagnostics.time_recon")) time_recon::install(dir / L"time_recon.txt");
        }
        if (hooks_ok && !g_config.get_bool("features.vr_enabled")) set_hooks_enabled(false);
    } else {
        LOG_I("core", "features.present_hook is off - not hooking rendering");
    }

    g_config.on_change([hooks_ok](const config::json& old_v, const config::json& new_v) {
        if (old_v["diagnostics.log_level"] != new_v["diagnostics.log_level"])
            log::set_level(log::level_from_string(new_v["diagnostics.log_level"].get<std::string>(), log::Level::Info));
        if (hooks_ok && old_v["features.vr_enabled"] != new_v["features.vr_enabled"])
            set_hooks_enabled(new_v["features.vr_enabled"].get<bool>());
        if (old_v["features.camera_override"] != new_v["features.camera_override"])
            camera_override::set_enabled(new_v["features.camera_override"].get<bool>());
        if (old_v["camera.pitch_source"] != new_v["camera.pitch_source"])
            camera_override::set_pitch_source(pitch_source_from(new_v["camera.pitch_source"].get<std::string>()));
        if (old_v["comfort.height_offset"] != new_v["comfort.height_offset"])
            camera_override::set_height_offset(new_v["comfort.height_offset"].get<float>());
        if (old_v["diagnostics.stereo_experiment"] != new_v["diagnostics.stereo_experiment"])
            stereo_views::set_enabled(new_v["diagnostics.stereo_experiment"].get<bool>());
        if (old_v["stereo.sync"] != new_v["stereo.sync"]) time_sync::set_enabled(new_v["stereo.sync"].get<bool>());
        if (old_v["diagnostics.motion_fix_jitter"] != new_v["diagnostics.motion_fix_jitter"])
            motion_fix::set_jitter_mode(new_v["diagnostics.motion_fix_jitter"].get<float>());
        if (old_v["diagnostics.motion_fix_debug"] != new_v["diagnostics.motion_fix_debug"])
            motion_fix::set_debug_zero(new_v["diagnostics.motion_fix_debug"].get<bool>());
        if (old_v["diagnostics.taa_motion_fix"] != new_v["diagnostics.taa_motion_fix"])
            taa_eyes::set_motion_fix(new_v["diagnostics.taa_motion_fix"].get<bool>());
        if (old_v["input.body_follows_head"] != new_v["input.body_follows_head"])
            camera_override::set_body_follows_head(new_v["input.body_follows_head"].get<bool>());
        if (old_v["diagnostics.mod_turning_sim"] != new_v["diagnostics.mod_turning_sim"])
            camera_override::set_turning_sim(new_v["diagnostics.mod_turning_sim"].get<bool>());
        if (old_v["hands.show"] != new_v["hands.show"]) hand_render::set_enabled(new_v["hands.show"].get<bool>());
        if (old_v["diagnostics.hand_sim"] != new_v["diagnostics.hand_sim"]) hand_pose::set_sim(new_v["diagnostics.hand_sim"].get<bool>());
        if (old_v["comfort.roomscale"] != new_v["comfort.roomscale"])
            camera_override::set_roomscale(new_v["comfort.roomscale"].get<bool>());
        if (old_v["diagnostics.room_sim"] != new_v["diagnostics.room_sim"])
            camera_override::set_room_sim(new_v["diagnostics.room_sim"].get<bool>());
        if (old_v["input.mod_turning"] != new_v["input.mod_turning"])
            camera_override::set_mod_turning(new_v["input.mod_turning"].get<bool>());
        if (old_v["input.turn_speed"] != new_v["input.turn_speed"] || old_v["input.snap_turn"] != new_v["input.snap_turn"] ||
            old_v["input.snap_angle"] != new_v["input.snap_angle"])
            camera_override::set_turn_options(new_v["input.turn_speed"].get<float>(), new_v["input.snap_turn"].get<bool>(),
                                              new_v["input.snap_angle"].get<float>());
        if (old_v["input.left_handed"] != new_v["input.left_handed"])
            xr_input::set_left_handed(new_v["input.left_handed"].get<bool>());
        if (old_v["comfort.physical_crouch"] != new_v["comfort.physical_crouch"] || old_v["comfort.crouch_depth"] != new_v["comfort.crouch_depth"])
            xr_input::set_physical_crouch(new_v["comfort.physical_crouch"].get<bool>(), new_v["comfort.crouch_depth"].get<float>());
        if (old_v["input.stick_pitch"] != new_v["input.stick_pitch"])
            xr_input::set_stick_pitch(new_v["input.stick_pitch"].get<bool>());
        if (old_v["rendering.taa_per_eye"] != new_v["rendering.taa_per_eye"])
            taa_eyes::set_enabled(new_v["rendering.taa_per_eye"].get<bool>());
        if (old_v["diagnostics.sync_hold_dt"] != new_v["diagnostics.sync_hold_dt"])
            time_sync::set_hold_dt(new_v["diagnostics.sync_hold_dt"].get<double>());
        if (old_v["diagnostics.ipd_vertical"] != new_v["diagnostics.ipd_vertical"])
            camera_override::set_ipd_vertical(new_v["diagnostics.ipd_vertical"].get<bool>());
        if (old_v["diagnostics.ipd_scale"] != new_v["diagnostics.ipd_scale"])
            camera_override::set_ipd_scale(new_v["diagnostics.ipd_scale"].get<float>());
        if (old_v["diagnostics.zero_ipd"] != new_v["diagnostics.zero_ipd"])
            camera_override::set_zero_ipd(new_v["diagnostics.zero_ipd"].get<bool>());
        if (old_v["stereo.mode"] != new_v["stereo.mode"])
            camera_override::set_stereo(new_v["stereo.mode"].get<std::string>() == "alternate");
        if (old_v["comfort.game_camera_motion"] != new_v["comfort.game_camera_motion"])
            camera_override::set_game_motion(new_v["comfort.game_camera_motion"].get<bool>());
        if (old_v["rendering.full_fov"] != new_v["rendering.full_fov"])
            camera_override::set_full_fov(new_v["rendering.full_fov"].get<bool>());
        if (old_v["diagnostics.fov_test"] != new_v["diagnostics.fov_test"])
            camera_override::set_fov_test(new_v["diagnostics.fov_test"].get<bool>());
    });

    tag_window_async(L"[MEC VR " MECVR_VERSION L"]");

    // Smoke line after 10 s of frames. Runtime/controller checks join at Stage 3.
    ULONGLONG first_frame_at = 0;
    bool smoke_done = false;
    int tick = 0;
    for (;;) {
        Sleep(500);
        g_config.poll_reload();
        if (++tick % 20 == 0) log_export_calls();  // every 10 s; only logs exports whose count changed
        xinput_hook::poll_install();
        xinput_hook::poll_test(dir);
        yaw_drive::poll_test(dir);
        if (tick % 20 == 0) xinput_hook::report();
        if (tick % 20 == 0) camera_override::report_turning();
        if (tick % 4 == 0) code_dump::poll(dir);
        if (tick % 4 == 0) view_recon::poll(dir);
        if (tick % 4 == 0) mem_find::poll(dir);
        bug_report::poll(dir, g_config.values().dump());
        if (time_sync::take_changed()) g_config.set_user_value("stereo.sync", time_sync::enabled());
        if (taa_eyes::take_changed()) g_config.set_user_value("rendering.taa_per_eye", taa_eyes::enabled());
        if (camera_override::take_stereo_changed())
            g_config.set_user_value("stereo.mode", camera_override::stereo() ? "alternate" : "mono");
        if (camera_override::take_full_fov_changed())
            g_config.set_user_value("rendering.full_fov", camera_override::full_fov());
        if (camera_override::take_motion_changed())
            g_config.set_user_value("comfort.game_camera_motion", camera_override::game_motion());
        if (camera_override::take_height_changed())
            g_config.set_user_value("comfort.height_offset", std::round(camera_override::height_offset() * 100.0) / 100.0);
        if (!smoke_done) {
            uint64_t frames = present_hook::frame_count();
            if (frames > 0 && first_frame_at == 0) first_frame_at = GetTickCount64();
            if (first_frame_at && GetTickCount64() - first_frame_at >= 10000) {
                LOG_I("smoke", "SMOKE OK mod=%s exe=%s proxy=%s present_hook=%s frames=%llu flags=%zu assertions=%llu",
                      MECVR_VERSION, exe_ver.c_str(), role_name(g_role), hooks_ok ? "live" : "off",
                      static_cast<unsigned long long>(frames), g_config.values().size(),
                      static_cast<unsigned long long>(camera_override::assertion_failures()));
                smoke_done = true;
                if (g_config.get_bool("diagnostics.typeinfo_dump")) {
                    static fs::path out = dir / L"typeinfo.txt";
                    if (HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD { recon::probe_strings(); typeinfo_dump::run(out); return 0; }, nullptr, 0, nullptr))
                        CloseHandle(t);
                }
            }
        }
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        g_role = proxy::init(module);
        if (HANDLE t = CreateThread(nullptr, 0, main_thread, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
