#pragma once
// Visuals -> Misc -> LOCAL: "Force crosshair".
//
// The game hides its own crosshair while scoped (and in a few other states); this
// feature draws a cross that never goes away. It is four short screen-space lines
// around the centre - no game-side state, just a contribution to the per-frame
// VisualRuntime::FxFrame that the render thread draws
// (menu/overlays/fx_overlay.hpp, Tracer).
#include <array>
#include <cmath>
#include <cstdint>

#include "../../../../sdk/visual_runtime_state.hpp"

namespace ForceCrosshair {

struct Settings {
    bool enabled{};
};

namespace Detail {
inline constexpr float kGap = 0.006f;  // gap from the centre, fraction of the height
inline constexpr float kArm = 0.02f;   // arm length, fraction of the height
inline constexpr float kWidth = 1.5f;  // px
inline constexpr std::uint32_t kColor = 0xFFFFFFFFu; // white, like the game's crosshair

// The four arms (left, right, top, bottom), each from `gap` out to `gap + arm` from
// the screen centre.
inline std::array<VisualRuntime::ScreenLine, 4> Arms(float gap = kGap, float arm = kArm,
                                                     float width = kWidth,
                                                     std::uint32_t color = kColor) {
    const float inner = gap;
    const float outer = gap + arm;
    return {{
        {.5f - outer, .5f, .5f - inner, .5f, width, color},
        {.5f + outer, .5f, .5f + inner, .5f, width, color},
        {.5f, .5f - outer, .5f, .5f - inner, width, color},
        {.5f, .5f + outer, .5f, .5f + inner, width, color},
    }};
}
} // namespace Detail

void Publish(const Settings& s);
Settings Snapshot();
// Per frame, from the game thread: contribute the four arms while enabled.
void Contribute(VisualRuntime::FxFrame& frame);
} // namespace ForceCrosshair
