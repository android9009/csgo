#include "math.hpp"
#include "../../context.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include <algorithm>
#include <cstring>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD(x) ((x) * (static_cast<float>(M_PI) / 180.f))
#define RAD2DEG(x) ((x) * (180.f / static_cast<float>(M_PI)))

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
void capture_view_matrix(const void* src) {
    // The global holds three 16-byte rows (the client's own code loads exactly anchor
    // +0/+16/+32); row 3 of the full matrix is the constant [0,0,0,1] the client appends.
    std::memcpy(&ctx.view_matrix.m[0], src, 3 * sizeof(ctx.view_matrix.m[0]));
    ctx.view_matrix.m[3][0] = 0.f;
    ctx.view_matrix.m[3][1] = 0.f;
    ctx.view_matrix.m[3][2] = 0.f;
    ctx.view_matrix.m[3][3] = 1.f;
}

bool world_to_screen(const Vector& world, Vector2D& screen) {
    // ctx.view_matrix is a pure affine view transform here (row 3 = [0,0,0,1]; see
    // capture_view_matrix), so the perspective divide has to be done explicitly, the same
    // way the client projects world points: view space is +X right, +Y down, +Z forward,
    // and the horizontal fov is the live player fov (m_iFOV), which is also what keeps
    // this correct while scoped or with the fov changer on.
    const VMatrix& m = ctx.view_matrix;
    const float vx = m.m[0][0] * world.x + m.m[0][1] * world.y + m.m[0][2] * world.z + m.m[0][3];
    const float vy = m.m[1][0] * world.x + m.m[1][1] * world.y + m.m[1][2] * world.z + m.m[1][3];
    const float vz = m.m[2][0] * world.x + m.m[2][1] * world.y + m.m[2][2] * world.z + m.m[2][3];
    if (vz < 0.01f) return false; // behind the near plane
    if (ctx.screen_size.x < 2.f || ctx.screen_size.y < 2.f) return false;
    float fov = 90.f;
    if (ctx.local) {
        const int live = ctx.local->m_iFOV();
        if (live >= 1 && live <= 179) fov = static_cast<float>(live);
    }
    const float aspect = ctx.screen_size.x / ctx.screen_size.y;
    const float scale_x = (ctx.screen_size.x * 0.5f) / std::tan(DEG2RAD(fov) * 0.5f);
    const float scale_y = scale_x / aspect; // the game holds the horizontal fov constant
    screen.x = ctx.screen_size.x * 0.5f + (vx / vz) * scale_x;
    screen.y = ctx.screen_size.y * 0.5f + (vy / vz) * scale_y;
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
