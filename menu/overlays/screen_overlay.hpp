#pragma once
// Visuals -> Misc: the two screen-space elements the module owns - the four "Remove scope"
// lines and the "Spread" circle.
//
// The game-side "Remove scope" detour (UpdatePostScreenEffects) has been removed along with
// the rest of the misc logic, so these lines are plain menu drawing now: they are geometry
// this module computes from the weapon inaccuracy and the fov, not a replacement for
// anything the game no longer suppresses.
//
// The maths lives in VisualsMisc::Detail (portable, tested on the host); this file only turns
// its output into lines. The values arrive through VisualRuntime: `screen_state` (scoped /
// inaccuracy / fov) and `settings.screen` from the menu thread (mode, colour, thickness,
// invert, spread colours).
#include <memory>

#include "esp_drawing.hpp"
#include "../../features/visuals/misc/misc.hpp"

namespace ScreenOverlay {
using namespace evo::gui;
using namespace evo::ren;

namespace {

color Transparent(const color& c) { return c.mod_a(0.f); }

// One of the four scope lines, always from `gap_end` outwards to `far_end`. A gradient line is
// full strength where it leaves the gap and invisible at the far end, so the four lines fade
// into the picture instead of stopping on a hard edge.
void ScopeLine(const std::shared_ptr<layer>& d, const vec2& gap_end, const vec2& far_end,
               const color& c, float thickness, bool gradient) {
    if (!d) return;
    if (gradient) d->add_line_multicolor(gap_end, far_end, c, Transparent(c), thickness);
    else d->add_line(gap_end, far_end, c, thickness);
}

} // namespace

inline void Render() {
    const auto settings = VisualRuntime::GetEffects();
    const auto& style = settings.screen;
    const auto state = VisualRuntime::GetScreen();

    const bool lines = VisualsMisc::Detail::ScopeLines(style.scope_mode);
    if (!lines && !style.spread) return;

    const auto display = draw.display / draw.scale;
    if (display.x <= 1.f || display.y <= 1.f) return;
    const auto& d = draw.layers[l_back];
    if (!d) return;

    const vec2 centre{display.x * 0.5f, display.y * 0.5f};
    const float focal = VisualsMisc::Detail::FocalPixels(display.y, state.fov);

    // The four lines only exist while scoped: un-scoped they vanish, which is what the menu
    // option means by "remove the scope" rather than "always draw a crosshair".
    if (lines && state.scoped && focal > 0.f) {
        const bool dynamic = VisualsMisc::Detail::ScopeDynamicLines(style.scope_mode);
        const float gap = VisualsMisc::Detail::ScopeGapPixels(dynamic, focal, state.inaccuracy);
        const float thickness = VisualsMisc::Detail::ScopeThickness(style.scope_line_size);
        const color line{style.scope_line_color};
        const bool gradient = VisualsMisc::Detail::ScopeGradientLines(style.scope_mode);
        const bool invert = style.scope_invert;

        // `invert` keeps the same four segments but draws them from the centre out to the gap
        // instead of from the gap out to the edge.
        const vec2 left_gap{centre.x - gap, centre.y};
        const vec2 right_gap{centre.x + gap, centre.y};
        const vec2 top_gap{centre.x, centre.y - gap};
        const vec2 bottom_gap{centre.x, centre.y + gap};
        const vec2 left_edge{0.f, centre.y};
        const vec2 right_edge{display.x, centre.y};
        const vec2 top_edge{centre.x, 0.f};
        const vec2 bottom_edge{centre.x, display.y};

        // Two ends per line: the one at the gap (always full colour) and the far one - the
        // screen edge normally, the centre when "Invert" is on.
        ScopeLine(d, left_gap, invert ? centre : left_edge, line, thickness, gradient);
        ScopeLine(d, right_gap, invert ? centre : right_edge, line, thickness, gradient);
        ScopeLine(d, top_gap, invert ? centre : top_edge, line, thickness, gradient);
        ScopeLine(d, bottom_gap, invert ? centre : bottom_edge, line, thickness, gradient);
    }

    // "Spread": the current inaccuracy as a circle. Same focal-length conversion as the lines,
    // so the circle and the dynamic gap always agree. A circle under a pixel is skipped.
    if (style.spread && focal > 0.f) {
        const float radius = VisualsMisc::Detail::SpreadRadiusPixels(focal, state.inaccuracy);
        if (radius >= 1.f) {
            const color first{style.spread_color1};
            const color second{style.spread_color2};
            constexpr int kSegments = 64;
            for (int i = 0; i < kSegments; ++i) {
                const float t0 = static_cast<float>(i) / kSegments;
                const float t1 = static_cast<float>(i + 1) / kSegments;
                const float a0 = t0 * 6.28318530718f;
                const float a1 = t1 * 6.28318530718f;
                const vec2 p0{centre.x + std::cos(a0) * radius, centre.y + std::sin(a0) * radius};
                const vec2 p1{centre.x + std::cos(a1) * radius, centre.y + std::sin(a1) * radius};
                const color edge = color::interpolate(first, second, t0);
                d->add_line(p0, p1, edge, 1.5f);
            }
        }
    }
}
} // namespace ScreenOverlay
