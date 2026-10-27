#include "core/pad_map.h"
#include "tests/check.h"

using namespace mecvr::pad_map;

TEST(pad_map_right_handed_layout) {
    Hand l, r;
    l.x = 0.5f; l.y = 1.f; l.grip = 1.f; l.trigger = 1.f; l.primary = true; l.stick_click = true; l.menu = true;
    r.x = -1.f; r.y = 0.7f; r.trigger = 0.5f; r.secondary = true; r.stick_click = true;
    const Pad p = map(l, r, false, false);
    CHECK(p.lx == 16384 && p.ly == 32767);           // left stick moves
    CHECK(p.rx == -32767 && p.ry == 0);               // right stick turns; no pitch by default
    CHECK(p.left_trigger == 255 && p.right_trigger == 128);
    CHECK((p.buttons & kLB) && !(p.buttons & kRB));   // left grip: Up actions
    CHECK((p.buttons & kX) && (p.buttons & kB));      // X light attack (left), B disruptor (right)
    CHECK(!(p.buttons & kA) && !(p.buttons & kY));
    CHECK(!(p.buttons & kBack) && !(p.buttons & kRightThumb) && (p.buttons & kStart));  // stick clicks via TapHold
}

TEST(pad_map_left_handed_mirrors) {
    Hand l, r;
    r.x = 0.25f; r.y = -1.f; r.grip = 0.9f; r.primary = true;   // right hand now moves
    l.x = 1.f; l.trigger = 1.f; l.primary = true;               // left hand now turns
    const Pad p = map(l, r, true, false);
    CHECK(p.lx == 8192 && p.ly == -32767);
    CHECK(p.rx == 32767);
    CHECK(p.right_trigger == 255 && p.left_trigger == 0);       // turn hand's trigger = Shift
    CHECK((p.buttons & kLB) && !(p.buttons & kRB));             // move hand's grip = Up actions
    CHECK((p.buttons & kX) && (p.buttons & kA));                // move-hand primary X, turn-hand primary A
}

TEST(pad_map_stick_pitch_and_clamping) {
    Hand l, r;
    r.y = 2.f;                       // out of range input is clamped
    CHECK(map(l, r, false, true).ry == 32767);
    CHECK(map(l, r, false, false).ry == 0);
    l.grip = 0.5f;                   // grips need more than half a squeeze
    CHECK(!(map(l, r, false, false).buttons & kLB));
}

TEST(pad_map_stick_click_tap_pauses_hold_opens_map) {
    TapHold th;
    // tap: press 0.1 s, release -> Start for 0.15 s after release, never Back
    CHECK(th.update(true, 0.00, kStart, kBack) == 0);
    CHECK(th.update(true, 0.10, kStart, kBack) == 0);
    CHECK(th.update(false, 0.12, kStart, kBack) == kStart);
    CHECK(th.update(false, 0.20, kStart, kBack) == kStart);
    CHECK(th.update(false, 0.30, kStart, kBack) == 0);
    // hold: Back from 0.5 s while held, no Start on release
    CHECK(th.update(true, 1.00, kStart, kBack) == 0);
    CHECK(th.update(true, 1.40, kStart, kBack) == 0);
    CHECK(th.update(true, 1.55, kStart, kBack) == kBack);
    CHECK(th.update(false, 1.60, kStart, kBack) == 0);
    CHECK(th.update(false, 1.70, kStart, kBack) == 0);
}

TEST(pad_map_both_sticks_held_open_the_settings_menu) {
    StickChord ch;
    TapHold l, r{1.0, 0.15};
    auto frame = [&](bool a, bool b, double t, uint16_t* lb, uint16_t* rb) {
        const bool menu = ch.update(a, b, t);
        if (ch.on) { l.cancel(); r.cancel(); *lb = *rb = 0; }
        else { *lb = l.update(a, t, kStart, kBack); *rb = r.update(b, t, kRightThumb, 0x0400); }
        return menu;
    };
    uint16_t lb, rb;
    // left first, right 0.2 s later: no menu before 1 s of both
    CHECK(!frame(true, false, 0.0, &lb, &rb) && lb == 0);
    CHECK(!frame(true, true, 0.2, &lb, &rb));
    CHECK(!frame(true, true, 1.1, &lb, &rb));
    CHECK(frame(true, true, 1.25, &lb, &rb) && lb == 0 && rb == 0);
    // releasing one, then the other: no pause, no Runner's Vision, no menu
    CHECK(!frame(false, true, 1.4, &lb, &rb) && lb == 0 && rb == 0);
    CHECK(!frame(false, false, 1.5, &lb, &rb) && lb == 0 && rb == 0);
    CHECK(!frame(false, false, 1.6, &lb, &rb) && lb == 0 && rb == 0);
    // a quick both-click (under 1 s) does nothing either
    CHECK(!frame(true, true, 2.0, &lb, &rb));
    CHECK(!frame(false, false, 2.3, &lb, &rb) && lb == 0 && rb == 0);
    // a lone left tap still pauses afterwards
    CHECK(!frame(true, false, 3.0, &lb, &rb));
    CHECK(!frame(false, false, 3.1, &lb, &rb) && lb == kStart);
}

TEST(pad_map_stick_click_no_longer_maps_back_directly) {
    Hand l, r;
    l.stick_click = true;
    CHECK(!(map(l, r, false, false).buttons & kBack));
}

TEST(pad_map_physical_crouch_hysteresis) {
    Crouch c;
    c.depth = 0.30f;  // 0.08 m hysteresis
    CHECK(!c.update(0.f));
    CHECK(!c.update(-0.29f));
    CHECK(c.update(-0.31f));   // crouched
    CHECK(c.update(-0.25f));   // still crouched (inside hysteresis)
    CHECK(!c.update(-0.21f));  // stood up
    CHECK(!c.update(-0.29f));
}
