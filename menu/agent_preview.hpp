#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

struct IDirect3DDevice9;

namespace agent_preview {
// The D3D9 frame hook captures Panorama before ImGui covers the backbuffer.
// draw() presents only the current Panorama capture; no static image fallback is used.
void begin_frame(IDirect3DDevice9* device);
void draw(const ImVec2& min, const ImVec2& max, int side, bool chams_active = true);
void end_frame();
// CS:GO 2023: true after SetScene succeeds and Panorama reports ready for display.
bool is_preloaded();
bool contains_point(const ImVec2& point);
void on_menu_closed();
void on_device_reset();
}
