#pragma once
// Visuals -> Misc -> OTHER: "Hitmarker".
//
// Marks a confirmed hit on a player. Confirmation is the `player_hurt` game event
// with the local player as attacker (the same route the on-shot chams use - no enemy
// fire timer, no health polling). While the hit is inside its duration the module
// publishes, per menu selection:
//   world  - an X at the victim (kind 1, follows the entity while it is alive),
//   screen - an X at the screen centre (kind 2),
//   kibit  - a second, smaller X at the victim (kind 1, its own colour/duration).
// The render thread draws them (menu/overlays/fx_overlay.hpp, Hitmarker).
#include <cmath>
#include <cstdint>

#include "../../../../sdk/visual_runtime_state.hpp"

namespace Hitmarker {

struct Settings {
    bool world{};
    bool screen{};
    bool kibit{};
    std::uint32_t world_color{0xFFFFFFFFu};
    std::uint32_t screen_color{0xFFFFFFFFu};
    std::uint32_t kibit_color{0xFFFFFFFFu};
    float world_duration{1.f};
    float screen_duration{1.f};
    float kibit_duration{1.f};
};

namespace Detail {
inline bool Within(float age, float duration) { return duration >= 0.f && age < duration; }
inline constexpr float kWorldSize = 0.012f; // fraction of the screen height
inline constexpr float kKibitSize = 0.008f;
inline constexpr float kScreenSize = 0.02f;
} // namespace Detail

void Publish(const Settings& s);
Settings Snapshot();
// Register the player_hurt / round listeners. Called from Hooks::Setup.
void Initialize();
// Hands the listeners back. Called from Hooks::Restore.
void Shutdown();
// Per frame, from the game thread: publish the still-active hitmarker marks.
void Contribute(VisualRuntime::FxFrame& frame);
} // namespace Hitmarker
