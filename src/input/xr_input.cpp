// Touch controllers -> virtual Xbox pad (D-026). See xr_input.h.
#include <windows.h>

#include "input/xr_input.h"

#include <Xinput.h>

#include <atomic>
#include <cmath>
#include <cstring>

#include "diagnostics/log.h"
#include "core/head_pose.h"
#include "core/pad_map.h"
#include "input/xinput_hook.h"
#include "render/camera_override.h"

namespace mecvr::xr_input {
namespace {

#define XRI_FN(name) PFN_##name p##name = nullptr;
#define XRI_FUNCS(X) \
    X(xrStringToPath) X(xrPathToString) X(xrCreateActionSet) X(xrCreateAction) X(xrSuggestInteractionProfileBindings) \
    X(xrAttachSessionActionSets) X(xrSyncActions) X(xrGetActionStateBoolean) X(xrGetActionStateFloat) \
    X(xrGetActionStateVector2f) X(xrApplyHapticFeedback) X(xrStopHapticFeedback) X(xrGetCurrentInteractionProfile)
XRI_FUNCS(XRI_FN)

XrInstance g_instance = XR_NULL_HANDLE;
XrSession g_session = XR_NULL_HANDLE;
XrActionSet g_set = XR_NULL_HANDLE;
XrPath g_hand[2] = {};  // 0 left, 1 right
enum Act { kStick, kStickClick, kTrigger, kGrip, kPrimary, kSecondary, kMenu, kHaptic, kActCount };
XrAction g_act[kActCount] = {};
bool g_ready = false;
std::atomic<bool> g_left_handed{false}, g_stick_pitch{false};
bool g_pad_on = false;
pad_map::TapHold g_stick_click;  // move-hand stick click: Pause / Map
pad_map::Crouch g_crouch;          // physical crouch (comfort.physical_crouch / crouch_depth)
std::atomic<bool> g_crouch_on{true};
ULONGLONG g_hello_until = 0;     // short buzz on both hands when the virtual pad comes on
// Vibration diagnostics (PLAYTEST-3a: none felt): strongest request and haptic call results per 10 s.
uint16_t g_vib_max[2] = {0, 0};
uint32_t g_haptic_ok = 0, g_haptic_fail = 0;
float g_head_dy_min = 0;  // lowest head height vs standing per 10 s (crouch diagnostics)
float g_run_was = 1.f;    // move-stick size last frame (crouched)
int g_release_frames = 0;

XrPath path(const char* s) {
    XrPath p = XR_NULL_PATH;
    pxrStringToPath(g_instance, s, &p);
    return p;
}

bool make_action(Act a, const char* name, const char* label, XrActionType type) {
    XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
    strcpy_s(ci.actionName, name);
    strcpy_s(ci.localizedActionName, label);
    ci.actionType = type;
    ci.countSubactionPaths = 2;
    ci.subactionPaths = g_hand;
    const XrResult r = pxrCreateAction(g_set, &ci, &g_act[a]);
    if (XR_FAILED(r)) LOG_E("input", "xrCreateAction %s failed (%d)", name, r);
    return XR_SUCCEEDED(r);
}

struct Binding { Act act; const char* path; };

void suggest(const char* profile, const Binding* b, size_t n) {
    XrActionSuggestedBinding sb[24];
    size_t k = 0;
    for (size_t i = 0; i < n && k < 24; ++i) {
        const XrPath p = path(b[i].path);
        if (p != XR_NULL_PATH) sb[k++] = {g_act[b[i].act], p};
    }
    XrInteractionProfileSuggestedBinding s{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    s.interactionProfile = path(profile);
    s.countSuggestedBindings = uint32_t(k);
    s.suggestedBindings = sb;
    const XrResult r = pxrSuggestInteractionProfileBindings(g_instance, &s);
    if (XR_FAILED(r)) LOG_W("input", "bindings for %s not accepted (%d)", profile, r);
}

// --- state reading ---
bool get_bool(Act a, int hand) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = g_act[a];
    gi.subactionPath = g_hand[hand];
    XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_SUCCEEDED(pxrGetActionStateBoolean(g_session, &gi, &s)) && s.isActive && s.currentState;
}
float get_float(Act a, int hand) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = g_act[a];
    gi.subactionPath = g_hand[hand];
    XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};
    return XR_SUCCEEDED(pxrGetActionStateFloat(g_session, &gi, &s)) && s.isActive ? s.currentState : 0.f;
}
XrVector2f get_vec(Act a, int hand, bool* active) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = g_act[a];
    gi.subactionPath = g_hand[hand];
    XrActionStateVector2f s{XR_TYPE_ACTION_STATE_VECTOR2F};
    const bool ok = XR_SUCCEEDED(pxrGetActionStateVector2f(g_session, &gi, &s)) && s.isActive;
    if (active) *active = ok;
    return ok ? s.currentState : XrVector2f{0, 0};
}

