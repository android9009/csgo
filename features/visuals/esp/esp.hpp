#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

namespace player_esp {
// Gameplay event callbacks feed the transient HUD effects renderer.
void record_hit_marker(int damage, float x, float y, float z);
void record_bullet_impact(int side, float from_x, float from_y, float from_z,
                          float to_x, float to_y, float to_z);

// Pushes the persistent per-side menu settings into the game chams backend and
// draws the real entity ESP on ImGui's background list.
void render();

// Panorama preview uses the same element layout/renderer with representative
// data. prepare_preview() also selects the independent chams pass snapshot.
void prepare_preview(int side);
void draw_preview(const ImVec2& min, const ImVec2& max, int side);

// Used by selective WndProc routing: ESP handles stay in ImGui, all remaining
// mouse drags over the ItemPreviewPanel are forwarded to Panorama.
bool preview_drag_hit(const ImVec2& point);
} // namespace player_esp
