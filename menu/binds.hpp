#pragma once

// This header is what pulls imgui in for binds.cpp, and it has to ask for the ImVec2/ImVec4
// operators before it does: gui.hpp defines the macro too, but it is included second there, by
// which point the imgui include guards have already shut the operators out of the translation
// unit. Guarded, so a caller that already defined it (imgui_widgets.cpp) is left alone.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "../sdk/includes/imgui/imgui.h"
#include "../sdk/includes/imgui/imgui_internal.h"
#include <cstdint>

struct ImGuiWindow;

namespace menu_binds {
using target_id = std::uint64_t;

void register_bool(const char* key, const char* label, bool* value);
void register_int(const char* key, const char* label, int* value, int minimum, int maximum);
void register_float(const char* key, const char* label, float* value, float minimum, float maximum);

// `rect` is the whole row (right-clicking anywhere on it opens the bind menu); `control` is just
// the widget on the right - the toggle or the slider track - which is what the gear is placed
// against so it lands in the gap left of the control rather than over the row's value.
target_id checkbox_item(ImGuiWindow* window, ImGuiID item_id, const char* label,
                        const ImRect& rect, const ImRect& control, bool* value);
target_id slider_item(ImGuiWindow* window, ImGuiID item_id, const char* label,
                      const ImRect& rect, const ImRect& control, ImGuiDataType type, void* value,
                      const void* minimum, const void* maximum);

void sync_bool(target_id id, bool value);
void sync_int(target_id id, int value);
void sync_float(target_id id, float value);

void update();
void flush();
void before_config_save();
void after_config_save();
void on_config_loaded();
bool is_capturing();
void draw_overlay(bool menu_open);

// The "Hotkeys" row in the bind menu toggles the on-screen keybind list.
bool overlay_enabled();
void set_overlay_enabled(bool enabled);
}
