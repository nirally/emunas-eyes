#pragma once
// Mod-owned turning (Stage 4, D-028). Pure, unit-tested.
//
// W = the player's world heading (the tracking-space origin's yaw). The view's heading is W + head yaw.
// The game's heading G is steered toward W + head yaw with injected mouse counts (one count turns the
// game by `rad_per_count`, measured: -0.001691 rad at the user's mouse sensitivity; linear, symmetric).
// Changes of G that our injection doesn't explain (quick turn, wall-runs, swings, takedowns) are the
// game's own turns: they are added to W, so the view follows them as before.
#include <cmath>

namespace mecvr::yaw_model {

inline float wrap(float a) {
    const float two_pi = 6.2831853f;
    a = std::fmod(a + 3.14159265f, two_pi);
    if (a < 0) a += two_pi;
    return a - 3.14159265f;
}

struct Model {
    float rad_per_count = -0.001691f;
    float gain = 0.6f;            // share of the remaining error corrected per frame
    int max_counts = 600;         // per frame (~58 deg)
    float deadband = 0.004f;      // rad (~0.25 deg): no corrections below this
    double lost_after_s = 0.25;   // nothing of the injected turn seen for this long = the game ignores it

    float world = 0;              // W
    bool primed = false;
    float last_game = 0;
    float pending = 0;            // injected turn not yet seen in G (rad)
    double pending_since = 0;     // last time an injected turn was sent into an empty queue or seen arriving

    // One game frame. game = G, head_yaw = tracking-space head yaw (relative to recentre),
    // turn = the player's stick/snap turn this frame (rad, + = left). steer: inject this frame (gameplay,
    // game window in front). Returns mouse counts to inject now.
    int update(double now, float game, float head_yaw, float turn, bool steer) {
        if (!primed) {
            primed = true;
            world = wrap(game - head_yaw);  // the view starts where it was: W + head = G
            last_game = game;
        }
        float d = wrap(game - last_game);
        last_game = game;
        if (pending != 0 && d * pending > 0) {  // our injection arriving
            const float used = std::fabs(d) < std::fabs(pending) ? d : pending;
            pending -= used;
            d -= used;
            pending_since = now;  // still arriving: the game is taking our input
        }
        if (pending != 0 && now - pending_since > lost_after_s) pending = 0;
        world = wrap(world + d + turn);  // the game's own turns + the player's turns
        if (!steer) return 0;
        const float err = wrap(world + head_yaw - game - pending);
        if (std::fabs(err) < deadband) return 0;
        long c = std::lround(gain * err / rad_per_count);
        if (c > max_counts) c = max_counts;
        if (c < -max_counts) c = -max_counts;
        if (c) {
            if (pending == 0) pending_since = now;
            pending += c * rad_per_count;
        }
        return int(c);
    }
};

// Snap turn: one turn per stick flick past `on`, re-armed when the stick returns inside `off`.
struct Snap {
    float on = 0.7f, off = 0.3f;
    bool armed = true;
    // x = stick, + = right. Returns the turn this frame (rad, + = left).
    float update(float x, float angle_rad) {
        if (armed && std::fabs(x) > on) {
            armed = false;
            return x > 0 ? -angle_rad : angle_rad;
        }
        if (std::fabs(x) < off) armed = true;
        return 0;
    }
};

}  // namespace mecvr::yaw_model
