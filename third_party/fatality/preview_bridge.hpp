#pragma once
#include <atomic>
namespace FatalityPreviewBridge {
inline std::atomic<bool> visible{false};
inline std::atomic<bool> skins_selecting{false};
// Set by the visible viewport and CT/T selector; Source render hook consumes it.
inline std::atomic<bool> skins_team_ct{true};
inline std::atomic<bool> render_ct{true};
inline std::atomic<bool> render_skins{false};
// Visuals only: 0 enemy, 1 teammate, 2 local.
inline std::atomic<int> visuals_subject{0};
inline std::atomic<float> skins_aspect{220.f / 292.f};
inline std::atomic<int> agent[2]{};
inline std::atomic<int> weapon{0};
inline std::atomic<float> yaw{180.f};
inline std::atomic<float> distance{310.f};
inline std::atomic<void*> texture{nullptr};
// Separate editor target; never reuses or replaces the agent viewport.
inline std::atomic<bool> weapon_editor_visible{false};
inline std::atomic<int> editor_weapon{0};
// Copied request only: the renderer never retains UI strings or live entity pointers.
inline std::atomic<int> editor_paint{0}, editor_seed{0};
inline std::atomic<float> editor_wear{0.f};
// 0 base, 1 waiting for a matching live item, 2 copied live materials, 3 incompatible build.
inline std::atomic<int> editor_material_status{0};
inline std::atomic<float> editor_yaw{0.f}, editor_pitch{0.f}, editor_roll{0.f};
inline std::atomic<float> editor_pan_x{0.f}, editor_pan_y{0.f};
inline std::atomic<void*> editor_texture{nullptr};
}
