// Entry point. DllMain only sets up forwarding and starts a worker thread;
// everything else happens on that thread, outside the loader lock.
#include <windows.h>

#include <MinHook.h>

#include <filesystem>
#include <string>

#include "core/config.h"
#include "core/game_version.h"
#include "core/proxy.h"
#include "core/window_title.h"
#include "diagnostics/log.h"
#include "render/present_hook.h"

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

void set_hooks_enabled(bool on) {
    MH_STATUS st = on ? MH_EnableHook(MH_ALL_HOOKS) : MH_DisableHook(MH_ALL_HOOKS);
    LOG_I("core", "hooks %s (%s)", on ? "enabled" : "disabled", MH_StatusToString(st));
}

DWORD WINAPI main_thread(LPVOID) {
    const fs::path dir = mod_dir();
    log::init(dir / L"mecvr.log");
    LOG_I("core", "MEC VR %s starting, loaded as %s, pid %lu", MECVR_VERSION, role_name(g_role),
          GetCurrentProcessId());

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
        if (hooks_ok && !g_config.get_bool("features.vr_enabled")) set_hooks_enabled(false);
    } else {
        LOG_I("core", "features.present_hook is off - not hooking rendering");
    }

    g_config.on_change([hooks_ok](const config::json& old_v, const config::json& new_v) {
        if (old_v["diagnostics.log_level"] != new_v["diagnostics.log_level"])
            log::set_level(log::level_from_string(new_v["diagnostics.log_level"].get<std::string>(), log::Level::Info));
        if (hooks_ok && old_v["features.vr_enabled"] != new_v["features.vr_enabled"])
            set_hooks_enabled(new_v["features.vr_enabled"].get<bool>());
    });

    tag_window_async(L"[MEC VR " MECVR_VERSION L"]");

    // Smoke line after 10 s of frames. Runtime/controller checks join at Stage 3.
    ULONGLONG first_frame_at = 0;
    bool smoke_done = false;
    int tick = 0;
    for (;;) {
        Sleep(500);
        g_config.poll_reload();
        if (++tick % 20 == 0 && tick <= 240) log_export_calls();  // every 10 s for the first 2 min
        if (!smoke_done) {
            uint64_t frames = present_hook::frame_count();
            if (frames > 0 && first_frame_at == 0) first_frame_at = GetTickCount64();
            if (first_frame_at && GetTickCount64() - first_frame_at >= 10000) {
                LOG_I("smoke", "SMOKE OK mod=%s exe=%s proxy=%s present_hook=%s frames=%llu flags=%zu assertions=0",
                      MECVR_VERSION, exe_ver.c_str(), role_name(g_role), hooks_ok ? "live" : "off",
                      static_cast<unsigned long long>(frames), g_config.values().size());
                smoke_done = true;
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
