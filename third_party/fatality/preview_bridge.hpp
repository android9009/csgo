#pragma once
#include <atomic>
namespace FatalityPreviewBridge {
inline std::atomic<bool> visible{false};
inline std::atomic<bool> skins_selecting{false};
// Set by the visible viewport and CT/T selector; Source render hook consumes it.
inline std::atomic<bool> skins_team_ct{true};
inline std::atomic<bool> render_ct{true};
inline std::atomic<bool> render_skins{false};
inline std::atomic<int> agent[2]{};
inline std::atomic<int> weapon{0};
inline std::atomic<float> yaw{180.f};
inline std::atomic<float> distance{310.f};
inline std::atomic<void*> texture{nullptr};
// Separate editor target; never reuses or replaces the agent viewport.
inline std::atomic<bool> weapon_editor_visible{false};
inline std::atomic<int> editor_weapon{0};
inline std::atomic<float> editor_yaw{0.f}, editor_pitch{0.f}, editor_roll{0.f};
inline std::atomic<float> editor_pan_x{0.f}, editor_pan_y{0.f};
inline std::atomic<void*> editor_texture{nullptr};
}
