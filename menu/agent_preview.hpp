#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

namespace agent_preview {
// Legendware-style preview: read the agent portrait from the game VPK and display it as an
// ordinary ImGui texture. No Source model rendering or FrameStageNotify hook is involved.
void draw(const ImVec2& min, const ImVec2& max, int side);
void on_device_reset();
}
