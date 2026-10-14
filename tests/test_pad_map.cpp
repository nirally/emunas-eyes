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
    CHECK((p.buttons & kBack) && (p.buttons & kRightThumb) && (p.buttons & kStart));
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
