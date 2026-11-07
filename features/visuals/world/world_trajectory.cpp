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

} // namespace world_visuals
