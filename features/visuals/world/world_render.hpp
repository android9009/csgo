#pragma once

// World tab renderers. The World feature owns its own translation units the
// same way chams/ does: item ESP, grenade trajectory and the HUD/crosshair
// layer live next to each other instead of inside esp.cpp.
#include "../esp/esp_draw.hpp" // brings the ImGui math operators first

#include "../dependencies/source.hpp"

namespace world_visuals {

// Bomb / weapons / hostages / grenades + smoke and molotov range circles.
void render_esp(ImDrawList* draw, const visuals_source::frame_data& frame, const ImVec2& display);

// Crosshair conditions, inaccuracy ring and the radar overlay.
void render_hud(ImDrawList* draw, const visuals_source::frame_data& frame, const ImVec2& display);

// Thrown-grenade arc (World > Grenades > Trajectory).
void render_trajectory(ImDrawList* draw, const visuals_source::world_data& entity,
                       const visuals_source::frame_data& frame, const ImVec2& display);

} // namespace world_visuals
