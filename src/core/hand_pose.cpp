#include "core/hand_pose.h"

#include <atomic>
#include <mutex>

namespace mecvr::hand_pose {
namespace {
std::mutex g_mutex;
head_pose::Pose g_hand[2];
std::atomic<bool> g_sim{false};
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
    p.rotation = q;
    p.position = s.relative.position + q.rotate({hand ? 0.20f : -0.20f, -0.30f, -0.35f});
    p.valid = true;
    return p;
}

void set_sim(bool on) { g_sim = on; }

}  // namespace mecvr::hand_pose
