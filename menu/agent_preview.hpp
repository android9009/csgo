#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

namespace agent_preview {
// Fixed, embedded full-body images: side 0 is Enemies and side 1 is Teammates.
// No Source model rendering, FrameStageNotify hook, VPK access, or network access is used.
void draw(const ImVec2& min, const ImVec2& max, int side);
void on_device_reset();
}
