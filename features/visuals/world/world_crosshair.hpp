#pragma once

// Crosshair helpers shared by the cvar/render translation units. Deliberately
// standalone (no ImGui, no drawing headers) because world_cvars.cpp has to see
// the same crosshair_style without pulling the render layer in.
namespace world_visuals {

// Penetration dot: true when a bullet fired straight ahead can pass the wall.
bool wallbang_possible();

// The crosshair the player has configured in game (cl_crosshair* convars). The
// force options draw this instead of a shape of their own.
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

} // namespace world_visuals
