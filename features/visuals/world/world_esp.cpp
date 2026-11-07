#include "world_render.hpp"

#include "../dependencies/source.hpp"
#include "../esp/esp_draw.hpp"
#include "../settings.hpp"
#include "../../../menu/weapon_icons.hpp"
#include "../../../sdk/config_system/config_system.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace world_visuals {
using visuals_source::world_data;
using visuals_source::vector3;

// -----------------------------------------------------------------------------
//  World ESP (bomb / weapons / hostages / grenades)
// -----------------------------------------------------------------------------
bool world_box(const visuals_source::frame_data& frame, const world_data& entity,
               const ImVec2& display, rect_f& box) {
    bool initialized = false;
    for (int corner = 0; corner < 8; ++corner) {
        const vector3 point{
            entity.origin.x + ((corner & 1) ? entity.maxs.x : entity.mins.x),
            entity.origin.y + ((corner & 2) ? entity.maxs.y : entity.mins.y),
            entity.origin.z + ((corner & 4) ? entity.maxs.z : entity.mins.z)
        };
        ImVec2 screen;
        if (!world_to_screen(frame.view_matrix, point, display, screen))
            continue;
        if (!initialized) {
            box.min = box.max = screen;
            initialized = true;
        } else {
            box.min.x = std::min(box.min.x, screen.x);
            box.min.y = std::min(box.min.y, screen.y);
            box.max.x = std::max(box.max.x, screen.x);
            box.max.y = std::max(box.max.y, screen.y);
        }
    }
    return initialized && box.max.x - box.min.x > 1.f && box.max.y - box.min.y > 1.f &&
        box.max.x >= 0.f && box.min.x <= display.x && box.max.y >= 0.f && box.min.y <= display.y;
}

void render_world_range(ImDrawList* draw, const world_data& entity,
                        const visuals_source::frame_data& frame, const ImVec2& display,
                        const c_color& range_color, float duration) {
    if (entity.range <= 0.f)
        return;
    alpha_scope alpha_state(entity.alpha);
    constexpr float pi = 3.14159265358979323846f;
    ImVec2 points[61];
    int point_count = 0;
    for (int i = 0; i <= 60; ++i) {
        const float angle = static_cast<float>(i) * (2.f * pi / 60.f);
        const vector3 point{ entity.origin.x + std::sin(angle) * entity.range,
            entity.origin.y + std::cos(angle) * entity.range, entity.origin.z };
        if (world_to_screen(frame.view_matrix, point, display, points[point_count]))
            ++point_count;
    }
    if (point_count > 2)
        draw->AddPolyline(points, point_count, color(range_color, 0.85f), false, 2.f);

    ImVec2 origin;
    if (!world_to_screen(frame.view_matrix, entity.origin, display, origin))
        return;
    if (entity.time_remaining >= 0.f && duration > 0.f) {
        constexpr float timer_width = 60.f;
        const float fraction = std::clamp(entity.time_remaining / duration, 0.f, 1.f);
        const float ease = std::sin(static_cast<float>(ImGui::GetTime()) * 2.f * pi) * 5.f;
        const rect_f outline{ ImVec2(origin.x - timer_width * 0.5f - 1.f, origin.y - 3.f - ease),
            ImVec2(origin.x + timer_width * 0.5f + 1.f, origin.y + 1.f - ease) };
        draw->AddRectFilled(outline.min, outline.max, color(0.f, 0.f, 0.f, 0.6f));
        draw->AddRectFilled(ImVec2(origin.x - timer_width * 0.5f, origin.y - 2.f - ease),
            ImVec2(origin.x - timer_width * 0.5f + timer_width * fraction, origin.y - 0.f - ease),
            color(range_color));
        char timer[24];
        std::snprintf(timer, sizeof(timer), "%.1f", std::max(0.f, entity.time_remaining));
        draw_text(draw, ImVec2(origin.x, origin.y - 18.f - ease), timer,
            color(1.f, 1.f, 1.f, 0.9f), 0.85f);
    }
}

