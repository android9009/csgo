#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

struct IDirect3DDevice9;

// Fatality C_MergedMDL preview. FrameStageNotify renders into the material target;
// the D3D9/ImGui pass only displays the completed raw texture.
namespace agent_preview {
void* frame_stage_notify_target();
void on_frame_stage_render_end();

void begin_frame(IDirect3DDevice9* device);
void draw(const ImVec2& min, const ImVec2& max, int side);
void end_frame();
void on_device_reset();
}
