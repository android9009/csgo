#pragma once

#include <cmath>
#include <cstddef>

namespace misc_runtime {
struct vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    vec3 operator+(const vec3& other) const { return { x + other.x, y + other.y, z + other.z }; }
    vec3 operator-(const vec3& other) const { return { x - other.x, y - other.y, z - other.z }; }
    vec3 operator*(float scale) const { return { x * scale, y * scale, z * scale }; }
    vec3 operator/(float scale) const { return { x / scale, y / scale, z / scale }; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float length_2d() const { return std::sqrt(x * x + y * y); }
};

struct alignas(16) vec3_aligned {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 0.f;
};

struct user_cmd {
    void* vtable;
    int command_number;
    int tick_count;
    vec3 viewangles;
    vec3 aimdirection;
    float forwardmove;
    float sidemove;
    float upmove;
    int buttons;
    int impulse;
    int weapon_select;
    int weapon_sub_type;
    int random_seed;
    short mouse_dx;
    short mouse_dy;
    bool has_been_predicted;
};

#if defined(_WIN32) && !defined(_WIN64)
static_assert(offsetof(user_cmd, buttons) == 0x30);
static_assert(offsetof(user_cmd, weapon_select) == 0x38);
static_assert(offsetof(user_cmd, mouse_dx) == 0x44);
static_assert(offsetof(user_cmd, has_been_predicted) == 0x48);
#endif

constexpr int in_attack = 1 << 0;
constexpr int in_attack2 = 1 << 11;
constexpr int in_jump = 1 << 1;
constexpr int in_duck = 1 << 2;
constexpr int in_forward = 1 << 3;
constexpr int in_back = 1 << 4;
constexpr int in_moveleft = 1 << 9;
constexpr int in_moveright = 1 << 10;
constexpr int in_speed = 1 << 17;
constexpr int fl_on_ground = 1 << 0;
constexpr int fl_ducking = 1 << 1;
constexpr float pi = 3.14159265358979323846f;

inline float normalize_yaw(float yaw) {
    while (yaw > 180.f)
        yaw -= 360.f;
    while (yaw < -180.f)
        yaw += 360.f;
    return yaw;
}

inline vec3 angle_vectors(const vec3& angles) {
    const float pitch = angles.x * pi / 180.f;
    const float yaw = angles.y * pi / 180.f;
    const float cp = std::cos(pitch);
    return { cp * std::cos(yaw), cp * std::sin(yaw), -std::sin(pitch) };
}
} // namespace misc_runtime