void haptic(int hand, float amplitude) {
    static float last[2] = {0, 0};
    if (amplitude <= 0.01f && last[hand] <= 0.01f) return;  // already quiet
    last[hand] = amplitude;
    XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
    hi.action = g_act[kHaptic];
    hi.subactionPath = g_hand[hand];
    if (amplitude <= 0.01f) {
        pxrStopHapticFeedback(g_session, &hi);
        return;
    }
    XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
    v.amplitude = amplitude;
    v.duration = 60'000'000;  // 60 ms, refreshed every frame while the game keeps rumbling
    v.frequency = XR_FREQUENCY_UNSPECIFIED;
    if (XR_SUCCEEDED(pxrApplyHapticFeedback(g_session, &hi, reinterpret_cast<const XrHapticBaseHeader*>(&v)))) ++g_haptic_ok;
    else ++g_haptic_fail;
}

void log_profile() {
    for (int h = 0; h < 2; ++h) {
        XrInteractionProfileState st{XR_TYPE_INTERACTION_PROFILE_STATE};
        if (XR_FAILED(pxrGetCurrentInteractionProfile(g_session, g_hand[h], &st)) || st.interactionProfile == XR_NULL_PATH)
            continue;
        char buf[XR_MAX_PATH_LENGTH];
        uint32_t n = 0;
        if (XR_SUCCEEDED(pxrPathToString(g_instance, st.interactionProfile, sizeof(buf), &n, buf)))
            LOG_I("input", "%s hand: %s", h ? "right" : "left", buf);
    }
}

}  // namespace

