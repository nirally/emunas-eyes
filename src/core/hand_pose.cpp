#include "core/hand_pose.h"

#include <windows.h>

#include <atomic>
#include <mutex>

namespace mecvr::hand_pose {
namespace {
std::mutex g_mutex;
head_pose::Pose g_hand[2];
std::atomic<bool> g_sim{false}, g_closeup{false};
}  // namespace

void set(int hand, const head_pose::Pose& relative) {
    std::lock_guard lock(g_mutex);
    g_hand[hand & 1] = relative;
}

head_pose::Pose get(int hand) {
    {
        std::lock_guard lock(g_mutex);
        if (g_hand[hand & 1].valid) return g_hand[hand & 1];
    }
    if (!g_sim.load()) return {};
    // Simulator: hands 35 cm ahead of the head, 20 cm to each side, 30 cm lower, following its yaw.
    const head_pose::Sample s = head_pose::current();
    head_pose::Pose p;
    const vm::Vec3 f = s.relative.rotation.rotate({0, 0, -1});
    const float yaw = std::atan2(-f.x, -f.z);
    const vm::Quat q = vm::yaw_quat(yaw);
    // Held like a Touch controller: the handle (grip -Z) tilted ~55 deg up from forward.
    p.rotation = (q * vm::Quat::axis_angle({1, 0, 0}, 0.95f)).normalized();
    p.position = s.relative.position + q.rotate({hand ? 0.20f : -0.20f, -0.30f, -0.35f});
    if (g_closeup.load()) {  // handle tipped towards the eyes, so the fingers curl face-on
        p.rotation = (q * vm::Quat::axis_angle({0, 1, 0}, hand ? 0.5f : -0.5f) * vm::Quat::axis_angle({1, 0, 0}, 2.4f)).normalized();
        p.position = s.relative.position + q.rotate({hand ? 0.11f : -0.11f, -0.12f, -0.32f});
    }
    p.valid = true;
    return p;
}

void set_sim(bool on) { g_sim = on; }
void set_sim_closeup(bool on) { g_closeup = on; }

namespace {
Fingers g_fingers[2];
ULONGLONG g_fingers_tick[2] = {0, 0};
}  // namespace

void set_controls(int hand, float trigger, bool trigger_touch, float grip, bool thumb_touch) {
    // Finger on the trigger: resting (0.35) to fully pulled; off it: pointing. Touch fingers rest on the grip
    // even when it isn't pressed, so the others start slightly bent.
    Fingers t;
    t.index = trigger_touch || trigger > 0.05f ? 0.35f + 0.65f * trigger : 0.f;
    t.others = 0.15f + 0.85f * grip;
    t.thumb = thumb_touch ? 1.f : 0.f;
    const ULONGLONG now = GetTickCount64();
    std::lock_guard lock(g_mutex);
    Fingers& f = g_fingers[hand & 1];
    const float dt = g_fingers_tick[hand & 1] ? (now - g_fingers_tick[hand & 1]) / 1000.f : 1.f;
    g_fingers_tick[hand & 1] = now;
    const float a = dt >= 0.06f ? 1.f : dt / 0.06f;  // ~60 ms to follow: smooths the on/off touches
    f.index += (t.index - f.index) * a;
    f.others += (t.others - f.others) * a;
    f.thumb += (t.thumb - f.thumb) * a;
}

Fingers fingers(int hand) {
    {
        std::lock_guard lock(g_mutex);
        if (g_hand[hand & 1].valid) return g_fingers[hand & 1];
    }
    Fingers f;
    if (g_sim.load()) {
        if (hand) f = {1.f, 1.f, 1.f};  // right: fist, thumb down
        else f = {0.f, 1.f, 0.f};       // left: pointing, thumb up
    }
    return f;
}

}  // namespace mecvr::hand_pose

namespace mecvr::hand_pose {
namespace {
std::atomic<ULONGLONG> g_buzz_until[2] = {0, 0}, g_buzz_last[2] = {0, 0};
}  // namespace

void contact_pulse(int hand) {
    const ULONGLONG now = GetTickCount64();
    if (now - g_buzz_last[hand & 1].load() < 250) return;  // sliding along a wall: not a stream of buzzes
    g_buzz_last[hand & 1] = now;
    g_buzz_until[hand & 1] = now + 50;
}

float contact_buzz(int hand) { return GetTickCount64() < g_buzz_until[hand & 1].load() ? 0.6f : 0.f; }

}  // namespace mecvr::hand_pose

namespace mecvr::hand_pose {
namespace {
std::atomic<bool> g_press[2] = {false, false};
std::atomic<ULONGLONG> g_press_tick[2] = {0, 0};
}  // namespace
void set_touch_press(int hand, bool on) {
    g_press[hand & 1] = on;
    g_press_tick[hand & 1] = GetTickCount64();
}
// Refreshed every drawn frame; a press not refreshed for 250 ms (hands not drawn, menu) counts as released.
bool touch_press() {
    const ULONGLONG now = GetTickCount64();
    for (int h = 0; h < 2; ++h)
        if (g_press[h].load() && now - g_press_tick[h].load() < 250) return true;
    return false;
}
}  // namespace mecvr::hand_pose
