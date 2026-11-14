#pragma once

#include <d3d9.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "imgui_internal.h"

// Direct3D 9 implementation of the blur API used by Darkside's ImGui chrome.
// The device is borrowed from the Present hook and remains owned by the game.
namespace blur {
    inline IDirect3DDevice9* device = nullptr;

    void on_device_reset();
    void shutdown();
    HRESULT last_result();
}

void draw_blur(ImDrawList* draw, float alpha = 1.0f, float rounding = -1.0f);
void draw_blur_rounded(ImDrawList* draw, ImVec2 min, ImVec2 max, float alpha = 1.0f,
    float rounding = -1.0f, ImColor tint = ImColor(1.0f, 1.0f, 1.0f, 0.0f));
