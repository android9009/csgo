#pragma once

#include "imgui.h"

namespace weapon_icons {
// Draws an embedded white weapon glyph, aspect-fitted and tinted by color.
void draw(ImDrawList* draw_list, int weapon, ImVec2 center, ImVec2 max_size, ImU32 color);

// The atlas uses a D3DPOOL_DEFAULT texture and must be recreated after Reset.
void on_device_reset();
}
