#pragma once

// World tab renderers. The World feature owns its own translation units the
// same way chams/ does: item ESP, grenade trajectory and the HUD/crosshair
// layer live next to each other instead of inside esp.cpp.
#include "../esp/esp_draw.hpp" // brings the ImGui math operators first

#include "../dependencies/source.hpp"

#include <string>

namespace world_visuals {

// HUD panel filter (removals). Called from the single PaintTraverse hook.
bool should_skip_panel(const char* name);

// main's thirdperson camera, driven from the OverrideView hook.
void run_thirdperson();

// Penetration dot: true when a bullet fired straight ahead can pass the wall.
bool wallbang_possible();

// The crosshair the player uses in game (cl_crosshair* convars), drawn by the
// force options instead of a custom shape.
struct crosshair_style {
    bool valid = false;
    float gap = 0.f;
    float size = 4.f;
    float thickness = 0.5f;
    bool dot = false;
    float r = 0.f;
    float g = 1.f;
    float b = 0.f;
};

crosshair_style player_crosshair();

// Bomb / weapons / hostages / grenades + smoke and molotov range circles.
void render_esp(ImDrawList* draw, const visuals_source::frame_data& frame, const ImVec2& display);

// Crosshair conditions, inaccuracy ring and the radar overlay.
void render_hud(ImDrawList* draw, const visuals_source::frame_data& frame, const ImVec2& display);

// Thrown-grenade arc (World > Grenades > Trajectory).
void render_trajectory(ImDrawList* draw, const visuals_source::world_data& entity,
                       const visuals_source::frame_data& frame, const ImVec2& display);

// Predicted arc of the grenade currently in hand (NL style). Gated by
// World > Grenades > Trajectory and, optionally, by the pulled pin.
void render_windows(ImDrawList* draw, const visuals_source::frame_data& frame,
                    const ImVec2& display);

void render_scoreboard(ImDrawList* draw, const visuals_source::frame_data& frame,
                       const ImVec2& display);

// World > HUD > Death notices. Fed from the player_death event.
void push_death_notice(std::string text, bool assistant, bool by_local);
void render_death_notices(ImDrawList* draw, const ImVec2& display);

void render_spectators(ImDrawList* draw, const visuals_source::frame_data& frame,
                       const ImVec2& display);

void render_hotkeys(ImDrawList* draw, const ImVec2& display);

void render_held_trajectory(ImDrawList* draw, const visuals_source::frame_data& frame,
                            const ImVec2& display);

} // namespace world_visuals