void render_esp(ImDrawList* draw, const visuals_source::frame_data& frame,
                const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    if (settings.grenade_trajectory)
        render_held_trajectory(draw, frame, display);
    for (const auto& entity : frame.world) {
        if (entity.kind == visuals_source::world_projectile) {
            // Fatality draws a live grenade's remaining path from its own velocity, which the
            // prediction does in world_trajectory.cpp now.
            if (settings.grenade_trajectory || settings.grenade_proximity_warnings)
                render_trajectory(draw, entity, frame, display);
        }
        if (entity.kind == visuals_source::world_smoke) {
            if (settings.grenade.smoke_radius)
                render_world_range(draw, entity, frame, display, settings.grenade.secondary_color, 18.f);
            if (settings.grenade.colorize_smoke)
                render_world_range(draw, entity, frame, display, settings.smoke_color, 18.f);
            continue;
        }
        if (entity.kind == visuals_source::world_inferno) {
            if (settings.grenade.inferno_radius)
                render_world_range(draw, entity, frame, display, settings.grenade.progress_background, 7.03125f);
            if (settings.grenade.colorize_molotov)
                render_world_range(draw, entity, frame, display, settings.molotov_color, 7.03125f);
            continue;
        }
        const player_visuals::world_item_settings* item = nullptr;
        if (entity.kind == visuals_source::world_planted_c4) item = &settings.bomb;
        else if (entity.kind == visuals_source::world_weapon) item = &settings.weapon;
        else if (entity.kind == visuals_source::world_hostage) item = &settings.hostage;
        else if (entity.kind == visuals_source::world_projectile) item = &settings.grenade;
        if (!item || !item->enabled)
            continue;

        rect_f box;
        if (!world_box(frame, entity, display, box))
            continue;
        alpha_scope alpha_state(entity.alpha);
        c_color item_color = item->color;
        if (entity.kind == visuals_source::world_projectile && settings.grenade.projectile_inferno)
            item_color = settings.grenade.glow_color;
        if (entity.kind == visuals_source::world_projectile && settings.grenade.colorize_explosion)
            item_color = settings.explosion_color;
        if (item->glow || item->colorize_effects || settings.grenade.projectile_inferno)
            draw->AddRect(box.min - ImVec2(3.f, 3.f), box.max + ImVec2(3.f, 3.f),
                color(item->colorize_effects ? settings.effects_color : item_color), 2.f, 0, 3.f);
        if (item->box) {
            draw->AddRect(box.min - ImVec2(1.f, 1.f), box.max + ImVec2(1.f, 1.f),
                color(0.f, 0.f, 0.f, 0.8f));
            draw->AddRect(box.min + ImVec2(1.f, 1.f), box.max - ImVec2(1.f, 1.f),
                color(0.f, 0.f, 0.f, 0.8f));
            // World > World ESP > Colors: the box/name/icon/ammo inks are their
            // own settings; item_color still drives glow and the progress bar.
            draw->AddRect(box.min, box.max, color(settings.box_color));
        }

        float bottom = box.max.y + 4.f;
        if (item->progress_bar && entity.clip >= 0 && entity.max_clip > 0) {
            const rect_f bar{ ImVec2(box.min.x, bottom), ImVec2(box.max.x, bottom + 2.f) };
            const float fraction = std::clamp(static_cast<float>(entity.clip) / entity.max_clip,
                0.f, 1.f);
            draw_bar(draw, bar, fraction, color(item_color), false);
            if (entity.clip < entity.max_clip) {
                char ammo[16];
                std::snprintf(ammo, sizeof(ammo), "%d", entity.clip);
                draw_text(draw, ImVec2(bar.min.x + (bar.max.x - bar.min.x) * fraction,
                    bar.min.y - 5.f), ammo, color(settings.ammo_color), 0.8f);
            }
            bottom += 8.f;
        }
        if (item->text && !entity.name.empty()) {
            draw_text(draw, ImVec2((box.min.x + box.max.x) * 0.5f, bottom + 4.f),
                entity.name, color(settings.name_color), 0.9f);
            bottom += 13.f;
        }
        if (item->distance) {
            char distance_text[32];
            std::snprintf(distance_text, sizeof(distance_text), "%dm", static_cast<int>(entity.distance * 0.0254f));
            draw_text(draw, ImVec2((box.min.x + box.max.x) * 0.5f, bottom + 4.f),
                distance_text, color(item_color), 0.8f);
            bottom += 11.f;
        }
        if (item->time_remaining && entity.time_remaining >= 0.f) {
            char remaining[24];
            std::snprintf(remaining, sizeof(remaining), "%.1fs", std::max(0.f, entity.time_remaining));
            draw_text(draw, ImVec2((box.min.x + box.max.x) * 0.5f, bottom + 4.f),
                remaining, color(item_color), 0.8f);
            bottom += 11.f;
        }
        if ((entity.kind == visuals_source::world_planted_c4 &&
                (item->bomb_indicator || item->defuse_indicator || item->defuser)) ||
            (entity.kind == visuals_source::world_hostage && item->rescue_indicator)) {
            const char* indicator = entity.kind == visuals_source::world_hostage ? "RESCUE" :
                (item->defuse_indicator ? "DEFUSE" : (item->defuser ? "DEFUSER" : "BOMB"));
            draw_text(draw, ImVec2((box.min.x + box.max.x) * 0.5f, bottom + 4.f),
                indicator, color(item->secondary_color), 0.8f);
            bottom += 11.f;
        }
        if (item->icon) {
            const int atlas = visuals_source::atlas_weapon_id(entity.weapon_id);
            if (atlas >= 0) {
                const ImVec2 size(48.f, 18.f);
                weapon_icons::draw(draw, atlas,
                    ImVec2((box.min.x + box.max.x) * 0.5f, bottom + 7.f), size,
                    color(settings.icon_color));
            }
        }
    }
    render_grenade_warning(draw, frame, display);
}

} // namespace world_visuals
