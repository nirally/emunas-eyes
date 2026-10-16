#include "core/dodge_model.h"
#include "tests/check.h"

using namespace mecvr::dodge_model;

namespace {
// Moves the head at (vx, vz) m/s for `secs` at 90 Hz; returns the number of dodges (last direction in dir).
int move(Model& m, V2& p, float vx, float vz, float secs, V2& dir) {
    int k = 0;
    for (float t = 0; t < secs; t += 1.f / 90) {
        p.x += vx / 90;
        p.z += vz / 90;
        k += m.update(p, 1.f / 90, dir);
    }
    return k;
}
}  // namespace

TEST(dodge_quick_side_step) {
    Model m;
    V2 p, dir;
    move(m, p, 0, 0, 0.2f, dir);
    CHECK(move(m, p, -1.8f, 0, 0.2f, dir) == 1);  // 36 cm to the left in 0.2 s
    CHECK(dir.x < -0.9f);
    CHECK(move(m, p, 0, 0, 0.5f, dir) == 0);
}

TEST(dodge_walking_is_not_a_dodge) {
    Model m;
    V2 p, dir;
    move(m, p, 0, 0, 0.2f, dir);
    CHECK(move(m, p, 0.8f, 0.3f, 2.f, dir) == 0);
}

TEST(dodge_leaning_in_is_not_a_dodge) {
    Model m;
    V2 p, dir;
    move(m, p, 0, 0, 0.2f, dir);
    CHECK(move(m, p, 0.3f, -1.8f, 0.2f, dir) == 0);  // quick lean forward (into a punch)
}

TEST(dodge_back_step_and_rearm) {
    Model m;
    V2 p, dir;
    move(m, p, 0, 0, 0.2f, dir);
    int k = move(m, p, 0, 1.8f, 0.2f, dir);   // back
    CHECK(dir.z > 0.9f);
    k += move(m, p, 0, 0, 0.7f, dir);         // stop
    k += move(m, p, 1.8f, 0, 0.2f, dir);      // right
    CHECK(k == 2);
    CHECK(dir.x > 0.9f);
}

TEST(dodge_small_twitch_is_not_a_dodge) {
    Model m;
    V2 p, dir;
    move(m, p, 0, 0, 0.2f, dir);
    CHECK(move(m, p, 1.2f, 0, 0.05f, dir) == 0);  // 6 cm head twitch
}

TEST(dodge_recentre_jump_is_not_a_dodge) {
    Model m;
    V2 p{0.3f, 0.1f}, dir;
    move(m, p, 0, 0, 0.3f, dir);
    p = {0, 0};  // recentre: the head is at the origin again in one frame
    CHECK(move(m, p, 0, 0, 0.3f, dir) == 0);
}
