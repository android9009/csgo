#include "event_log.hpp"
#include "world_render.hpp"

#include "../dependencies/source.hpp"
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
        draw->AddRectFilled(min_point, max_point, color(0.f, 0.f, 0.f, 0.55f * alpha), 3.f);
        draw->AddRectFilled(min_point, ImVec2(min_point.x + 2.f, max_point.y),
            color(ink.r, ink.g, ink.b, alpha), 1.f);
        draw->AddText(font, text_size, ImVec2(min_point.x + pad + 2.f, y + 3.f),
            color(ink, alpha), it->text.c_str());
        y += 23.f;
    }
}

void render_hud(ImDrawList* draw, const visuals_source::frame_data& frame,
                const ImVec2& display) {
    render_event_logs(draw, display);
    const auto& world = g_cfg->world;
    const auto& page = player_visuals::world_settings();
    // World > Crosshairs: every toggle is a condition on the held weapon (NL
    // draws the overlay only while that condition holds), not a permanent
    // overlay - the old code painted one crosshair for the union of all four.
    visuals_source::local_view_state view_state{};
    const bool view_state_valid = visuals_source::get_local_view_state(view_state);
    const bool scoped = view_state_valid && view_state.scoped;
    const bool penetration_cross = page.penetration_crosshair;
    const bool draw_crosshair = penetration_cross ||
        (page.force_crosshair_scoped && scoped) ||
        (page.force_sniper_crosshair && view_state_valid && view_state.holding_sniper && !scoped) ||
        (page.force_crosshair_c4 && view_state_valid && view_state.holding_c4);
    if (draw_crosshair) {
        const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
        const ImU32 shadow = color(0.f, 0.f, 0.f, 0.9f);
        const ImU32 cross = color(penetration_cross ? page.penetration_color : c_color{ 1.f, 1.f, 1.f, 0.95f });
        draw->AddLine(ImVec2(center.x - 6.f, center.y), ImVec2(center.x - 2.f, center.y), shadow, 3.f);
        draw->AddLine(ImVec2(center.x + 2.f, center.y), ImVec2(center.x + 6.f, center.y), shadow, 3.f);
        draw->AddLine(ImVec2(center.x, center.y - 6.f), ImVec2(center.x, center.y - 2.f), shadow, 3.f);
        draw->AddLine(ImVec2(center.x, center.y + 2.f), ImVec2(center.x, center.y + 6.f), shadow, 3.f);
        draw->AddLine(ImVec2(center.x - 6.f, center.y), ImVec2(center.x - 2.f, center.y), cross, 1.f);
        draw->AddLine(ImVec2(center.x + 2.f, center.y), ImVec2(center.x + 6.f, center.y), cross, 1.f);
        draw->AddLine(ImVec2(center.x, center.y - 6.f), ImVec2(center.x, center.y - 2.f), cross, 1.f);
        draw->AddLine(ImVec2(center.x, center.y + 2.f), ImVec2(center.x, center.y + 6.f), cross, 1.f);
    }

    if (world.m_inaccuracy_overlay && frame.in_game) {
        visuals_source::local_view_state view_state{};
        if (visuals_source::get_local_view_state(view_state, true) && view_state.alive) {
            const float spread = std::clamp(view_state.inaccuracy + view_state.spread, 0.f, 0.15f);
            const float radius = std::clamp(spread * display.y * 2.6f, 2.f, 140.f);
            const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
            auto spread_color = player_visuals::world_settings().inaccuracy_color;
            spread_color.a *= 0.82f;
            draw->AddCircle(center, radius, color(spread_color), 48, 1.25f);
            draw->AddCircle(center, radius + 1.f, color(0.f, 0.f, 0.f, 0.55f), 48, 1.f);
        }
    }

    if (!world.m_draw_radar || !frame.in_game)
        return;
    const ImVec2 origin(display.x - 154.f, 86.f);
    constexpr float size = 130.f;
    const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
    draw->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size), color(0.035f, 0.045f, 0.06f, 0.72f), 3.f);
    draw->AddRect(origin, ImVec2(origin.x + size, origin.y + size), color(0.65f, 0.7f, 0.8f, 0.45f), 3.f);
    draw->AddLine(ImVec2(center.x - size * 0.42f, center.y), ImVec2(center.x + size * 0.42f, center.y), color(0.55f, 0.6f, 0.68f, 0.18f));
    draw->AddLine(ImVec2(center.x, center.y - size * 0.42f), ImVec2(center.x, center.y + size * 0.42f), color(0.55f, 0.6f, 0.68f, 0.18f));
    constexpr float radians = 0.017453292519943295f;
    const float yaw = frame.view_angles.y * radians;
    const float cosine = std::cos(yaw), sine = std::sin(yaw);
    const auto& radar = player_visuals::world_settings().radar_options;
    for (const auto& player : frame.players) {
        if ((player.side == 0 && !radar[0]) || (player.side != 0 && player.side != 1))
            continue;
        const float dx = player.origin.x - frame.local_origin.x;
        const float dy = player.origin.y - frame.local_origin.y;
        const float right = dx * cosine - dy * sine;
        const float forward = dx * sine + dy * cosine;
        const ImVec2 dot(std::clamp(center.x + right * 0.11f, origin.x + 5.f, origin.x + size - 5.f),
            std::clamp(center.y - forward * 0.11f, origin.y + 5.f, origin.y + size - 5.f));
        const ImU32 dot_color = player.side == 0 ? color(1.f, 0.32f, 0.27f, player.alpha)
            : color(0.30f, 0.68f, 1.f, player.alpha);
        draw->AddCircleFilled(dot, player.dormant ? 2.f : 3.f, dot_color, 12);
        if (radar[3]) {
            const ImVec2 direction(dot.x + std::cos(yaw) * 5.f, dot.y + std::sin(yaw) * 5.f);
            draw->AddLine(dot, direction, dot_color, 1.f);
        }
    }
    for (const auto& object : frame.world) {
        if ((object.kind == visuals_source::world_hostage && !radar[1]) ||
            (object.kind == visuals_source::world_planted_c4 && !radar[2]) ||
            (object.kind != visuals_source::world_hostage && object.kind != visuals_source::world_planted_c4))
            continue;
        const float dx = object.origin.x - frame.local_origin.x;
        const float dy = object.origin.y - frame.local_origin.y;
        const float right = dx * cosine - dy * sine;
        const float forward = dx * sine + dy * cosine;
        const ImVec2 dot(std::clamp(center.x + right * 0.11f, origin.x + 5.f, origin.x + size - 5.f),
            std::clamp(center.y - forward * 0.11f, origin.y + 5.f, origin.y + size - 5.f));
        draw->AddCircleFilled(dot, 3.f, object.kind == visuals_source::world_planted_c4
            ? color(1.f, 0.30f, 0.25f, 0.95f) : color(0.45f, 1.f, 0.60f, 0.95f), 12);
    }
    draw->AddTriangleFilled(ImVec2(center.x, center.y - 5.f), ImVec2(center.x - 4.f, center.y + 4.f),
        ImVec2(center.x + 4.f, center.y + 4.f), color(0.96f, 0.96f, 1.f, 0.95f));
    draw_text(draw, ImVec2(origin.x + size * 0.5f, origin.y - 9.f), "RADAR",
        color(0.9f, 0.92f, 0.96f, 0.85f), 0.75f);
}

} // namespace world_visuals
