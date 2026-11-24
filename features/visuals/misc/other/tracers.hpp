#pragma once
// Visuals -> Misc -> OTHER: "Bullet tracers".
//
// A tracer is the path of the local player's bullet. Like the on-shot chams
// (features/visuals/chams/shot_chams.cpp), this module listens to the `weapon_fire`
// game event and, on a local shot, traces the current crosshair ray to where the
// bullet would stop. The line from the eye to that point is kept for the tracer's
// life and published as a VisualRuntime::ScreenLine every frame; the render thread
// draws it (menu/overlays/fx_overlay.hpp, Tracer).
//
// What the trace hits picks the colour, matching the three menu swatches: a world
// stop uses the "Local" colour, a player stop the "Enemy" / "Teammates" one. Style 0
// ("Laser") is thicker and lives longer than style 1 ("Line").
#include <array>
#include <cmath>
#include <cstdint>

#include "../../../../sdk/visual_runtime_state.hpp"

namespace Tracers {

namespace Target {
inline constexpr int Local = 0;
inline constexpr int Enemy = 1;
inline constexpr int Team = 2;
inline constexpr int Count = 3;
} // namespace Target

struct Settings {
    std::array<bool, Target::Count> enabled{};
    std::array<std::uint32_t, Target::Count> color{};
    std::array<int, Target::Count> style{}; // 0 laser, 1 line
};

namespace Detail {
// How long a tracer stays on screen. The laser looks like a sustained beam, the
// plain line like the game's own short tracer.
inline float Life(int style) { return style == 0 ? 0.2f : 0.1f; }
inline float Width(int style) { return style == 0 ? 2.5f : 1.5f; }
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
// Per frame, from the game thread: publish the still-alive tracer lines.
void Contribute(VisualRuntime::FxFrame& frame);
} // namespace Tracers
