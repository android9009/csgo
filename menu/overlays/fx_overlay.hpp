#pragma once
// Visuals -> Misc -> Other: the screen half of "Bullet tracers", "Show impacts" and
// "Hitmarker".
//
// The game thread has already projected everything (features/visuals/misc/effects.cpp); this
// file only draws what arrived in `VisualRuntime::fx_frame`. Coordinates are normalized to the
// viewport, the same convention the ESP boxes use.
#include <memory>

#include "esp_drawing.hpp"

namespace FxOverlay {
using namespace evo::gui;
using namespace evo::ren;

namespace {

// A tracer: one line from the shooter's eye to the impact point.
void Tracer(const std::shared_ptr<layer>& d, const VisualRuntime::ScreenLine& line,
            const vec2& display) {
    if (!d) return;
    const color c{line.color};
    if (c.value.a <= 0.f) return;
    d->add_line(vec2{line.x1 * display.x, line.y1 * display.y},
                vec2{line.x2 * display.x, line.y2 * display.y}, c, line.width);
}

// An impact box: a small square in the first colour with the second one on the opposite
// corners, which is what makes it readable against any wall.
void ImpactBox(const std::shared_ptr<layer>& d, const VisualRuntime::ScreenMark& mark,
               const vec2& display) {
    if (!d) return;
    const float half = mark.size * display.y;
    if (half < 1.f) return;
    const vec2 centre{mark.x * display.x, mark.y * display.y};
    const rect box{vec2{centre.x - half, centre.y - half}, vec2{centre.x + half, centre.y + half}};
    const color first{mark.color1};
    const color second{mark.color2};
    d->add_rect(box, first, 1.f);
    // Two short diagonals in the second colour, so a box that lands on a same-coloured wall
    // is still visible.
    d->add_line(vec2{box.tl().x, box.tl().y}, vec2{box.tl().x + half * 0.6f, box.tl().y},
                second, 1.f);
    d->add_line(vec2{box.br().x, box.br().y}, vec2{box.br().x - half * 0.6f, box.br().y},
                second, 1.f);
}

// A hitmarker: four short strokes around the centre, the classic X.
void Hitmarker(const std::shared_ptr<layer>& d, const VisualRuntime::ScreenMark& mark,
               const vec2& display) {
    if (!d) return;
    const float size = (std::max)(mark.size * display.y, 3.f);
    const vec2 centre{mark.x * display.x, mark.y * display.y};
    const color c{mark.color1};
    const float gap = size * 0.45f;
    d->add_line(vec2{centre.x - size, centre.y - size}, vec2{centre.x - gap, centre.y - gap}, c, 1.5f);
    d->add_line(vec2{centre.x + gap, centre.y - gap}, vec2{centre.x + size, centre.y - size}, c, 1.5f);
    d->add_line(vec2{centre.x - size, centre.y + size}, vec2{centre.x - gap, centre.y + gap}, c, 1.5f);
    d->add_line(vec2{centre.x + gap, centre.y + gap}, vec2{centre.x + size, centre.y + size}, c, 1.5f);
}

} // namespace

inline void Render() {
    const auto frame = VisualRuntime::GetFx();
    if (frame.line_count <= 0 && frame.mark_count <= 0) return;
    const auto display = draw.display / draw.scale;
    if (display.x <= 1.f || display.y <= 1.f) return;
    const auto& d = draw.layers[l_back];
    if (!d) return;

    for (int i = 0; i < frame.line_count && i < static_cast<int>(frame.lines.size()); ++i)
        Tracer(d, frame.lines[i], display);
    for (int i = 0; i < frame.mark_count && i < static_cast<int>(frame.marks.size()); ++i) {
        const auto& mark = frame.marks[i];
        if (mark.kind == 0) ImpactBox(d, mark, display);
        else Hitmarker(d, mark, display);
    }
}
} // namespace FxOverlay
