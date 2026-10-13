// In-game bug trigger (F11 for now; a controller binding replaces it in Stage 3).
// Snapshots the moment: time, camera/head state, active settings, log tail.
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "core/head_pose.h"
#include "diagnostics/log.h"
#include "render/camera_override.h"
#include "render/xr_session.h"

namespace mecvr::bug_report {

void poll(const std::filesystem::path& mod_dir, const std::string& settings_json) {
    // Polled every 500 ms: also honour the "pressed since last call" bit so quick taps count.
    static bool was = false;
    SHORT st = GetAsyncKeyState(VK_F11);
    bool down = (st & 0x8000) != 0;
    bool pressed = (down && !was) || (st & 1);
    was = down;
    if (!pressed) return;

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf_s(name, L"report-%04d%02d%02d-%02d%02d%02d.txt", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    auto dir = mod_dir / L"reports";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    FILE* f = nullptr;
    if (_wfopen_s(&f, (dir / name).c_str(), L"w") != 0 || !f) return;

    head_pose::Sample s = head_pose::current();
    fprintf(f, "MEC VR bug report %04d-%02d-%02d %02d:%02d:%02d\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    fprintf(f, "headset session running: %s\n", xr_session::running() ? "yes" : "no (simulator)");
    fprintf(f, "head relative: rot (%.3f %.3f %.3f %.3f) pos (%.3f %.3f %.3f)\n", s.relative.rotation.x,
            s.relative.rotation.y, s.relative.rotation.z, s.relative.rotation.w, s.relative.position.x,
            s.relative.position.y, s.relative.position.z);
    fprintf(f, "roll assertion failures: %llu\n", (unsigned long long)camera_override::assertion_failures());
    fprintf(f, "\nsettings: %s\n\n--- last 300 log lines ---\n", settings_json.c_str());
    for (auto& line : log::tail(300)) fprintf(f, "%s\n", line.c_str());
    fclose(f);
    LOG_W("report", "BUG REPORT saved: reports\\%ls", name);
    MessageBeep(MB_OK);
}

}  // namespace mecvr::bug_report
