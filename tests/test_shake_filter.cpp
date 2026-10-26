#include "core/shake_filter.h"
#include "tests/check.h"

#include <cmath>

using namespace mecvr;
using vm::Quat;
using vm::Vec3;

namespace {
float angle_deg(Quat a, Quat b) {
    const Vec3 v = shake_filter::log_q((a.conjugate() * b).normalized());
    return std::sqrt(v.dot(v)) * 57.29578f;
}
constexpr float kDt = 1.f / 45.f;  // one stereo pair
}  // namespace

TEST(shake_filter_log_exp_round_trip) {
    const Quat q = Quat::axis_angle(Vec3{0.3f, 1.f, -0.2f}.normalized(), 1.1f);
    CHECK(angle_deg(shake_filter::exp_q(shake_filter::log_q(q)), q) < 0.01f);
    CHECK(angle_deg(shake_filter::exp_q({0, 0, 0}), Quat{}) < 1e-4f);
}

TEST(shake_filter_smooths_a_small_fast_shake) {
    // +-2 deg of roll at 10 Hz (the train roof): the output keeps under 40% of it once settled.
    shake_filter::Filter f;
    float peak = 0;
    for (int i = 0; i < 450; ++i) {
        const float deg = 2.f * std::sin(6.2831853f * 10.f * i * kDt);
        const Quat out = f.update(Quat::axis_angle({0, 0, 1}, deg / 57.29578f), kDt);
        if (i > 90) peak = std::fmax(peak, angle_deg(out, Quat{}));
    }
    CHECK(peak < 0.8f);
}

TEST(shake_filter_passes_a_steady_turn_with_little_lag) {
    // 150 deg/s of look pitch (the stick) for 1 s: under 2 deg behind at the end; a 90 deg roll in 0.3 s the same.
    shake_filter::Filter f;
    Quat out;
    for (int i = 1; i <= 45; ++i) out = f.update(Quat::axis_angle({1, 0, 0}, 150.f * i * kDt / 57.29578f), kDt);
    CHECK(angle_deg(out, Quat::axis_angle({1, 0, 0}, 150.f / 57.29578f)) < 2.f);
    shake_filter::Filter g;
    for (int i = 1; i <= 14; ++i) out = g.update(Quat::axis_angle({0, 0, 1}, (90.f * i / 14.f) / 57.29578f), kDt);
    CHECK(angle_deg(out, Quat::axis_angle({0, 0, 1}, 90.f / 57.29578f)) < 2.5f);
}

TEST(shake_filter_lets_a_camera_cut_through_at_once) {
    shake_filter::Filter f;
    for (int i = 0; i < 20; ++i) f.update(Quat{}, kDt);
    const Quat cut = Quat::axis_angle({0, 1, 0}, 0.8f);  // 46 deg in one step
    CHECK(angle_deg(f.update(cut, kDt), cut) < 1e-3f);
    const Quat still = Quat::axis_angle({0, 1, 0}, 0.3f);
    CHECK(angle_deg(f.update(still, kDt), still) < 1e-3f);  // a still input stays exact
    for (int i = 0; i < 5; ++i) CHECK(angle_deg(f.update(still, kDt), still) < 1e-3f);
}

namespace {
// Feeds `deg(t)` about `axis` for `secs` to a detector, as the steps between updates; returns the last weight.
template <class F>
float feed(shake_filter::Detector& d, Vec3 axis, float secs, float t0, F deg) {
    float w = d.weight;
    for (int i = 1; i <= int(secs / kDt); ++i) {
        const float t = t0 + i * kDt, step = (deg(t) - deg(t - kDt)) / 57.29578f;
        w = d.update(axis * step, kDt);
    }
    return w;
}
}  // namespace

TEST(shake_detector_fades_out_a_back_and_forth_shake_and_back_in) {
    // The train roof: +-3 deg of pitch at 3 Hz. Within 1.5 s the game motion is faded out; 2 s after it stops, back.
    shake_filter::Detector d;
    CHECK(feed(d, {1, 0, 0}, 1.5f, 0.f, [](float t) { return 3.f * std::sin(6.2831853f * 3.f * t); }) < 0.01f);
    CHECK(d.shaking);
    CHECK(feed(d, {1, 0, 0}, 2.f, 1.5f, [](float) { return 0.f; }) > 0.99f);
    CHECK(!d.shaking);
}

TEST(shake_detector_catches_a_shake_on_two_axes) {
    // The desktop fake shake: +-3 deg roll at 4 Hz with +-3 deg pitch at 5 Hz - the step's direction turns smoothly.
    shake_filter::Detector d;
    float w = 1.f;
    for (int i = 1; i <= int(1.5f / kDt); ++i) {
        const float t = i * kDt, tp = t - kDt;
        const float r = 3.f * (std::sin(6.2831853f * 4.f * t) - std::sin(6.2831853f * 4.f * tp)) / 57.29578f;
        const float q = 3.f * (std::sin(6.2831853f * 5.f * t) - std::sin(6.2831853f * 5.f * tp)) / 57.29578f;
        w = d.update(Vec3{q, 0, r}, kDt);
    }
    CHECK(d.shaking);
    CHECK(w < 0.01f);
}

TEST(shake_detector_leaves_rolls_turns_and_dips_alone) {
    shake_filter::Detector d;
    // A 360 deg roll in 0.7 s, a 30 deg wall-run tilt and back over 1.2 s, a landing dip, a 150 deg/s stick look.
    CHECK(feed(d, {0, 0, 1}, 0.7f, 0.f, [](float t) { return 360.f * t / 0.7f; }) > 0.99f);
    CHECK(feed(d, {0, 0, 1}, 1.2f, 0.f, [](float t) { return 30.f * std::sin(3.14159265f * t / 1.2f); }) > 0.99f);
    CHECK(feed(d, {1, 0, 0}, 0.5f, 0.f, [](float t) { return -8.f * std::sin(3.14159265f * t / 0.5f); }) > 0.99f);
    CHECK(feed(d, {1, 0, 0}, 1.f, 0.f, [](float t) { return 150.f * t; }) > 0.99f);
    CHECK(!d.shaking);
}

TEST(shake_blend_goes_from_calm_to_full) {
    const Quat calm = Quat::axis_angle({1, 0, 0}, 0.2f), full = Quat::axis_angle({0, 0, 1}, 0.6f) * calm;
    CHECK(angle_deg(shake_filter::blend(calm, full, 0.f), calm) < 1e-3f);
    CHECK(angle_deg(shake_filter::blend(calm, full, 1.f), full) < 1e-3f);
    CHECK(std::fabs(angle_deg(shake_filter::blend(calm, full, 0.5f), calm) - 0.3f * 57.29578f) < 0.05f);
}
