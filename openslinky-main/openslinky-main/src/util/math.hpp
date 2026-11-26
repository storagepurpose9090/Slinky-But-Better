#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace slinky::math {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPlayerEyeHeight = 1.62;

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct Range2f {
    float min = 0.0f;
    float max = 0.0f;
};

struct Color3f {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Vec4f {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct AABB {
    Vec3d min{};
    Vec3d max{};
};

inline Vec3d make_vec3(double x, double y, double z) noexcept {
    return {x, y, z};
}

inline Vec3d vec3_sub(const Vec3d& a, const Vec3d& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3d vec3_add(const Vec3d& a, const Vec3d& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3d vec3_scale(const Vec3d& v, double s) noexcept {
    return {v.x * s, v.y * s, v.z * s};
}

inline Vec3d vec3_lerp(const Vec3d& a, const Vec3d& b, double t) noexcept {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

inline double vec3_dist_sq(const Vec3d& a, const Vec3d& b) noexcept {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

inline double vec3_dist(const Vec3d& a, const Vec3d& b) noexcept {
    return std::sqrt(vec3_dist_sq(a, b));
}

inline void vec3_horiz_vert_dist(const Vec3d& a, const Vec3d& b, double* out_horiz, double* out_vert) noexcept {
    *out_horiz = std::hypot(a.x - b.x, a.z - b.z);
    *out_vert = std::fabs(a.y - b.y);
}

inline Vec3d vec3_add_eye_height(const Vec3d& p) noexcept {
    return {p.x, p.y + kPlayerEyeHeight, p.z};
}

inline Vec3d vec3_sub_eye_height(const Vec3d& p) noexcept {
    return {p.x, p.y - kPlayerEyeHeight, p.z};
}

inline double vec3_length(const Vec3d& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vec3d vec3_normalize(Vec3d v) noexcept {
    const double len = vec3_length(v);
    if (len == 0.0) {
        return {0.0, 0.0, 0.0};
    }
    return {v.x / len, v.y / len, v.z / len};
}

inline Vec3d vec3_rcp(const Vec3d& v) noexcept {
    return {1.0 / v.x, 1.0 / v.y, 1.0 / v.z};
}

inline AABB make_aabb(const Vec3d& min_pt, const Vec3d& max_pt) noexcept {
    return {min_pt, max_pt};
}

inline AABB aabb_from_points(const Vec3d& a, const Vec3d& b) noexcept {
    return {
        {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)},
        {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)},
    };
}

inline AABB aabb_expand(AABB box, double amount) noexcept {
    box.min.x -= amount;
    box.min.y -= amount;
    box.min.z -= amount;
    box.max.x += amount;
    box.max.y += amount;
    box.max.z += amount;
    return box;
}

inline bool aabb_intersects(const AABB& a, const AABB& b) noexcept {
    return b.max.x > a.min.x && a.max.x > b.min.x &&
           b.max.y > a.min.y && a.max.y > b.min.y &&
           b.max.z > a.min.z && a.max.z > b.min.z;
}

inline bool aabb_contains_point(const AABB& box, const Vec3d& p) noexcept {
    return p.x > box.min.x && box.max.x > p.x &&
           p.y > box.min.y && box.max.y > p.y &&
           p.z > box.min.z && box.max.z > p.z;
}

inline Vec3d aabb_closest_point(const AABB& box, const Vec3d& p) noexcept {
    return {
        p.x < box.min.x ? box.min.x : std::fmin(box.max.x, p.x),
        p.y < box.min.y ? box.min.y : std::fmin(box.max.y, p.y),
        p.z < box.min.z ? box.min.z : std::fmin(box.max.z, p.z),
    };
}

// Fast slab ray-segment intersection (origin + inv_dir where t in [0, 1]) - matches sub_18002B5D0
inline bool aabb_ray_intersects_rcp(const AABB& box, const Vec3d& origin, const Vec3d& inv_dir) noexcept {
    const float tx1 = static_cast<float>((box.min.x - origin.x) * inv_dir.x);
    const float tx2 = static_cast<float>((box.max.x - origin.x) * inv_dir.x);
    float tmin = std::fmin(std::fmax(tx1, 0.0f), std::fmax(tx2, 0.0f));
    float tmax = std::fmax(std::fmin(tx1, INFINITY), std::fmin(tx2, INFINITY));

    const float ty1 = static_cast<float>((box.min.y - origin.y) * inv_dir.y);
    const float ty2 = static_cast<float>((box.max.y - origin.y) * inv_dir.y);
    tmin = std::fmin(std::fmax(ty1, tmin), std::fmax(ty2, tmin));
    tmax = std::fmax(std::fmin(ty1, tmax), std::fmin(ty2, tmax));

    const float tz1 = static_cast<float>((box.min.z - origin.z) * inv_dir.z);
    const float tz2 = static_cast<float>((box.max.z - origin.z) * inv_dir.z);
    const float tmin_z1 = std::isnan(tz1) ? tmin : std::fmax(tmin, tz1);
    const float tmin_z2 = std::isnan(tz2) ? tmin : std::fmax(tmin, tz2);
    tmin = std::isnan(tmin_z1) ? tmin_z2 : std::fmin(tmin_z2, tmin_z1);

    const float tmax_z1 = std::fmin(tmax, tz1);
    const float tmax_z2 = std::fmin(tmax, tz2);
    tmax = std::isnan(tmax_z1) ? tmax_z2 : std::fmax(tmax_z2, tmax_z1);

    return tmax > tmin && static_cast<double>(tmin) <= 1.000000059604645;
}

// Slab ray-segment intersection returning hit point (origin + delta * t, t in [0, 1]) - matches sub_18002B7F0
inline bool aabb_raycast_segment(const AABB& box, const Vec3d& origin, const Vec3d& delta, Vec3d* out_hit) noexcept {
    const Vec3d inv_dir = vec3_rcp(delta);
    const float tx1 = static_cast<float>((box.min.x - origin.x) * inv_dir.x);
    const float tx2 = static_cast<float>((box.max.x - origin.x) * inv_dir.x);
    float tmin = std::fmin(std::fmax(tx1, 0.0f), std::fmax(tx2, 0.0f));
    float tmax = std::fmax(std::fmin(tx1, INFINITY), std::fmin(tx2, INFINITY));

    const float ty1 = static_cast<float>((box.min.y - origin.y) * inv_dir.y);
    const float ty2 = static_cast<float>((box.max.y - origin.y) * inv_dir.y);
    tmin = std::fmin(std::fmax(ty1, tmin), std::fmax(ty2, tmin));
    tmax = std::fmax(std::fmin(ty1, tmax), std::fmin(ty2, tmax));

    const float tz1 = static_cast<float>((box.min.z - origin.z) * inv_dir.z);
    const float tz2 = static_cast<float>((box.max.z - origin.z) * inv_dir.z);
    const float tmin_z1 = std::isnan(tz1) ? tmin : std::fmax(tmin, tz1);
    const float tmin_z2 = std::isnan(tz2) ? tmin : std::fmax(tmin, tz2);
    tmin = std::isnan(tmin_z1) ? tmin_z2 : std::fmin(tmin_z2, tmin_z1);

    const float tmax_z1 = std::fmin(tmax, tz1);
    const float tmax_z2 = std::fmin(tmax, tz2);
    tmax = std::isnan(tmax_z1) ? tmax_z2 : std::fmax(tmax_z2, tmax_z1);

    const bool hit = tmin < tmax && static_cast<double>(tmin) <= 1.000000059604645;
    if (hit && out_hit != nullptr) {
        out_hit->x = origin.x + delta.x * static_cast<double>(tmin);
        out_hit->y = origin.y + delta.y * static_cast<double>(tmin);
        out_hit->z = origin.z + delta.z * static_cast<double>(tmin);
    }
    return hit;
}

inline Vec2f make_vec2(float x, float y) noexcept {
    return {x, y};
}

inline Vec2f vec2_add(Vec2f a, Vec2f b) noexcept {
    return {a.x + b.x, a.y + b.y};
}

inline Vec2f vec2_sub(Vec2f a, Vec2f b) noexcept {
    return {a.x - b.x, a.y - b.y};
}

inline Vec2f vec2_scale(Vec2f v, float s) noexcept {
    return {v.x * s, v.y * s};
}

inline Vec2f vec2_lerp(Vec2f a, Vec2f b, float t) noexcept {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

inline float vec2_length_sq(Vec2f v) noexcept {
    return v.x * v.x + v.y * v.y;
}

inline float vec2_length(Vec2f v) noexcept {
    return std::hypot(v.x, v.y);
}

inline float vec2_dist(Vec2f a, Vec2f b) noexcept {
    return std::hypot(b.x - a.x, b.y - a.y);
}

inline float vec2_angle_to(Vec2f from, Vec2f to) noexcept {
    return std::atan2(to.y - from.y, to.x - from.x);
}

// Yaw/pitch in degrees from eye_pos to target_pos - matches sub_18002BC70
inline Vec2f calc_yaw_pitch(const Vec3d& from, const Vec3d& to) noexcept {
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double dz = to.z - from.z;
    const double horiz = std::sqrt(dx * dx + dz * dz);
    const float yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
    const float pitch = static_cast<float>(std::atan2(dy, horiz) * -180.0 / kPi);
    return {yaw, pitch};
}

inline Vec4f make_rect(float x, float y, float w, float h) noexcept {
    return {x, y, w, h};
}

inline bool rect_contains(const Vec4f& r, Vec2f p) noexcept {
    return p.x >= r.x && p.y >= r.y && (r.x + r.w) > p.x && (r.y + r.h) > p.y;
}

inline bool rect_overlaps(const Vec4f& a, const Vec4f& b) noexcept {
    return a.x <= (b.x + b.w) && b.x <= (a.x + a.w) &&
           a.y <= (b.y + b.h) && b.y <= (a.y + a.h);
}

inline Vec4f rect_inset(const Vec4f& r, float pad) noexcept {
    return {r.x + pad, r.y + pad, r.w - pad * 2.0f, r.h - pad * 2.0f};
}

inline Vec4f rect_round(const Vec4f& r) noexcept {
    return {std::round(r.x), std::round(r.y), std::round(r.w), std::round(r.h)};
}

inline float clampf(float min_v, float max_v, float val) noexcept {
    const float hi = std::fmin(max_v, val);
    return val < min_v ? min_v : hi;
}

inline float wrap_degrees_180(float angle) noexcept {
    float wrapped = std::fmod(angle, 360.0f);
    if (wrapped >= 180.0f) {
        wrapped -= 360.0f;
    } else if (wrapped < -180.0f) {
        wrapped += 360.0f;
    }
    return wrapped;
}

inline float angle_diff_deg(float a, float b) noexcept {
    return wrap_degrees_180(a - b);
}

inline Vec2f rotation_diff_deg(Vec2f a, Vec2f b) noexcept {
    return {wrap_degrees_180(a.x - b.x), wrap_degrees_180(a.y - b.y)};
}

inline float lerpf_clamped(float a, float b, float t) noexcept {
    return a + std::fmin(1.0f, t) * (b - a);
}

inline float smoothstep01(float t) noexcept {
    return (3.0f - (t + t)) * (t * t);
}

inline float lerpf(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

inline double lerpd(double a, double b, double t) noexcept {
    return a + (b - a) * t;
}

inline int block_to_chunk_coord(int block_coord) noexcept {
    return block_coord >> 4;
}

inline int chunk_to_block_coord(int chunk_coord) noexcept {
    return chunk_coord * 16;
}

// Projects 3D world delta relative to camera into 2D screen space - matches sub_18004AAA0
inline bool world_to_screen(
    const Vec3d& rel_pos,
    Vec2f viewport_size,
    const float modelview[16],
    const float projection[16],
    Vec2f* out_screen
) noexcept {
    const float rx = static_cast<float>(rel_pos.x);
    const float ry = static_cast<float>(rel_pos.y);
    const float rz = static_cast<float>(rel_pos.z);

    const float ex = rx * modelview[0] + ry * modelview[4] + rz * modelview[8] + modelview[12];
    const float ey = rx * modelview[1] + ry * modelview[5] + rz * modelview[9] + modelview[13];
    const float ez = rx * modelview[2] + ry * modelview[6] + rz * modelview[10] + modelview[14];
    const float ew = rx * modelview[3] + ry * modelview[7] + rz * modelview[11] + modelview[15];

    const float cx = ex * projection[0] + ey * projection[4] + ez * projection[8] + ew * projection[12];
    const float cy = ex * projection[1] + ey * projection[5] + ez * projection[9] + ew * projection[13];
    const float cz = ex * projection[2] + ey * projection[6] + ez * projection[10] + ew * projection[14];
    const float cw = ex * projection[3] + ey * projection[7] + ez * projection[11] + ew * projection[15];

    if (cw == 0.0f) {
        return false;
    }
    const float ndc_z = cz / cw;
    if (ndc_z >= 1.0f) {
        return false;
    }
    const float ndc_x = cx / cw;
    const float ndc_y = cy / cw;
    out_screen->x = (ndc_x + 1.0f) * 0.5f * viewport_size.x;
    out_screen->y = (1.0f - (ndc_y + 1.0f) * 0.5f) * viewport_size.y;
    return true;
}

} // namespace slinky::math
