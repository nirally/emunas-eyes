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
// B Disruptor, X Light attack, Y Heavy attack, R3 Runner's Vision, Back Map, Start Pause.
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
    if (tn.stick_click) p.buttons |= kRightThumb;  // Runner's Vision
    if (mv.stick_click) p.buttons |= kBack;        // Map
    if (left.menu || right.menu) p.buttons |= kStart;  // Pause
    return p;
}

}  // namespace mecvr::pad_map
