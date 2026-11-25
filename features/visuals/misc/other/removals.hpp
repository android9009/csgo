#pragma once
// Removals feature bridge (Visuals tab "Removals" combo).
// The menu stores a bitset; bit order is pinned to the combo in tab_visuals.cpp:
//   0 Flash, 1 Smoke, 2 Particle, 3 Post Processing, 4 Water, 5 Blood, 6 Decals,
//   7 Ragdolls, 8 Visual punch, 9 Shadows.
// Forward the subset that airflow's misc.removals bitmask understands
// (scope/vis_recoil/post_process/smoke/flash/fog_/shadow/viewmodel_move/landing_bob).
#include "features/airflow_bridge.hpp"
#include "../../sdk/visual_runtime_state.hpp"
#include <cstdint>
namespace Removals {
namespace Bits {
inline constexpr int Flash = 0;
inline constexpr int Smoke = 1;
inline constexpr int Particle = 2;
inline constexpr int PostProcessing = 3;
inline constexpr int Water = 4;
inline constexpr int Blood = 5;
inline constexpr int Decals = 6;
inline constexpr int Ragdolls = 7;
inline constexpr int VisualPunch = 8;
inline constexpr int Shadows = 9;
inline constexpr int Count = 10;
}
inline bool Enabled(std::uint64_t mask, int index) {
    return index >= 0 && index < Bits::Count && ((mask >> index) & 1ull) != 0;
}
inline unsigned FromMask(std::uint64_t mask) {
    unsigned out = 0;
    if (Enabled(mask, Bits::Flash)) out |= static_cast<unsigned>(removals_t::flash);
    if (Enabled(mask, Bits::Smoke)) out |= static_cast<unsigned>(removals_t::smoke);
    if (Enabled(mask, Bits::PostProcessing)) out |= static_cast<unsigned>(removals_t::post_process);
    if (Enabled(mask, Bits::VisualPunch)) out |= static_cast<unsigned>(removals_t::vis_recoil);
    if (Enabled(mask, Bits::Shadows)) out |= static_cast<unsigned>(removals_t::shadow);
    return out;
}
inline void Publish(std::uint64_t mask) {
    // "Remove scope" (Visuals -> Misc -> Local) is published through the
    // VisualRuntime state, not the removals combo, but both share the airflow
    // removals bitmask - keep the scope bit when republishing.
    const bool scope_on = VisualRuntime::GetEffects().screen.scope_mode > 0;
    g_cfg.misc.removals = FromMask(mask) | (scope_on ? static_cast<unsigned>(removals_t::scope) : 0u);
}
}
