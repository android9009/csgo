#include "event_log.hpp"
#include "world_render.hpp"

#include "../dependencies/source.hpp"
#include "../../../runtime/debug_console.hpp"
#include "../esp/esp_draw.hpp"
#include "../settings.hpp"
#include "../../../menu/weapon_icons.hpp"
#include "../../../sdk/config_system/config_system.hpp"

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace world_visuals {

// -----------------------------------------------------------------------------
//  World HUD (crosshairs, inaccuracy ring, radar)
// -----------------------------------------------------------------------------
// notify.txt-style stack: newest entry on top, right-aligned plate that fades
// out over the configured lifetime, accent bar coloured by category.
void render_event_logs(ImDrawList* draw, const ImVec2& display) {
    const auto& page = player_visuals::world_settings();
    if (!page.window_event_logs)
        return;
    const player_visuals::event_log_filter filter{
        page.event_log_filter[0], page.event_log_filter[1], page.event_log_filter[2] };
    const auto entries = player_visuals::event_log().snapshot(page.event_log_lifetime, 8, filter);
    if (entries.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    ImFont* font = ImGui::GetFont();
    constexpr float text_size = 13.f;
    constexpr float pad = 7.f;
    const float lifetime = std::max(0.5f, page.event_log_lifetime);
    float y = 84.f;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const float age = std::chrono::duration<float>(now - it->timestamp).count();
        float alpha = 1.f;
        if (age > lifetime)
            alpha = std::max(0.f, 1.f - (age - lifetime) / 1.2f);
        if (alpha <= 0.01f)
            continue;
        c_color ink = page.event_log_hit_color;
        if (it->category == player_visuals::event_category::miss)
            ink = page.event_log_miss_color;
        else if (it->category == player_visuals::event_category::evaded)
            ink = page.event_log_evaded_color;
        const ImVec2 measured = font->CalcTextSizeA(text_size, FLT_MAX, 0.f, it->text.c_str());
        const ImVec2 max_point(display.x - 14.f, y + 20.f);
        const ImVec2 min_point(max_point.x - measured.x - pad * 2.f - 3.f, y);
        // World > Windows > style: the plate uses the shared window colour,
        // line opacity and background opacity instead of hard-coded values.
        const float background = std::clamp(page.windows_background_opacity, 0.f, 1.f) * alpha;
        const float line = std::clamp(page.windows_line_opacity, 0.f, 1.f) * alpha;
        draw->AddRectFilled(min_point, max_point,
            color(page.windows_color, background * 0.45f), 3.f);
        draw->AddRectFilled(min_point, ImVec2(min_point.x + 2.f, max_point.y),
            color(page.windows_color, line), 1.f);
        draw->AddText(font, text_size, ImVec2(min_point.x + pad + 2.f, y + 3.f),
            color(ink, alpha), it->text.c_str());
        y += 23.f;
    }
}

// World > HUD > Scope Overlay. Default keeps the engine HudZoom; Lines only
// removes that overlay and leaves the game's own sniper crosshair untouched -
// nothing is painted for it. Custom draws the NL script's four gradient
// segments (offset = gap from the crosshair, position = length).
void render_scope_overlay(ImDrawList* draw, const ImVec2& display) {
    const auto& page = player_visuals::world_settings();
    const int mode = std::clamp(page.scope_overlay_mode, 0, 2);
    if (mode == 0)
        return;
    visuals_source::local_view_state state{};
    if (!visuals_source::get_local_view_state(state) || !state.scoped || !state.alive)
        return;
    const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
    const ImU32 main_color = color(page.scope_color_main);
    const ImU32 edge_color = color(page.scope_color_edge);
    if (mode == 1) {
        // Lines: the engine's scope body stays hidden and only its crosshair lines
        // are drawn - one hairline up, down, left and right of the centre gap.
        constexpr float gap = 4.f;
        draw->AddLine(ImVec2(center.x + gap, center.y), ImVec2(display.x, center.y), main_color, 1.f);
        draw->AddLine(ImVec2(0.f, center.y), ImVec2(center.x - gap, center.y), main_color, 1.f);
        draw->AddLine(ImVec2(center.x, center.y + gap), ImVec2(center.x, display.y), main_color, 1.f);
        draw->AddLine(ImVec2(center.x, 0.f), ImVec2(center.x, center.y - gap), main_color, 1.f);
        return;
    }
    // Custom scope: the Lua scales both sliders by screen height / 1080.
    const float scale = display.y / 1080.f;
    const float offset = std::clamp(page.scope_offset, 0.f, 500.f) * scale;
    const float position = std::max(offset + 1.f, std::clamp(page.scope_position, 0.f, 500.f) * scale);
    const float thickness = std::clamp(page.scope_thickness, 1.f, 10.f);
    const float half = thickness * 0.5f;
    const bool rotated = page.scope_style == 1;
    if (!rotated) {
        // Bottom-left/top-left = far end (edge color), near end = main color.
        draw->AddRectFilledMultiColor(ImVec2(center.x - half, center.y - position),
            ImVec2(center.x + half, center.y - offset),
            edge_color, edge_color, main_color, main_color);
        draw->AddRectFilledMultiColor(ImVec2(center.x - half, center.y + offset),
            ImVec2(center.x + half, center.y + position),
            main_color, main_color, edge_color, edge_color);
        draw->AddRectFilledMultiColor(ImVec2(center.x - position, center.y - half),
            ImVec2(center.x - offset, center.y + half),
            edge_color, main_color, main_color, edge_color);
        draw->AddRectFilledMultiColor(ImVec2(center.x + offset, center.y - half),
            ImVec2(center.x + position, center.y + half),
            main_color, edge_color, edge_color, main_color);
        return;
    }
    // Rotated: the same four segments turned 45 degrees around the crosshair.
    constexpr float diagonal = 0.70710678f;
    auto segment = [&](float dir_x, float dir_y, float perp_x, float perp_y) {
        const ImVec2 near_point(center.x + dir_x * offset, center.y + dir_y * offset);
        const ImVec2 far_point(center.x + dir_x * position, center.y + dir_y * position);
        const ImVec2 near_a(near_point.x + perp_x * half, near_point.y + perp_y * half);
        const ImVec2 near_b(near_point.x - perp_x * half, near_point.y - perp_y * half);
        const ImVec2 far_a(far_point.x + perp_x * half, far_point.y + perp_y * half);
        const ImVec2 far_b(far_point.x - perp_x * half, far_point.y - perp_y * half);
        draw->AddQuadFilled(far_a, near_a, near_b, far_b, main_color);
        draw->AddQuadFilled(far_a, near_a, near_b, far_b, edge_color);
    };
    segment(diagonal, -diagonal, diagonal, diagonal);
    segment(diagonal, diagonal, -diagonal, diagonal);
    segment(-diagonal, diagonal, -diagonal, -diagonal);
    segment(-diagonal, -diagonal, diagonal, -diagonal);
}

void render_hud(ImDrawList* draw, const visuals_source::frame_data& frame,
                const ImVec2& display) {
    AIRFLOW_DEBUG_STAGE("world_hud.render");
    render_windows(draw, frame, display);
    render_hotkeys(draw, display);
    render_spectators(draw, frame, display);
    render_death_notices(draw, display);
    render_scoreboard(draw, frame, display);
    render_event_logs(draw, display);
    const auto& world = g_cfg->world;
    const auto& page = player_visuals::world_settings();
    // World > Crosshairs: every toggle is a condition on the held weapon (NL
    // draws the overlay only while that condition holds), not a permanent
    // overlay - the old code painted one crosshair for the union of all four.
    visuals_source::local_view_state view_state{};
    const bool view_state_valid = visuals_source::get_local_view_state(view_state);
    const bool scoped = view_state_valid && view_state.scoped;
    const bool draw_crosshair =
        (page.force_crosshair_scoped && scoped) ||
        (page.force_sniper_crosshair && view_state_valid && view_state.holding_sniper && !scoped) ||
        (page.force_crosshair_c4 && view_state_valid && view_state.holding_c4);
    if (draw_crosshair) {
        // These options force the crosshair the player has configured in game, so
        // its size, gap, thickness and colour come from the cl_crosshair* convars
        // instead of a shape of our own.
        const auto style = world_visuals::player_crosshair();
        const float unit = display.y / 1080.f;
        const float thickness = std::max(0.5f, style.thickness) * unit;
        const float size = std::max(1.f, style.size) * unit;
        const float gap = std::max(0.f, size + style.gap * unit);
        const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
        const ImU32 cross = color(style.r, style.g, style.b, 1.f);
        const ImU32 outline = color(0.f, 0.f, 0.f, 0.9f);
        const ImVec2 arms[4][2] = {
            { ImVec2(center.x - gap - size, center.y), ImVec2(center.x - gap, center.y) },
            { ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + size, center.y) },
            { ImVec2(center.x, center.y - gap - size), ImVec2(center.x, center.y - gap) },
            { ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + size) },
        };
        for (const auto& arm : arms) {
            draw->AddLine(arm[0], arm[1], outline, thickness + 2.f * unit);
            draw->AddLine(arm[0], arm[1], cross, thickness);
        }
        if (style.dot) {
            const float dot = std::max(1.f, thickness);
            draw->AddRectFilled(ImVec2(center.x - dot, center.y - dot),
                ImVec2(center.x + dot, center.y + dot), cross);
        }
    }
    // Penetration crosshair, exactly like Fatality: a 3x3 square on the screen
    // centre whose colour says whether the wall ahead can be shot through.
    if (page.penetration_crosshair && frame.in_game && view_state_valid && view_state.alive) {
        const auto& colors = player_visuals::world_settings();
        const ImU32 dot = color(world_visuals::wallbang_possible() ? colors.penetration_color
                                                                   : colors.penetration_color_blocked);
        const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
        draw->AddRectFilled(ImVec2(center.x - 1.f, center.y - 1.f), ImVec2(center.x + 2.f, center.y + 2.f), dot);
    }

    if (world.m_inaccuracy_overlay && frame.in_game) {
        visuals_source::local_view_state view_state{};
        if (visuals_source::get_local_view_state(view_state, true) && view_state.alive) {
            // Fatality's spread circle: a filled disc, radius = cone * screen height
            // * 0.7 (never below 0.4 % of the cone), with the inner colour in the
            // middle fading to the outer colour at the rim.
            float cone = std::clamp(view_state.inaccuracy + view_state.spread, 0.f, 0.15f);
            if (cone < 0.004f)
                cone = 0.004f;
            const float radius = std::clamp(cone * display.y * 0.7f, 1.f, display.y * 0.6f);
            const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
            const auto& colors = player_visuals::world_settings();
            constexpr int steps = 20;
            for (int step = steps; step >= 1; --step) {
                const float t = static_cast<float>(step) / static_cast<float>(steps);
                const float inner = 1.f - t;
                auto mixed = colors.inaccuracy_color;
                mixed.r = colors.inaccuracy_color.r * inner + colors.inaccuracy_color_secondary.r * t;
                mixed.g = colors.inaccuracy_color.g * inner + colors.inaccuracy_color_secondary.g * t;
                mixed.b = colors.inaccuracy_color.b * inner + colors.inaccuracy_color_secondary.b * t;
                mixed.a = colors.inaccuracy_color.a * inner + colors.inaccuracy_color_secondary.a * t;
                draw->AddCircleFilled(center, radius * t, color(mixed), 48);
            }
        }
    }

    // World > HUD > Radar no longer draws its own map: the reveal options set
    // the engine's spotted flags (visuals_source::update_radar_reveal), so the
    // normal game radar shows the enemies.
    render_scope_overlay(draw, display);
}

} // namespace world_visuals
