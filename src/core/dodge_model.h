#pragma once
// Shift by body movement (Stage 6, D-040). Pure, unit-tested.
//
// A dodge is a quick head movement to the side or backwards (a lean or a step): horizontal head speed over
// `speed` and at least `dist` covered in the last 0.2 s. Forward doesn't count (you lean in to punch). One
// dodge per movement: the head has to slow below `rearm` first, and dodges are at least `min_gap` apart.
// Vectors are horizontal in the body frame (x right, z back; metres), like room_model.
#include <cmath>

namespace mecvr::dodge_model {

struct V2 {
    float x = 0, z = 0;
};

struct Model {
    float speed = 1.0f;     // m/s: walking around the room is slower (1.3 was "too much", PLAYTEST-6c)
    float dist = 0.08f;     // m covered in the last 0.2 s
    float rearm = 0.5f;     // m/s
    float min_gap = 0.6f;   // s
    float smooth = 0.04f;   // s: head velocity smoothing
    float jump = 0.25f;     // m in one frame: a recentre or tracking glitch, not a movement

    static constexpr int kN = 64;
    V2 hist[kN];
    float hist_t[kN] = {};
    int n = 0, head_i = 0;
    float t = 0, since = 1e9f, vx = 0, vz = 0;
    V2 last;
    bool primed = false, armed = true;

    // Returns true on the frame a dodge is recognised; dir = its unit direction.
    bool update(V2 p, float dt, V2& dir) {
        const float jx = p.x - last.x, jz = p.z - last.z;
        if (!primed || dt <= 0 || dt > 0.1f || jx * jx + jz * jz > jump * jump) {  // start fresh
            primed = true;
            last = p;
            vx = vz = 0;
            n = 0;
            return false;
        }
        t += dt;
        since += dt;
        const float a = dt / (smooth + dt);
        vx += (jx / dt - vx) * a;
        vz += (jz / dt - vz) * a;
        last = p;
        hist[head_i] = p;
        hist_t[head_i] = t;
        head_i = (head_i + 1) % kN;
        if (n < kN) ++n;
        const float s = std::sqrt(vx * vx + vz * vz);
        if (s < rearm) armed = true;
        if (!armed || s < speed || since < min_gap) return false;
        // Oldest sample within the last 0.2 s.
        V2 old = p;
        for (int k = 1; k <= n; ++k) {
            const int i = (head_i - k + kN) % kN;
            if (t - hist_t[i] > 0.2f) break;
            old = hist[i];
        }
        const float dx = p.x - old.x, dz = p.z - old.z, d = std::sqrt(dx * dx + dz * dz);
        if (d < dist) return false;
        if (-dz / d > 0.707f) return false;  // within 45 deg of forward (z<0): leaning in, not dodging
        dir = {dx / d, dz / d};
        armed = false;
        since = 0;
        return true;
    }
};

}  // namespace mecvr::dodge_model
