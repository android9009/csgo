#include "dependencies/gameplay.hpp"

#include <algorithm>
#include <cmath>

namespace misc_gameplay {
using namespace misc_runtime;

vec3 vector_angles(const vec3& direction) {
    const float flat = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (flat <= 0.000001f)
        return { direction.z > 0.f ? -90.f : 90.f, 0.f, 0.f };
    return { -std::atan2(direction.z, flat) * 180.f / pi,
        std::atan2(direction.y, direction.x) * 180.f / pi, 0.f };
}

vec3 normalized(const vec3& value) {
    const float length = value.length();
    return length > 0.000001f ? value / length : vec3{};
}

float dot(const vec3& a, const vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
vec3 ray_circle_intersection(const vec3& ray, const vec3& center, float radius) {
    const float projection = ray.x * center.x + ray.y * center.y;
    const float c = center.x * center.x + center.y * center.y - radius * radius;
    const float discriminant = 4.f * (projection * projection - c);
    if (discriminant < 0.f) {
        vec3 nearest = ray * projection;
        vec3 difference = { nearest.x - center.x, nearest.y - center.y, 0.f };
        const float length = difference.length_2d();
        if (length <= 0.000001f)
            difference = { -ray.y, ray.x, 0.f };
        else
            difference = difference / length;
        return { center.x + difference.x * radius, center.y + difference.y * radius, 0.f };
    }
    const float root = std::sqrt(discriminant);
    const float t = projection + root * 0.5f;
    return { ray.x * t, ray.y * t, 0.f };
}

float calculate_throw_yaw(const vec3& wish_direction, const vec3& velocity,
                          float throw_velocity, float strength) {
    vec3 direction = { wish_direction.x, wish_direction.y, 0.f };
    const float horizontal = direction.length_2d();
    const float total = wish_direction.length();
    if (horizontal <= 0.000001f || total <= 0.000001f)
        return std::atan2(wish_direction.y, wish_direction.x) * 180.f / pi;
    direction = direction / horizontal;
    const float cos_pitch = std::clamp(dot(direction, wish_direction) / total, 0.f, 1.f);
    const float launch_speed = std::clamp(throw_velocity * 0.9f, 15.f, 750.f) *
        (std::clamp(strength, 0.f, 1.f) * 0.7f + 0.3f);
    const vec3 inherited = velocity * 1.25f;
    const vec3 intersection = ray_circle_intersection(direction, inherited, launch_speed * cos_pitch);
    const vec3 relative = intersection - inherited;
    return std::atan2(relative.y, relative.x) * 180.f / pi;
}

float calculate_throw_pitch(const vec3& wish_direction, const vec3& velocity,
                            float throw_velocity, float strength) {
    const float launch_speed = std::clamp(throw_velocity * 0.9f, 15.f, 750.f) *
        (std::clamp(strength, 0.f, 1.f) * 0.7f + 0.3f);
    const vec3 current_velocity = velocity * 1.25f + wish_direction * launch_speed;
    const float difference = vector_angles(wish_direction).x - vector_angles(current_velocity).x;
    return difference * (std::cos(difference * pi / 180.f) + 1.f) * 0.5f;
}

bool compensate_grenade_throw(user_cmd* cmd, const vec3& velocity,
                              const vec3& previous_velocity, float throw_velocity,
                              float strength) {
    if (throw_velocity <= 0.f || !std::isfinite(strength) ||
        !std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z) ||
        !std::isfinite(previous_velocity.x) || !std::isfinite(previous_velocity.y) ||
        !std::isfinite(previous_velocity.z)) {
        return false;
    }
    const vec3 smoothed_velocity = (velocity + previous_velocity) * 0.5f;
    const vec3 input_angles = cmd->viewangles;
    const vec3 direction = angle_vectors(input_angles);
    const float launch_speed = std::clamp(throw_velocity * 0.9f, 15.f, 750.f) *
        (std::clamp(strength, 0.f, 1.f) * 0.7f + 0.3f);
    const vec3 base_velocity = direction * launch_speed;
    const vec3 current_throw_velocity = velocity * 1.25f + base_velocity;
    vec3 target_velocity = normalized(base_velocity + smoothed_velocity * 1.25f);
    if (dot(current_throw_velocity, direction) > 0.f)
        target_velocity = direction;
    if (target_velocity.length() <= 0.000001f)
        return false;

    vec3 adjusted_angles = input_angles;
    adjusted_angles.x += calculate_throw_pitch(direction, smoothed_velocity, throw_velocity, strength);
    adjusted_angles.y = calculate_throw_yaw(target_velocity, previous_velocity, throw_velocity, strength);
    if (!std::isfinite(adjusted_angles.x) || !std::isfinite(adjusted_angles.y))
        return false;
    cmd->viewangles.x = adjusted_angles.x;
    cmd->viewangles.y = normalize_yaw(adjusted_angles.y);
    return true;
}

} // namespace misc_gameplay
