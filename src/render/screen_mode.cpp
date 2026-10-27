// Stage 8B: menus and the map on a big flat screen. See screen_mode.h.
#include "render/screen_mode.h"

#include <atomic>

#include "diagnostics/log.h"
#include "render/time_sync.h"
#include "render/ui_shrink.h"
#include "render/xr_session.h"

namespace mecvr::screen_mode {
namespace {

constexpr int kSwitchFrames = 10;
constexpr float kCutsceneSeconds = 1.0f;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_cutscenes{false};
bool g_cutscene_now = false;  // the screen is up for a cutscene (render thread, log)
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

void set_cutscenes(bool on) { g_cutscenes = on; }
bool cutscenes() { return g_cutscenes.load(); }

void on_present() {
    const bool vr = xr_session::running() || g_flat.load();
    const bool paused = g_enabled.load() && time_sync::game_paused();
    const float no_hud = ui_shrink::hud_absent_seconds();
    const bool cutscene = g_cutscenes.load() && !paused && no_hud >= kCutsceneSeconds;
    const bool want = vr && (paused || cutscene);
    if (want == g_active.load()) {
        g_run = 0;
        return;
    }
    if (++g_run < kSwitchFrames) return;
    g_run = 0;
    g_active = want;
    g_cutscene_now = want && cutscene;
    LOG_I("screen", "%s", !want ? "back to VR" : g_cutscene_now ? "no HUD for 1 s (cutscene or loading): on the big screen"
                                                                : "game paused: menus on the big screen (game camera, full-size UI)");
}

bool active() { return g_active.load(); }
float width() { return g_width.load(); }
float distance() { return g_distance.load(); }

}  // namespace mecvr::screen_mode
