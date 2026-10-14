// Turning the game by injected mouse movement (Stage 4, D-028). See yaw_drive.h.
#include "input/yaw_drive.h"

#include <windows.h>

#include <cmath>

#include "diagnostics/log.h"
#include "render/camera_override.h"

namespace mecvr::yaw_drive {
namespace {

float wrap(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

bool game_in_front() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

}  // namespace

bool inject(int dx) {
    if (!dx || !game_in_front()) return false;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;  // relative; the game reads it as raw mouse motion
    return SendInput(1, &in, sizeof(in)) == 1;
}

void poll_test(const std::filesystem::path& dir) {
    std::error_code ec;
    if (std::filesystem::remove(dir / L"TURNTEST", ec)) {
        // Full right-stick turn for 0.5 s; the view should turn input.turn_speed/2 (right = negative) and
        // the game follow.
        const float w0 = camera_override::world_heading(), g0 = camera_override::game_heading();
        camera_override::set_turn_input(1.f);
        Sleep(500);
        camera_override::set_turn_input(0.f);
        Sleep(500);
        const float w1 = camera_override::world_heading(), g1 = camera_override::game_heading();
        LOG_I("yaw", "turn test: view turned %+.1f deg, game turned %+.1f deg, game now %+.1f deg off the view",
              wrap(w1 - w0) * 57.29578f, wrap(g1 - g0) * 57.29578f, wrap(g1 - w1) * 57.29578f);
        return;
    }
    if (!std::filesystem::remove(dir / L"MOUSETEST", ec)) return;
    // Several sizes and both directions, so linearity and any acceleration show up.
    const int sizes[] = {50, 200, 800};
    for (int counts : sizes) {
        for (int sign : {1, -1}) {
            const float before = camera_override::game_heading();
            const int steps = 10;
            for (int i = 0; i < steps; ++i) {
                inject(sign * counts / steps);
                Sleep(15);
            }
            Sleep(400);
            const float turned = wrap(camera_override::game_heading() - before);
            LOG_I("yaw", "mouse test: %+d counts turned the game %+.4f rad (%+.6f rad/count)", sign * counts, turned,
                  turned / (sign * counts));
            Sleep(200);
        }
    }
}

}  // namespace mecvr::yaw_drive
