#include "core/ui_fit.h"
#include "tests/check.h"

#include <cmath>

using namespace mecvr;

TEST(ui_fit_scales_about_the_frame_centre) {
    // The full-frame viewport becomes the middle 40%; the health bar's corner moves in, the centre stays.
    const ui_fit::Rect full = ui_fit::scale_about_centre({0, 0, 2880, 1620}, 2880, 1620, 0.4f);
    CHECK(std::fabs(full.x - 864.f) < 1e-3f && std::fabs(full.y - 486.f) < 1e-3f);
    CHECK(std::fabs(full.w - 1152.f) < 1e-3f && std::fabs(full.h - 648.f) < 1e-3f);
    const ui_fit::Rect centre = ui_fit::scale_about_centre({1440, 810, 0, 0}, 2880, 1620, 0.4f);
    CHECK(std::fabs(centre.x - 1440.f) < 1e-3f && std::fabs(centre.y - 810.f) < 1e-3f);
    const ui_fit::Rect same = ui_fit::scale_about_centre({100, 200, 300, 400}, 2880, 1620, 1.f);
    CHECK(same.x == 100.f && same.y == 200.f && same.w == 300.f && same.h == 400.f);
}

TEST(ui_fit_places_an_element_in_frame_space) {
    // No offset: the same as scaling about the centre.
    const ui_fit::Rect a = ui_fit::place({100, 200, 300, 400}, 2880, 1620, 0.4f, 0.f, 0.f);
    const ui_fit::Rect b = ui_fit::scale_about_centre({100, 200, 300, 400}, 2880, 1620, 0.4f);
    CHECK(std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f && std::fabs(a.w - b.w) < 1e-3f);
    // The point at frame (-0.78, -0.75) lands on (0, -0.35) at scale 0.6 (o = t - k a).
    const float k = 0.6f, ox = 0.f - k * -0.78f, oy = -0.35f - k * -0.75f;
    const ui_fit::Rect full = ui_fit::place({0, 0, 2880, 1620}, 2880, 1620, k, ox, oy);
    const float px = (-0.78f + 1.f) * 0.5f * 2880.f, py = (1.f - -0.75f) * 0.5f * 1620.f;  // the point in pixels
    const float qx = full.x + px / 2880.f * full.w, qy = full.y + py / 1620.f * full.h;   // through the new viewport
    CHECK(std::fabs(qx - 1440.f) < 0.5f && std::fabs(qy - (1.f + 0.35f) * 0.5f * 1620.f) < 0.5f);
}
