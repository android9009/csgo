#pragma once

// Shared ESP drawing primitives.
//
// Airflow keeps exactly one implementation of the ink/alpha/text/bar helpers so
// the player ESP (esp.cpp) and the World tab renderers
// (features/visuals/world/*.cpp) draw with identical shadows, alpha handling and
// layout maths instead of each file growing its own copy.
// Same guard as esp.hpp: the ImVec2 operators must be opted into before
// imgui.h is pulled in, otherwise every +/- on an ImVec2 fails to compile.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "imgui_internal.h"

#include "../../../sdk/typedefs/c_color.hpp"
#include "../dependencies/source.hpp"
#include "../settings.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string_view>

namespace visuals_draw {

struct rect_f {
    ImVec2 min{};
    ImVec2 max{};
};

// All elements of one Airflow ESP object share its animated alpha, including
// shadows, bars, flags and hitboxes. The scope keeps preview rendering at 1.0.
inline thread_local float render_alpha = 1.f;
struct alpha_scope {
    float previous;
    explicit alpha_scope(float alpha) : previous(render_alpha) {
        render_alpha = std::clamp(alpha, 0.f, 1.f);
    }
    ~alpha_scope() { render_alpha = previous; }
};

inline ImU32 color(float r, float g, float b, float a = 1.f) {
    return ImGui::GetColorU32(ImVec4(std::clamp(r, 0.f, 1.f), std::clamp(g, 0.f, 1.f),
        std::clamp(b, 0.f, 1.f), std::clamp(a * render_alpha, 0.f, 1.f)));
}

inline ImU32 color(const c_color& value, float alpha = 1.f) {
    return color(value.r, value.g, value.b, value.a * alpha);
}

inline ImU32 color(const player_visuals::esp_color& value, float alpha = 1.f) {
    return color(value.primary, alpha);
}

inline rect_f draw_text(ImDrawList* draw, ImVec2 center, std::string_view text, ImU32 ink,
                        float scale, int alignment = 0) { // 0 center, 1 left, -1 right
    if (text.empty())
        return {};
    ImFont* font = ImGui::GetFont();
    const float font_size = std::max(8.f, 12.f * scale);
    const char* begin = text.data();
    const char* end = begin + text.size();
    const ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, begin, end);
    ImVec2 top_left = center - ImVec2(size.x * 0.5f, size.y * 0.5f);
    if (alignment > 0)
        top_left.x = center.x;
    else if (alignment < 0)
        top_left.x = center.x - size.x;
    const ImU32 shadow = color(0.f, 0.f, 0.f, 0.85f);
    draw->AddText(font, font_size, top_left + ImVec2(1.f, 1.f), shadow, begin, end);
    draw->AddText(font, font_size, top_left, ink, begin, end);
    return { top_left, top_left + size };
}

inline rect_f draw_bar(ImDrawList* draw, const rect_f& rect, float fraction, ImU32 fill, bool vertical) {
    const ImU32 border = color(0.f, 0.f, 0.f, 0.85f);
    draw->AddRectFilled(rect.min - ImVec2(1.f, 1.f), rect.max + ImVec2(1.f, 1.f), border);
    fraction = std::clamp(fraction, 0.f, 1.f);
    if (vertical) {
        const float y = rect.max.y - (rect.max.y - rect.min.y) * fraction;
        draw->AddRectFilled(ImVec2(rect.min.x, y), rect.max, fill);
    } else {
        const float x = rect.min.x + (rect.max.x - rect.min.x) * fraction;
        draw->AddRectFilled(rect.min, ImVec2(x, rect.max.y), fill);
    }
    return { rect.min - ImVec2(2.f, 2.f), rect.max + ImVec2(2.f, 2.f) };
}

inline bool world_to_screen(const float matrix[4][4], const visuals_source::vector3& point,
                            const ImVec2& display, ImVec2& output) {
    const float x = matrix[0][0] * point.x + matrix[0][1] * point.y +
        matrix[0][2] * point.z + matrix[0][3];
    const float y = matrix[1][0] * point.x + matrix[1][1] * point.y +
        matrix[1][2] * point.z + matrix[1][3];
    const float w = matrix[3][0] * point.x + matrix[3][1] * point.y +
        matrix[3][2] * point.z + matrix[3][3];
    if (!std::isfinite(w) || w < 0.001f)
        return false;
    const float inverse = 1.f / w;
    output.x = display.x * 0.5f + x * inverse * display.x * 0.5f;
    output.y = display.y * 0.5f - y * inverse * display.y * 0.5f;
    return std::isfinite(output.x) && std::isfinite(output.y);
}

} // namespace visuals_draw

// Renderers were written against the unprefixed names; pull the helpers in so
// existing call sites stay untouched.
using visuals_draw::alpha_scope;
using visuals_draw::color;
using visuals_draw::draw_bar;
using visuals_draw::draw_text;
using visuals_draw::rect_f;
using visuals_draw::world_to_screen;
