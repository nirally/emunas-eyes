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
