#include "math.hpp"
#include "context.hpp"
#include "sdk/interfaces/interfaces.hpp"
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD(x) ((x) * (M_PI / 180.f))
#define RAD2DEG(x) ((x) * (180.f / M_PI))

namespace Math {
bool Setup() { return true; }
float normalize_yaw(float yaw) {
    while (yaw > 180.f) yaw -= 360.f;
    while (yaw < -180.f) yaw += 360.f;
    return yaw;
}
float normalize_pitch(float pitch) {
    while (pitch > 180.f) pitch -= 360.f;
    while (pitch < -180.f) pitch += 360.f;
    if (pitch > 89.f) pitch -= 180.f;
    if (pitch < -89.f) pitch += 180.f;
    return pitch;
}
QAngle normalize_angles(QAngle a) { a.x = normalize_pitch(a.x); a.y = normalize_yaw(a.y); a.z = 0.f; return a; }
float angle_diff(float src, float dst) { return normalize_yaw(dst - src); }
QAngle calc_angle(const Vector& src, const Vector& dst) {
    Vector d = dst - src;
    float hyp = std::sqrt(d.x * d.x + d.y * d.y);
    return { static_cast<float>(-RAD2DEG(std::atan2(d.z, hyp))), static_cast<float>(RAD2DEG(std::atan2(d.y, d.x))), 0.f };
}
void angle_vectors(const QAngle& ang, Vector* fwd, Vector* right, Vector* up) {
    float sp, sy, cp, cy;
    sp = std::sin(DEG2RAD(ang.x)); sy = std::sin(DEG2RAD(ang.y));
    cp = std::cos(DEG2RAD(ang.x)); cy = std::cos(DEG2RAD(ang.y));
    if (fwd) *fwd = {cp * cy, cp * sy, -sp};
    if (right) *right = {-sy, cy, 0.f};
    if (up) *up = {sp * cy, sp * sy, cp};
}
Vector angle_vector(const QAngle& ang) { Vector v; angle_vectors(ang, &v); return v; }
bool world_to_screen(const Vector& world, Vector2D& screen) {
    float w = ctx.view_matrix.m[3][0] * world.x + ctx.view_matrix.m[3][1] * world.y
            + ctx.view_matrix.m[3][2] * world.z + ctx.view_matrix.m[3][3];
    if (w < 0.01f) return false;
    screen.x = ctx.screen_size.x * 0.5f * (1.f + (ctx.view_matrix.m[0][0] * world.x + ctx.view_matrix.m[0][1] * world.y + ctx.view_matrix.m[0][2] * world.z + ctx.view_matrix.m[0][3]) / w);
    screen.y = ctx.screen_size.y * 0.5f * (1.f - (ctx.view_matrix.m[1][0] * world.x + ctx.view_matrix.m[1][1] * world.y + ctx.view_matrix.m[1][2] * world.z + ctx.view_matrix.m[1][3]) / w);
    return true;
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float clamp(float v, float lo, float hi) { return std::clamp(v, lo, hi); }
int clamp(int v, int lo, int hi) { return std::clamp(v, lo, hi); }
float fov(const QAngle& view, const QAngle& target) {
    QAngle d = normalize_angles(target - view);
    return std::sqrt(d.x * d.x + d.y * d.y);
}
Vector get_eye_pos(void* player) {
    auto* entity = static_cast<CCSPlayer*>(player);
    return entity ? entity->GetEyePosition() : Vector{};
}
} // namespace Math
