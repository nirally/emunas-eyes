#pragma once
// Touch controllers -> the game's Xbox pad layout (D-026). Pure function, unit-tested.
#include <cmath>
#include <cstdint>

namespace mecvr::pad_map {

// XInput button bits (same values as XINPUT_GAMEPAD_*).
constexpr uint16_t kStart = 0x0010, kBack = 0x0020, kLeftThumb = 0x0040, kRightThumb = 0x0080, kLB = 0x0100,
                   kRB = 0x0200, kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;

struct Hand {          // one physical controller
    float x = 0, y = 0; // thumbstick
    bool stick_click = false;
    float trigger = 0, grip = 0;
    bool primary = false;    // A (right) / X (left)
    bool secondary = false;  // B (right) / Y (left)
    bool menu = false;
};

struct Pad {
    uint16_t buttons = 0;
    uint8_t left_trigger = 0, right_trigger = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

inline int16_t axis(float v) {
    v = v < -1.f ? -1.f : v > 1.f ? 1.f : v;
    return int16_t(std::lround(v * 32767.f));
}
inline uint8_t trig(float v) { return uint8_t(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 255.f)); }

// Catalyst's default pad layout: LB Up actions, LT Down actions, RB Quick turn, RT Shift, A Interact,
// B Disruptor, X Light attack, Y Heavy attack, R3 Runner's Vision, Back Map, Start Pause (Map/Pause from
// the move-hand stick click via TapHold).
// The "move" hand is the left one (right when left_handed); the other hand turns. Right stick Y only
// with stick_pitch (head and mouse do pitch in VR).
inline Pad map(const Hand& left, const Hand& right, bool left_handed, bool stick_pitch) {
    const Hand& mv = left_handed ? right : left;
    const Hand& tn = left_handed ? left : right;
    Pad p;
    p.lx = axis(mv.x);
    p.ly = axis(mv.y);
    p.rx = axis(tn.x);
    p.ry = stick_pitch ? axis(tn.y) : 0;
    p.left_trigger = trig(mv.trigger);   // Down actions
    p.right_trigger = trig(tn.trigger);  // Shift
    if (mv.grip > 0.5f) p.buttons |= kLB;      // Up actions
    if (tn.grip > 0.5f) p.buttons |= kRB;      // Quick turn
    if (tn.primary) p.buttons |= kA;           // Interact
    if (tn.secondary) p.buttons |= kB;         // Disruptor
    if (mv.primary) p.buttons |= kX;           // Light attack
    if (mv.secondary) p.buttons |= kY;         // Heavy attack
    // Turn-hand stick click: tap = Runner's Vision, hold = recentre (TapHold in xr_input, PLAYTEST-4f).
    // Move-hand stick click: tap = Pause, hold = Map (TapHold below sets these; SteamVR keeps the
    // Touch menu button for its dashboard, PLAYTEST-3a). The menu button stays mapped for runtimes
    // that pass it through.
    if (left.menu || right.menu) p.buttons |= kStart;
    return p;
}

// One button, two actions: a short press sends `tap` for a moment after release; holding longer than
// hold_s sends `hold` while still held. Times in seconds.
struct TapHold {
    double hold_s = 0.5, pulse_s = 0.15;
    bool was = false, holding = false;
    double down_at = 0, pulse_until = -1;
    // Returns the buttons to add for this frame.
    uint16_t update(bool pressed, double now, uint16_t tap, uint16_t hold) {
        uint16_t out = 0;
        if (pressed && !was) { down_at = now; holding = false; }
        if (pressed && now - down_at >= hold_s) holding = true;
        if (!pressed && was && !holding) pulse_until = now + pulse_s;
        if (!pressed) holding = false;
        was = pressed;
        if (holding) out |= hold;
        if (now < pulse_until) out |= tap;
        return out;
    }
    // Forget the press: no tap on release, no hold.
    void cancel() { was = holding = false; pulse_until = -1; }
};

// Both stick clicks together (PLAYTEST-9a: SteamVR keeps the Touch menu button, so this opens the settings menu).
// `on` from the moment both are down until both are up: the stick clicks' own TapHolds are cancelled meanwhile.
// update() is true once both have been held hold_s.
struct StickChord {
    double hold_s = 1.0;
    bool on = false;
    double since = 0;
    bool update(bool a, bool b, double now) {
        if (a && b && !on) { on = true; since = now; }
        else if (!a && !b) on = false;
        return on && a && b && now - since >= hold_s;
    }
};

// Physical crouch (Stage 4): head height below standing (recentred, metres; negative = lower) holds
// Down actions. Enters below -depth, leaves above -depth + hysteresis.
struct Crouch {
    float depth = 0.25f, hysteresis = 0.08f;
    bool down = false;
    bool update(float head_dy) {
        if (!down && head_dy < -depth) down = true;
        else if (down && head_dy > -depth + hysteresis) down = false;
        return down;
    }
};

}  // namespace mecvr::pad_map
