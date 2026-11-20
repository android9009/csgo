#pragma once
// Visuals -> Misc -> Other: "Bullet tracers", "Show impacts" and "Hitmarker".
//
// The three features share one pipeline and this header holds its portable half:
//
//   game thread (features/visuals/misc/effects.cpp)
//     game event (bullet_impact / player_hurt)  ->  world-space records
//     every FRAME_RENDER_START                  ->  project the live records, publish them
//   render thread (menu/overlays/fx_overlay.hpp)
//     draw the published screen-space records
//
// Nothing here touches Windows or the SDK, so the geometry, the category rules and the
// lifetimes can be checked on the host (tests/visuals_fx_test.cpp).
#include <array>
#include <cmath>
#include <cstdint>

namespace VisualsFx {
struct Vec3 {
    float x{}, y{}, z{};
};

// The menu's three per-shooter categories: "Local", "Enemy", "Teammates".
enum CategoryIndex { Local = 0, Enemy = 1, Team = 2, CategoryCount = 3 };

// What the menu published. Indexes are CategoryIndex; `style` is 0 for "Laser" and 1 for
// "Line" (the two entries the per-category combo offers).
struct Settings {
    std::array<bool, CategoryCount> tracer{};
    std::array<std::uint32_t, CategoryCount> tracer_color{};
    std::array<int, CategoryCount> tracer_style{};
    bool impacts{};
    std::uint32_t impact_color1{};
    std::uint32_t impact_color2{};
    std::array<bool, 4> hitmarker{}; // indexed by cfg_t::hitmarker_* (world=1, screen=2, kibit=3)
    std::array<std::uint32_t, 4> hitmarker_color{};
    std::array<float, 4> hitmarker_life{};
};

// A tracer that has not been pruned yet: both ends are world positions.
struct Tracer {
    Vec3 start{}, end{};
    int category{Enemy};
    float born{};
};

// One impact point of the local player's own shots ("Show impacts").
struct Impact {
    Vec3 position{};
    float born{};
};

// One hitmarker: where the victim stood when the damage event arrived, which variant to
// draw and how much damage it was for.
struct Hit {
    Vec3 position{};
    int kind{1}; // cfg_t::hitmarker_* (1 world, 2 screen, 3 kibit)
    int amount{};
    float born{};
};

// "Laser" is the thicker of the two styles, "Line" the thin one. Both are drawn by this
// module (the engine's beam interface is not verified for this build), so the difference that
// matters is the width; anything else would be a promise the renderer cannot keep.
inline float TracerWidth(int style) { return style == 1 ? 1.5f : 3.f; }

// Which category a shooter belongs to. `local` wins over the team comparison: the local
// player is on their own team, and "Local" has its own colour.
inline int Category(bool local, bool teammate) { return local ? Local : (teammate ? Team : Enemy); }

// Lifetime of a record, in seconds of `curtime`. A clock that jumped backwards (map change,
// demo seek) makes the record dead instead of immortal.
inline bool Alive(float born, float now, float life) {
    if (!std::isfinite(born) || !std::isfinite(now) || !std::isfinite(life) || life <= 0.f)
        return false;
    const float age = now - born;
    return age >= 0.f && age <= life;
}

// What a screen-space record looks like. `x`/`y` are normalized to the viewport (0..1), the
// same convention the ESP boxes use, so the render thread only multiplies by the display size.
struct ScreenPoint {
    float x{}, y{};
};

struct ScreenSegment {
    float x1{}, y1{}, x2{}, y2{};
};

inline bool ProjectPoint(const std::array<float, 16>& matrix, const Vec3& point, ScreenPoint& out) {
    const float w = matrix[12] * point.x + matrix[13] * point.y + matrix[14] * point.z + matrix[15];
    if (!(w > 0.01f)) return false;
    const float cx = matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + matrix[3];
    const float cy = matrix[4] * point.x + matrix[5] * point.y + matrix[6] * point.z + matrix[7];
    out.x = 0.5f + cx / (2.f * w);
    out.y = 0.5f - cy / (2.f * w);
    return std::isfinite(out.x) && std::isfinite(out.y);
}

// A world segment as a screen segment, clipped against the camera plane. Both ends are
// interpolated in clip space, which keeps the visible part of a tracer that starts at the
// shooter's eye and ends in front of the camera exactly where the engine would draw it.
inline bool ProjectSegment(const std::array<float, 16>& matrix, const Vec3& a, const Vec3& b,
                           ScreenSegment& out) {
    constexpr float kNear = 0.01f;
    auto clip = [&](const Vec3& p, float& cx, float& cy, float& w) {
        cx = matrix[0] * p.x + matrix[1] * p.y + matrix[2] * p.z + matrix[3];
        cy = matrix[4] * p.x + matrix[5] * p.y + matrix[6] * p.z + matrix[7];
        w = matrix[12] * p.x + matrix[13] * p.y + matrix[14] * p.z + matrix[15];
    };
    float ax, ay, aw, bx, by, bw;
    clip(a, ax, ay, aw);
    clip(b, bx, by, bw);
    if (!std::isfinite(aw) || !std::isfinite(bw)) return false;
    if (aw < kNear && bw < kNear) return false; // wholly behind the camera
    if (aw < kNear) {
        const float t = (kNear - aw) / (bw - aw);
        ax = ax + (bx - ax) * t;
        ay = ay + (by - ay) * t;
        aw = kNear;
    } else if (bw < kNear) {
        const float t = (kNear - bw) / (aw - bw);
        bx = bx + (ax - bx) * t;
        by = by + (ay - by) * t;
        bw = kNear;
    }
    out.x1 = 0.5f + ax / (2.f * aw);
    out.y1 = 0.5f - ay / (2.f * aw);
    out.x2 = 0.5f + bx / (2.f * bw);
    out.y2 = 0.5f - by / (2.f * bw);
    return std::isfinite(out.x1) && std::isfinite(out.y1) && std::isfinite(out.x2) &&
           std::isfinite(out.y2);
}

// Pixel size -> the same size as a fraction of the screen height, which is what the published
// records carry. A zero height has no scale, so the caller gets 0 and skips the record.
inline float NormalizedSize(float pixels, float screen_height) {
    if (!std::isfinite(pixels) || !std::isfinite(screen_height) || screen_height <= 1.f) return 0.f;
    return pixels / screen_height;
}
} // namespace VisualsFx
