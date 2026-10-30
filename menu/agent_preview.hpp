#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

struct IDirect3DDevice9;

// Live CS:GO ItemPreviewPanel mirrored into the existing ImGui Players column.
// The D3D9 frame hooks own capture/reset lifetime; draw() only synchronizes the panel when needed.
namespace agent_preview {
void begin_frame(IDirect3DDevice9* device);
void draw(const ImVec2& min, const ImVec2& max, int side);
void end_frame();
void on_device_reset();
}
