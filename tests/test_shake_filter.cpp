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
