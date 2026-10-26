// Stage 8B: menus and the map on a big flat screen. See screen_mode.h.
#include "render/screen_mode.h"

#include <atomic>

#include "diagnostics/log.h"
#include "render/time_sync.h"
#include "render/xr_session.h"

namespace mecvr::screen_mode {
namespace {

constexpr int kSwitchFrames = 10;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_active{false};
std::atomic<bool> g_flat{false};
std::atomic<float> g_width{2.6f}, g_distance{2.0f};
int g_run = 0;  // frames the other way (render thread)

}  // namespace

void set_enabled(bool on) { g_enabled = on; }
void set_flat(bool on) { g_flat = on; }
void set_size(float width_m, float distance_m) {
    g_width = width_m < 0.5f ? 0.5f : width_m;
    g_distance = distance_m < 0.5f ? 0.5f : distance_m;
}

void on_present() {
    const bool want = g_enabled.load() && (xr_session::running() || g_flat.load()) && time_sync::game_paused();
    if (want == g_active.load()) {
        g_run = 0;
        return;
    }
    if (++g_run < kSwitchFrames) return;
    g_run = 0;
    g_active = want;
    LOG_I("screen", "%s", want ? "game paused: menus on the big screen (game camera, full-size UI)" : "back to VR");
}

bool active() { return g_active.load(); }
float width() { return g_width.load(); }
float distance() { return g_distance.load(); }

}  // namespace mecvr::screen_mode