bool create(XrInstance instance, XrSession session, PFN_xrGetInstanceProcAddr gipa) {
    g_instance = instance;
    g_session = session;
#define XRI_RESOLVE(name) \
    if (XR_FAILED(gipa(instance, #name, reinterpret_cast<PFN_xrVoidFunction*>(&p##name))) || !p##name) { \
        LOG_E("input", "missing function %s", #name); return false; }
    XRI_FUNCS(XRI_RESOLVE)
#undef XRI_RESOLVE
    g_hand[0] = path("/user/hand/left");
    g_hand[1] = path("/user/hand/right");

    XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy_s(si.actionSetName, "gameplay");
    strcpy_s(si.localizedActionSetName, "Gameplay");
    if (XR_FAILED(pxrCreateActionSet(instance, &si, &g_set))) { LOG_E("input", "xrCreateActionSet failed"); return false; }
    bool ok = make_action(kStick, "stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT) &&
              make_action(kStickClick, "stick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT) &&
              make_action(kTrigger, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT) &&
              make_action(kGrip, "grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT) &&
              make_action(kPrimary, "primary", "A / X button", XR_ACTION_TYPE_BOOLEAN_INPUT) &&
              make_action(kSecondary, "secondary", "B / Y button", XR_ACTION_TYPE_BOOLEAN_INPUT) &&
              make_action(kMenu, "menu", "Menu button", XR_ACTION_TYPE_BOOLEAN_INPUT) &&
              make_action(kHaptic, "rumble", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT);
    if (!ok) return false;

    const Binding touch[] = {
        {kStick, "/user/hand/left/input/thumbstick"},         {kStick, "/user/hand/right/input/thumbstick"},
        {kStickClick, "/user/hand/left/input/thumbstick/click"}, {kStickClick, "/user/hand/right/input/thumbstick/click"},
        {kTrigger, "/user/hand/left/input/trigger/value"},   {kTrigger, "/user/hand/right/input/trigger/value"},
        {kGrip, "/user/hand/left/input/squeeze/value"},      {kGrip, "/user/hand/right/input/squeeze/value"},
        {kPrimary, "/user/hand/left/input/x/click"},         {kPrimary, "/user/hand/right/input/a/click"},
        {kSecondary, "/user/hand/left/input/y/click"},       {kSecondary, "/user/hand/right/input/b/click"},
        {kMenu, "/user/hand/left/input/menu/click"},
        {kHaptic, "/user/hand/left/output/haptic"},          {kHaptic, "/user/hand/right/output/haptic"},
    };
    suggest("/interaction_profiles/oculus/touch_controller", touch, sizeof(touch) / sizeof(touch[0]));
    const Binding index[] = {
        {kStick, "/user/hand/left/input/thumbstick"},         {kStick, "/user/hand/right/input/thumbstick"},
        {kStickClick, "/user/hand/left/input/thumbstick/click"}, {kStickClick, "/user/hand/right/input/thumbstick/click"},
        {kTrigger, "/user/hand/left/input/trigger/value"},   {kTrigger, "/user/hand/right/input/trigger/value"},
        {kGrip, "/user/hand/left/input/squeeze/value"},      {kGrip, "/user/hand/right/input/squeeze/value"},
        {kPrimary, "/user/hand/left/input/a/click"},         {kPrimary, "/user/hand/right/input/a/click"},
        {kSecondary, "/user/hand/left/input/b/click"},       {kSecondary, "/user/hand/right/input/b/click"},
        {kMenu, "/user/hand/left/input/system/click"},
        {kHaptic, "/user/hand/left/output/haptic"},          {kHaptic, "/user/hand/right/output/haptic"},
    };
    suggest("/interaction_profiles/valve/index_controller", index, sizeof(index) / sizeof(index[0]));
    const Binding simple[] = {
        {kTrigger, "/user/hand/left/input/select/click"}, {kTrigger, "/user/hand/right/input/select/click"},
        {kMenu, "/user/hand/left/input/menu/click"},
        {kHaptic, "/user/hand/left/output/haptic"},       {kHaptic, "/user/hand/right/output/haptic"},
    };
    suggest("/interaction_profiles/khr/simple_controller", simple, sizeof(simple) / sizeof(simple[0]));

    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    ai.countActionSets = 1;
    ai.actionSets = &g_set;
    if (XR_FAILED(pxrAttachSessionActionSets(session, &ai))) { LOG_E("input", "xrAttachSessionActionSets failed"); return false; }
    g_ready = true;
    LOG_I("input", "controller actions ready (%s-handed, stick pitch %s)", g_left_handed.load() ? "left" : "right",
          g_stick_pitch.load() ? "on" : "off");
    return true;
}

void sync(XrTime /*time*/, bool focused) {
    if (!g_ready) return;
    bool active = false;
    if (focused) {
        XrActiveActionSet as{g_set, XR_NULL_PATH};
        XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
        si.countActiveActionSets = 1;
        si.activeActionSets = &as;
        if (XR_SUCCEEDED(pxrSyncActions(g_session, &si))) {
            // Physical hands; for a left-handed player the roles swap ("move" hand = right).
            const int move = g_left_handed.load() ? 1 : 0, turn = 1 - move;
            pad_map::Hand hand[2];
            for (int h = 0; h < 2; ++h) {
                bool on = false;
                const XrVector2f s = get_vec(kStick, h, &on);
                active = active || on;
                hand[h].x = s.x;
                hand[h].y = s.y;
                hand[h].stick_click = get_bool(kStickClick, h);
                hand[h].trigger = get_float(kTrigger, h);
                hand[h].grip = get_float(kGrip, h);
                hand[h].primary = get_bool(kPrimary, h);
                hand[h].secondary = get_bool(kSecondary, h);
                hand[h].menu = get_bool(kMenu, h);
            }
            if (active) {
                pad_map::Pad pad = pad_map::map(hand[0], hand[1], g_left_handed.load(), g_stick_pitch.load());
                const float head_dy = head_pose::current().relative.position.y;
                if (head_dy < g_head_dy_min) g_head_dy_min = head_dy;
                const bool was_down = g_crouch.down;
                if (g_crouch_on.load() && g_crouch.update(head_dy)) {
                    // Down actions (slide). The game slides on a fresh press while running, so starting to run
                    // while already crouched releases it for two frames first (PLAYTEST-4b).
                    const float run = std::sqrt(hand[move].x * hand[move].x + hand[move].y * hand[move].y);
                    if (g_run_was < 0.5f && run >= 0.7f) g_release_frames = 2;
                    g_run_was = run;
                    if (g_release_frames > 0) --g_release_frames;
                    else pad.left_trigger = 255;
                } else {
                    g_run_was = 1.f;  // no re-press until crouched again and starting from slow
                }
                if (g_crouch.down != was_down)
                    LOG_I("input", "physical crouch %s (head %.2f m from standing)", g_crouch.down ? "DOWN" : "up", head_dy);
                if (camera_override::turning_active()) {  // the mod turns the view; the game follows (D-028)
                    camera_override::set_turn_input(hand[turn].x);
                    pad.rx = 0;
                } else {
                    camera_override::set_turn_input(0);
                }
                pad.buttons |= g_stick_click.update(hand[move].stick_click, GetTickCount64() / 1000.0, pad_map::kStart,
                                                    pad_map::kBack);  // tap: pause, hold: map
                xinput_hook::set_pad(pad);

                // The game's rumble: low-frequency motor -> move hand, high-frequency -> turn hand.
                uint16_t low = 0, high = 0;
                xinput_hook::last_vibration(low, high);
                if (low > g_vib_max[0]) g_vib_max[0] = low;
                if (high > g_vib_max[1]) g_vib_max[1] = high;
                // The game asks for at most half strength (32767); Quest motors need more to be felt.
                auto strength = [](uint16_t v) { return v ? std::fmin(1.f, 0.35f + 2.f * v / 65535.f) : 0.f; };
                if (GetTickCount64() < g_hello_until) {  // greeting buzz when the controllers connect
                    haptic(move, 0.8f);
                    haptic(turn, 0.8f);
                } else {
                    haptic(move, strength(low));
                    haptic(turn, strength(high));
                }
            }
        }
    }
    static ULONGLONG last_report = GetTickCount64();
    if (GetTickCount64() - last_report >= 10000) {
        last_report = GetTickCount64();
        if (g_pad_on)
            LOG_I("input", "vibration requested by the game: max low %u high %u; haptic calls ok %u failed %u; lowest head %.2f m",
                  g_vib_max[0], g_vib_max[1], g_haptic_ok, g_haptic_fail, g_head_dy_min);
        g_head_dy_min = 0;
        g_vib_max[0] = g_vib_max[1] = 0;
        g_haptic_ok = g_haptic_fail = 0;
    }
    if (active != g_pad_on) {
        g_pad_on = active;
        xinput_hook::set_pad(xinput_hook::Pad{});
        xinput_hook::set_virtual(active);
        if (active) {
            log_profile();
            g_hello_until = GetTickCount64() + 200;
        }
    }
}

void set_left_handed(bool on) { g_left_handed = on; }
void set_stick_pitch(bool on) { g_stick_pitch = on; }
void set_physical_crouch(bool on, float depth_m) {
    g_crouch_on = on;
    g_crouch.depth = depth_m;
}

}  // namespace mecvr::xr_input
