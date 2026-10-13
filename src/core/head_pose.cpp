#include "core/head_pose.h"

#include <windows.h>

#include <cmath>
#include <mutex>

namespace mecvr::head_pose {
namespace {

std::mutex g_mutex;

// Headset
Pose g_headset;
bool g_recenter_pending = false;
vm::Quat g_origin_rot;   // heading only
vm::Vec3 g_origin_pos;
Pose g_rendered;

// Simulator
float g_yaw = 0, g_pitch = 0, g_roll = 0, g_height = 0;
constexpr float kTurnRate = 1.5f;     // rad/s (~86 deg/s)
constexpr float kHeightRate = 0.5f;   // m/s

bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

float heading_of(vm::Quat q) {
    vm::Vec3 fwd = q.rotate({0, 0, -1});
    return std::atan2(-fwd.x, -fwd.z);
}

}  // namespace

void set_headset_pose(const Pose& absolute, bool valid) {
    std::lock_guard lock(g_mutex);
    g_headset = absolute;
    g_headset.valid = valid && absolute.valid;
    if (g_headset.valid && g_recenter_pending) {
        g_recenter_pending = false;
        g_origin_rot = vm::yaw_quat(heading_of(absolute.rotation));
        g_origin_pos = absolute.position;
    }
}

void request_recenter() {
    std::lock_guard lock(g_mutex);
    g_recenter_pending = true;
}

void set_rendered(const Pose& absolute) {
    std::lock_guard lock(g_mutex);
    g_rendered = absolute;
}

Pose rendered_pose() {
    std::lock_guard lock(g_mutex);
    return g_rendered.valid ? g_rendered : g_headset;
}

void sim_update(float dt) {
    if (down(VK_NUMPAD5)) {
        std::lock_guard lock(g_mutex);
        g_yaw = g_pitch = g_roll = g_height = 0;
        g_recenter_pending = true;
        return;
    }
    if (dt <= 0 || dt > 0.25f) return;
    std::lock_guard lock(g_mutex);
    if (down(VK_NUMPAD4)) g_yaw += kTurnRate * dt;
    if (down(VK_NUMPAD6)) g_yaw -= kTurnRate * dt;
    if (down(VK_NUMPAD8)) g_pitch += kTurnRate * dt;
    if (down(VK_NUMPAD2)) g_pitch -= kTurnRate * dt;
    if (down(VK_NUMPAD7)) g_roll += kTurnRate * dt;
    if (down(VK_NUMPAD9)) g_roll -= kTurnRate * dt;
    if (down(VK_ADD)) g_height += kHeightRate * dt;
    if (down(VK_SUBTRACT)) g_height -= kHeightRate * dt;
    if (g_pitch > 1.5f) g_pitch = 1.5f;
    if (g_pitch < -1.5f) g_pitch = -1.5f;
}

Sample current() {
    std::lock_guard lock(g_mutex);
    Sample s;
    if (g_headset.valid) {
        // relative = origin^-1 * absolute
        vm::Quat inv = g_origin_rot.conjugate();
        s.relative.rotation = (inv * g_headset.rotation).normalized();
        s.relative.position = inv.rotate(g_headset.position - g_origin_pos);
        s.relative.valid = true;
        s.absolute = g_headset;
        s.headset = true;
        return s;
    }
    // Yaw about world up, then pitch about the new right axis, then roll about the view axis.
    s.relative.rotation = vm::yaw_quat(g_yaw) * vm::Quat::axis_angle({1, 0, 0}, g_pitch) *
                          vm::Quat::axis_angle({0, 0, 1}, g_roll);
    s.relative.position = {0, g_height, 0};
    s.relative.valid = true;
    s.absolute = s.relative;
    return s;
}

}  // namespace mecvr::head_pose
