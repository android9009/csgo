#pragma once
#include <algorithm>
#include <cmath>

namespace PreviewFraming {
// Fit a centered box at the current yaw. Source's FOV is horizontal.
// Include the box depth so its nearest corners cannot cross the frame.
inline float Distance(float half_x, float half_y, float half_z, float yaw,
                      float aspect, float horizontal_fov = 12.f) {
    constexpr float radians = 0.01745329252f;
    const float c = std::abs(std::cos(yaw * radians));
    const float s = std::abs(std::sin(yaw * radians));
    const float depth = c * half_x + s * half_y;
    const float horizontal = s * half_x + c * half_y;
    const float tangent = std::tan(horizontal_fov * radians * .5f);
    constexpr float occupancy = .94f;
    return depth + (std::max)(horizontal / tangent, half_z * aspect / tangent) / occupancy;
}
}
