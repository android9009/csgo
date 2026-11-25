#pragma once
// Visuals -> Misc -> Other: the screen half of "Bullet tracers", "Show impacts", "Hitmarker"
// and "Out of FOV".
//
// The game-thread half of the pipeline has been removed, so `VisualRuntime::fx_frame`
// stays empty; this file only draws whatever arrives there. Coordinates are normalized to
// the viewport, the same convention the ESP boxes use.
#include <algorithm>
#include <cmath>
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

// "Out of FOV": a triangle at the screen edge, pointing at the player it belongs to. The base
// of the triangle is perpendicular to the direction, so it reads as an arrow instead of a dot.
void EdgeArrow(const std::shared_ptr<layer>& d, const VisualRuntime::ScreenArrow& arrow,
               const vec2& display) {
    if (!d) return;
    const float size = (std::max)(arrow.size * display.y, 4.f);
    const vec2 tip{arrow.x * display.x, arrow.y * display.y};
    const float dir_x = std::cos(arrow.angle);
    const float dir_y = std::sin(arrow.angle);
    // Tip on the edge position, the two base corners behind it.
    const vec2 base{tip.x - dir_x * size, tip.y - dir_y * size};
    const float half = size * 0.55f;
    const vec2 left{base.x - dir_y * half, base.y + dir_x * half};
    const vec2 right{base.x + dir_y * half, base.y - dir_x * half};
    d->add_triangle_filled(tip, left, right, color{arrow.color});
}

// Grenade trajectory marker (mark kind 3): a white ring with the core colour, like the
// collision dots the Lua script draws on the predicted path.
void TrajectoryMark(const std::shared_ptr<layer>& d, const VisualRuntime::ScreenMark& mark,
                    const vec2& display) {
    if (!d) return;
    const vec2 centre{mark.x * display.x, mark.y * display.y};
    const float size = (std::max)(mark.size * display.y, 2.f);
    const float alpha = (std::max)((std::min)(mark.alpha, 1.f), 0.f);
    if (alpha <= 0.01f) return;
    const color white{1.f, 1.f, 1.f, alpha};
    const color core{mark.color2};
    d->add_circle(centre, size, white, 20, 1.f, 1.5f);
    d->add_circle_filled(centre, size * 0.45f, core, 16);
}

