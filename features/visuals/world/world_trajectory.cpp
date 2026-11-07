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
//  Grenade trajectory
// -----------------------------------------------------------------------------
void render_trajectory(ImDrawList* draw, const visuals_source::world_data& entity,
                       const visuals_source::frame_data& frame, const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    constexpr float step = 0.06f;
    const float gravity = 800.f * std::clamp(settings.trajectory_exponent, 0.f, 2.f);
    const int point_count = settings.trajectory_precise ? 60 : 30;
    ImVec2 previous{};
    bool previous_valid = world_to_screen(frame.view_matrix, entity.origin, display, previous);
    for (int index = 1; index <= point_count; ++index) {
        const float time = step * static_cast<float>(index);
        const vector3 point{ entity.origin.x + entity.velocity.x * time,
            entity.origin.y + entity.velocity.y * time,
            entity.origin.z + entity.velocity.z * time - 0.5f * gravity * time * time };
        ImVec2 screen;
        const bool valid = world_to_screen(frame.view_matrix, point, display, screen);
        if (previous_valid && valid) {
            const float pulse = settings.trajectory_shimmer > 1
                ? 0.75f + 0.25f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.f + index * 0.15f)
                : 1.f;
            auto trajectory_color = settings.trajectory_color;
            trajectory_color.a *= pulse * 0.9f;
            draw->AddLine(previous, screen, color(trajectory_color),
                std::clamp(settings.trajectory_thickness, 0.5f, 5.f));
            if (index % 3 == 0)
                draw->AddCircleFilled(screen, std::clamp(settings.trajectory_points_thickness * 0.5f,
                    0.5f, 4.f), color(trajectory_color), 8);
        }
        previous = screen;
        previous_valid = valid;
    }
}


namespace {
constexpr float k_pi = 3.14159265f;
float radians(float degrees) { return degrees * (k_pi / 180.f); }
} // namespace

void render_held_trajectory(ImDrawList* draw, const visuals_source::frame_data& frame,
                            const ImVec2& display) {
    const auto& settings = player_visuals::world_settings();
    visuals_source::local_view_state state{};
    if (!visuals_source::get_local_view_state(state) || !state.alive || !state.holding_grenade)
        return;
    // World > Grenades > Trajectory > Pin pulled only: NL hides the arc until
    // the pin is out, the only moment the throw is actually committed.
    if (settings.trajectory_pin_pulled_only && !state.pin_pulled)
        return;
    if (!frame.has_view_matrix)
        return;

    constexpr float throw_speed = 750.f;   // every CS:GO grenade shares this base toss speed
    constexpr float gravity = 800.f;
    const float gravity_scale = std::clamp(settings.trajectory_exponent, 0.f, 2.f);
    const int point_count = settings.trajectory_precise ? 90 : 45;
    const float step = settings.trajectory_precise ? 0.02f : 0.035f;

    const float pitch = radians(frame.view_angles.x);
    const float yaw = radians(frame.view_angles.y);
    const float pitch_cos = std::cos(pitch);
    visuals_source::vector3 position{ frame.local_origin.x, frame.local_origin.y,
        frame.local_origin.z + 64.f };
    visuals_source::vector3 velocity{ pitch_cos * std::cos(yaw) * throw_speed,
        pitch_cos * std::sin(yaw) * throw_speed, -std::sin(pitch) * throw_speed };
    const float ground = frame.local_origin.z - 4.f;

    auto trajectory_color = settings.trajectory_color;
    const float thickness = std::clamp(settings.trajectory_thickness, 0.5f, 5.f);
    const float point_thickness = std::clamp(settings.trajectory_points_thickness * 0.5f, 0.5f, 4.f);
    bool has_previous = false;
    ImVec2 previous{};
    for (int i = 0; i < point_count; ++i) {
        velocity.z -= gravity * gravity_scale * step;
        position.x += velocity.x * step;
        position.y += velocity.y * step;
        position.z += velocity.z * step;
        // Flat-ground bounce estimate: without a world trace the arc cannot follow
        // walls or slopes, but it still lands where the real throw lands on flat ground.
        if (position.z <= ground) {
            position.z = ground;
            velocity.z = -velocity.z * 0.45f;
            velocity.x *= 0.6f;
            velocity.y *= 0.6f;
            if (std::fabs(velocity.z) < 20.f)
                break;
        }
        ImVec2 screen{};
        if (!world_to_screen(frame.view_matrix, position, display, screen)) {
            has_previous = false;
            continue;
        }
        auto fade = trajectory_color;
        fade.a *= 1.f - static_cast<float>(i) / static_cast<float>(point_count) * 0.55f;
        if (settings.trajectory_shimmer > 1) {
            const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(i) * 0.35f +
                static_cast<float>(ImGui::GetTime()) * (settings.trajectory_shimmer == 3 ? 6.f : 3.f));
            fade.a *= pulse;
        }
        if (has_previous)
            draw->AddLine(previous, screen, color(fade), thickness);
        if (settings.trajectory_precise || i % 2 == 0)
            draw->AddCircleFilled(screen, point_thickness, color(fade), 8);
        previous = screen;
        has_previous = true;
    }
}

} // namespace world_visuals
