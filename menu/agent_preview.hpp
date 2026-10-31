#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

struct IDirect3DDevice9;

// Hybrid preview: Fatality's C_MergedMDL renders the full body into a material target at
// FrameStageNotify; Legendware's VPK/D3D9 texture path provides a portrait-only fallback.
// The D3D9/ImGui pass displays whichever completed texture is currently available.
namespace agent_preview {
void* frame_stage_notify_target();
void on_frame_stage_render_end();

void begin_frame(IDirect3DDevice9* device);
void draw(const ImVec2& min, const ImVec2& max, int side);
void end_frame();
void on_device_reset();
}
