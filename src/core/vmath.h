#pragma once
// Minimal vector math for camera work. Conventions match both Frostbite and
// OpenXR: right-handed, Y up, cameras look down -Z. A Frostbite LinearTransform
// stores the camera's world-space basis vectors as rows: right (X), up (Y),
// back (Z), then translation.
#include <cmath>

namespace mecvr::vm {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(Vec3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const { float l = length(); return l > 1e-12f ? *this * (1.f / l) : Vec3{}; }
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    static Quat axis_angle(Vec3 axis, float radians) {
        Vec3 a = axis.normalized();
        float s = std::sin(radians * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
    }
    Quat operator*(Quat q) const {
        return {w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z};
    }
    Vec3 rotate(Vec3 v) const {
        Vec3 u{x, y, z};
        Vec3 t = u.cross(v) * 2.f;
        return v + t * w + u.cross(t);
    }
    Quat normalized() const {
        float l = std::sqrt(x * x + y * y + z * z + w * w);
        return l > 1e-12f ? Quat{x / l, y / l, z / l, w / l} : Quat{};
    }
    Quat conjugate() const { return {-x, -y, -z, w}; }
};

// 4 rows of float4, 16-byte aligned, exactly as the game stores it.
struct alignas(16) LinearTransform {
    float right[4], up[4], back[4], trans[4];

    Vec3 r() const { return {right[0], right[1], right[2]}; }
    Vec3 u() const { return {up[0], up[1], up[2]}; }
    Vec3 b() const { return {back[0], back[1], back[2]}; }
    Vec3 t() const { return {trans[0], trans[1], trans[2]}; }

    // Writes basis and translation; leaves each row's w lane untouched (the game packs data there).
    void set(Quat q, Vec3 pos) {
        Vec3 bx = q.rotate({1, 0, 0}), by = q.rotate({0, 1, 0}), bz = q.rotate({0, 0, 1});
        right[0] = bx.x; right[1] = bx.y; right[2] = bx.z;
        up[0] = by.x;    up[1] = by.y;    up[2] = by.z;
        back[0] = bz.x;  back[1] = bz.y;  back[2] = bz.z;
        trans[0] = pos.x; trans[1] = pos.y; trans[2] = pos.z;
    }
};
static_assert(sizeof(LinearTransform) == 64);

// Rotation of a transform's basis as a quaternion (basis rows are the rotated X, Y, Z axes).
inline Quat quat_of(const LinearTransform& m) {
    // Matrix with columns = basis vectors: m00 = right.x, m10 = right.y, m20 = right.z, ...
    float m00 = m.right[0], m10 = m.right[1], m20 = m.right[2];
    float m01 = m.up[0], m11 = m.up[1], m21 = m.up[2];
    float m02 = m.back[0], m12 = m.back[1], m22 = m.back[2];
    float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.f) * 2.f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.f + m00 - m11 - m22) * 2.f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        float s = std::sqrt(1.f + m11 - m00 - m22) * 2.f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        float s = std::sqrt(1.f + m22 - m00 - m11) * 2.f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return q.normalized();
}

// Heading of a camera: angle about world +Y of its forward (-back) direction,
// 0 when looking down world -Z, positive turning left (counter-clockwise from above).
inline float yaw_of(const LinearTransform& m) {
    Vec3 fwd = m.b() * -1.f;
    return std::atan2(-fwd.x, -fwd.z);
}

// Heading that survives the game camera tumbling (rolls, flips). For a camera built as
// yaw * pitch * roll with |roll| < 90 deg, the back axis is (sin h cos p, -sin p, cos h cos p)
// and cos p has the sign of up.y - so flipping by that sign gives the exact heading for any
// pitch, including upside down mid-roll. When the back axis is near vertical (the instant of
// looking straight down/up) it falls back to the right axis, which a forward roll never moves.
inline float heading_of(const LinearTransform& m) {
    Vec3 b = m.b();
    float horiz = std::sqrt(b.x * b.x + b.z * b.z);
    if (horiz > 0.3f) {
        float s = m.u().y >= 0.f ? 1.f : -1.f;
        return std::atan2(s * b.x, s * b.z);
    }
    Vec3 r = m.r();
    return std::atan2(-r.z, r.x);
}

inline float pitch_of(const LinearTransform& m) {
    Vec3 fwd = (m.b() * -1.f).normalized();
    return std::asin(fwd.y < -1.f ? -1.f : fwd.y > 1.f ? 1.f : fwd.y);
}

// Roll: angle of the camera's right vector out of the horizontal plane.
inline float roll_of(const LinearTransform& m) {
    Vec3 fwd = (m.b() * -1.f).normalized();
    Vec3 level_right = fwd.cross({0, 1, 0}).normalized();  // horizontal right for this heading
    Vec3 right = m.r().normalized();
    if (level_right.length() < 1e-6f) return 0.f;  // looking straight up/down: undefined
    float s = level_right.cross(right).dot(fwd);
    float c = level_right.dot(right);
    return std::atan2(s, c);
}

inline Quat yaw_quat(float yaw) { return Quat::axis_angle({0, 1, 0}, yaw); }

}  // namespace mecvr::vm
