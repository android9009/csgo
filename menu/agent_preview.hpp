#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

struct IDirect3DDevice9;

namespace agent_preview {
// The D3D9 frame hook captures Panorama before ImGui covers the backbuffer.
// draw() keeps the embedded PNG fallback until a current capture is available.
void begin_frame(IDirect3DDevice9* device);
void draw(const ImVec2& min, const ImVec2& max, int side);
void end_frame();
void on_device_reset();
}
