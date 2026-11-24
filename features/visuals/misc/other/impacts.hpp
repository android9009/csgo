#pragma once
// Visuals -> Misc -> OTHER: "Show impacts".
//
// Marks where the local player's bullets stop. The game knows nothing about an impact
// to the client besides the `weapon_fire` event, so (as with the tracers) the module
// traces the crosshair ray on a local shot and keeps the ray's end point while the
// impact is "fresh". The point is published as a VisualRuntime::ScreenMark (kind 0,
// the impact box) each frame; the render thread draws it
// (menu/overlays/fx_overlay.hpp, ImpactBox).
//
// A stop on a player is the hitmarker's territory (a red impact box on a body would
// only fight it), so those shots are not recorded.
#include <cmath>
#include <cstdint>

#include "../../../../sdk/visual_runtime_state.hpp"

namespace Impacts {

struct Settings {
    bool enabled{};
    std::uint32_t color1{0xFF000000u}; // box body
    std::uint32_t color2{0xFFFFFFFFu}; // corner ticks
};

namespace Detail {
inline constexpr float kLife = 0.6f;      // an impact sits for a moment, then is gone
inline constexpr float kSize = 0.008f;    // half-extent, fraction of the screen height

// evo::ren packs colours 0xAABBGGRR; fading scales the alpha byte toward zero.
inline std::uint32_t Fade(std::uint32_t rgba, float t) {
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    const float alpha = static_cast<float>((rgba >> 24) & 0xFFu) * (1.f - t) / 255.f;
    return (rgba & 0x00FFFFFFu) | (static_cast<std::uint32_t>(alpha * 255.f) << 24);
}
} // namespace Detail

void Publish(const Settings& s);
Settings Snapshot();
// Register the weapon_fire listener. Called from Hooks::Setup.
void Initialize();
// Hands the listener back. Called from Hooks::Restore.
void Shutdown();
// Per frame, from the game thread: publish the still-fresh impact marks.
void Contribute(VisualRuntime::FxFrame& frame);
} // namespace Impacts
