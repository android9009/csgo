#pragma once
// Visuals -> Misc -> OTHER: "Out of FOV" — a screen-edge arrow at every enemy that
// is within range but not currently visible (behind the camera or off the screen).
//
// The game thread contributes one VisualRuntime::ScreenArrow per hidden enemy at
// FRAME_RENDER_START; the render thread draws them (menu/overlays/fx_overlay.hpp,
// EdgeArrow). The walk from the screen centre to the viewport edge is in Detail so
// it can be checked on the host (tests/visuals_misc_screen_test.cpp).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "../../../../sdk/visual_runtime_state.hpp"

namespace OutOfFov {

struct Settings {
    bool enabled{};
    std::uint32_t color{0xFFFFFFFFu}; // "Color"
    float distance{50.f};             // "Distance" (cfg.visuals.out_of_fov_dst), metres
    float size{24.f};                 // "Size" (cfg.visuals.out_of_fov_size), px
};

namespace Detail {
struct Arrow {
    bool valid{};
    float x{}, y{};    // normalized screen position on the viewport edge
    float angle{};     // radians, 0 = right, positive = down (screen axes)
    float size{};      // fraction of the screen height
    std::uint32_t color{};
};

// Walk from the screen centre (0.5, 0.5) towards the normalized point (nx, ny) until
// the ray leaves the [0, 1] viewport. (nx, ny) may already be outside - the walk
// clamps it to the edge while keeping the direction. A point that is still inside
// (both in [0, 1]) means the target is on screen and no arrow is made, as is a
// degenerate point at the exact centre (no direction).
inline Arrow EdgeArrow(float nx, float ny, float size_frac, std::uint32_t color) {
    Arrow a;
    const float dx = nx - .5f;
    const float dy = ny - .5f;
    if (!(dx != 0.f || dy != 0.f)) return a;
    if (nx >= 0.f && nx <= 1.f && ny >= 0.f && ny <= 1.f) return a;
    // Exit parameter per axis: how far along the ray until that axis hits the border.
    constexpr float inf = 1e9f;
    const float tx = dx > 0.f ? .5f / dx : (dx < 0.f ? -.5f / dx : inf);
    const float ty = dy > 0.f ? .5f / dy : (dy < 0.f ? -.5f / dy : inf);
    const float t = (std::min)(tx, ty);
    a.valid = true;
    a.x = .5f + t * dx;
    a.y = .5f + t * dy;
    a.angle = std::atan2(dy, dx);
    a.size = size_frac;
    a.color = color;
    return a;
}

// Screen direction for a target that is behind the view: the target's position in the
// view basis (dot with the view's right and up vectors) collapsed onto the screen
// axes. Exactly behind the camera the direction is undefined, so a fixed edge (right)
// is chosen - an arrow at any edge still reads as "behind you".
inline std::pair<float, float> BehindDir(float right, float up) {
    const float len = std::hypot(right, up);
    if (!(len > 1e-4f)) return {1.f, 0.f};
    return {right / len, -up / len}; // screen y grows downwards
}
} // namespace Detail

void Publish(const Settings& s);
Settings Snapshot();
// Per frame, from the game thread: contribute an edge arrow per hidden in-range enemy.
void Contribute(VisualRuntime::FxFrame& frame);
} // namespace OutOfFov