// Grenade proximity warning disc: radial glow (colour fading to transparent), a soft
// ring and the weapon silhouette (grenade or molotov), the Lua's fallback icons.
void WarningDisc(const std::shared_ptr<layer>& d, const VisualRuntime::ScreenDisc& disc,
                 const vec2& display) {
    if (!d) return;
    const float alpha = (std::max)((std::min)(disc.alpha, 1.f), 0.f);
    if (alpha <= 0.005f) return;
    const vec2 centre{disc.x * display.x, disc.y * display.y};
    const float radius = (std::max)(disc.radius * display.y, 6.f);
    const float glow = (std::max)(disc.glow * display.y, radius);
    const color base{disc.color};
    // Glow: the base colour fading to transparent at the edge.
    d->add_circle_filled_multicolor(centre, glow,
                                    {color{base.value.r, base.value.g, base.value.b, alpha * 0.81f},
                                     color{base.value.r, base.value.g, base.value.b, 0.f}},
                                    32);
    // Ring at the warning radius (the Lua's no-texture fallback outline).
    d->add_circle(centre, radius, color{base.value.r, base.value.g, base.value.b, alpha * 0.55f}, 28, 1.f, 1.f);

    const float r = radius;
    auto white = color{1.f, 1.f, 1.f, alpha};
    auto shadow = color{0.f, 0.f, 0.f, alpha * 0.75f};
    auto body = [&](const vec2& at, float radius_px) {
        d->add_circle_filled(at + vec2{1.f, 1.f}, radius_px, shadow, 20);
        d->add_circle_filled(at, radius_px, white, 20);
    };
    auto box = [&](const vec2& tl, const vec2& br) {
        d->add_rect_filled(rect{tl + vec2{1.f, 1.f}, br + vec2{1.f, 1.f}}, shadow);
        d->add_rect_filled(rect{tl, br}, white);
    };
    auto tri = [&](const vec2& p0, const vec2& p1, const vec2& p2) {
        d->add_triangle_filled(p0 + vec2{1.f, 1.f}, p1 + vec2{1.f, 1.f}, p2 + vec2{1.f, 1.f}, shadow);
        d->add_triangle_filled(p0, p1, p2, white);
    };
    if (disc.icon == 1) {
        // Grenade (also used for the flash): body, cap, pin and the little fuse stroke.
        body({centre.x, centre.y + r * 0.22f}, r * 0.42f);
        box({centre.x - r * 0.24f, centre.y - r * 0.62f}, {centre.x + r * 0.24f, centre.y - r * 0.34f});
        box({centre.x + r * 0.18f, centre.y - r * 0.55f}, {centre.x + r * 0.6f, centre.y - r * 0.4f});
        d->add_line({centre.x + r * 0.55f, centre.y - r * 0.38f}, {centre.x + r * 0.7f, centre.y - r * 0.05f}, white, 1.f);
    } else if (disc.icon == 2) {
        // Molotov / fire: bottle silhouette with the spout.
        tri({centre.x, centre.y - r * 0.7f}, {centre.x - r * 0.5f, centre.y + r * 0.3f},
            {centre.x + r * 0.45f, centre.y + r * 0.3f});
        body({centre.x, centre.y + r * 0.15f}, r * 0.45f);
        tri({centre.x + r * 0.35f, centre.y - r * 0.35f}, {centre.x, centre.y + r * 0.3f},
            {centre.x + r * 0.55f, centre.y + r * 0.3f});
    }
}

// A floating label: centred text with a one-pixel shadow, the damage / fuse / distance
// lines of the grenade warning.
void Label(const std::shared_ptr<layer>& d, const VisualRuntime::ScreenText& text,
           const vec2& display) {
    if (!d) return;
    const float alpha = (std::max)((std::min)(text.alpha, 1.f), 0.f);
    if (alpha <= 0.005f) return;
    if (text.text[0] == 0) return;
    const vec2 p{text.x * display.x, text.y * display.y};
    d->add_text(p + vec2{1.f, 1.f}, text.text, color{0.f, 0.f, 0.f, alpha * 0.85f},
                text_params::with_h(align_center));
    d->add_text(p, text.text, color{text.color}, text_params::with_h(align_center));
}

} // namespace

inline void Render() {
    const auto frame = VisualRuntime::GetFx();
    if (frame.line_count <= 0 && frame.mark_count <= 0 && frame.arrow_count <= 0 &&
        frame.disc_count <= 0 && frame.text_count <= 0)
        return;
    const auto display = draw.display / draw.scale;
    if (display.x <= 1.f || display.y <= 1.f) return;
    const auto& d = draw.layers[l_back];
    if (!d) return;

    for (int i = 0; i < frame.line_count && i < static_cast<int>(frame.lines.size()); ++i)
        Tracer(d, frame.lines[i], display);
    for (int i = 0; i < frame.mark_count && i < static_cast<int>(frame.marks.size()); ++i) {
        const auto& mark = frame.marks[i];
        if (mark.kind == 0) ImpactBox(d, mark, display);
        else if (mark.kind == 3) TrajectoryMark(d, mark, display);
        else Hitmarker(d, mark, display);
    }
    for (int i = 0; i < frame.arrow_count && i < static_cast<int>(frame.arrows.size()); ++i)
        EdgeArrow(d, frame.arrows[i], display);
    // The warning disc glows under its labels, so it goes first.
    for (int i = 0; i < frame.disc_count && i < static_cast<int>(frame.discs.size()); ++i)
        WarningDisc(d, frame.discs[i], display);
    for (int i = 0; i < frame.text_count && i < static_cast<int>(frame.texts.size()); ++i)
        Label(d, frame.texts[i], display);
}
} // namespace FxOverlay
