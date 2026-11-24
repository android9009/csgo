#pragma once
// Removals for the Visuals tab: the "visuals>misc>other>removals" combo box.
//
// The combo box stores a bitset, so the bit order below must match the item list in
// menu/tabs/tab_visuals.cpp. tests/removals_test.cpp pins that order, because a silent
// mismatch between the UI order and the runtime bit is exactly how removals end up
// toggling the wrong effect.
//
// The mechanisms, mirroring this repository's `main` branch where it has the same
// feature:
//   - flash, visual punch: per-frame netvar writes on the local player.
//   - post processing: `mat_postprocess_enable 0` (main's post_processing.cpp).
//   - shadows: `cl_csm_shadows 0` (main's post_processing.cpp shadow branch).
//   - particle: `r_drawparticles 0`, the client's master particle-render switch.
//   - water: `mat_water_simm 0`, the client's water-simulation switch.
//   - blood, decals: `mat_decals 0`, the client's decal cap - blood in CS:GO is a
//     decal (DecalBullet / DecalBulletPlayer), so both bits share the override.
//   - smoke: MATERIAL_VAR_NO_DRAW on the four particle/vistasmokev1 materials (main's
//     remove_projectiles.cpp); the cloud is vpcf-sprite rendered through them.
//   - ragdolls: the DrawModelExecute hook skips CCSRagdoll model draws.
//
// The cvar overrides capture their original values before the first write and hand
// them back when the bit goes off, when there is no game, and on unload - a cvar or
// a material flag that survives the cheat being gone would be worse than no feature.
#include <cstdint>

#include "../../../../sdk/studiohdr.hpp"

namespace Removals {
// Bit positions, in the combo box order. They live in their own scope because the
// feature namespaces own the simple names.
namespace Bits {
inline constexpr int Flash = 0;           // "Flashbang Effect"
inline constexpr int Smoke = 1;           // "Smoke Rendering"
inline constexpr int Particle = 2;        // "Particle"
inline constexpr int PostProcessing = 3;  // "Post Processing"
inline constexpr int Water = 4;           // "Water Effects"
inline constexpr int Blood = 5;           // "Blood Effects"
inline constexpr int Decals = 6;          // "Decals"
inline constexpr int Ragdolls = 7;        // "Ragdolls"
inline constexpr int VisualPunch = 8;     // "Visual punch"
inline constexpr int Shadows = 9;         // "Shadows"
inline constexpr int Count = 10;
} // namespace Bits

inline bool Enabled(std::uint64_t mask, int index) {
    return index >= 0 && index < Bits::Count && ((mask >> index) & 1ull) != 0;
}

// The decoded mask.
struct Flags {
    bool flash{};
    bool smoke{};
    bool particle{};
    bool post_processing{};
    bool water{};
    bool blood{};
    bool decals{};
    bool ragdolls{};
    bool visual_punch{};
    bool shadows{};
};

inline Flags FromMask(std::uint64_t mask) {
    return {Enabled(mask, Bits::Flash),       Enabled(mask, Bits::Smoke),
            Enabled(mask, Bits::Particle),    Enabled(mask, Bits::PostProcessing),
            Enabled(mask, Bits::Water),       Enabled(mask, Bits::Blood),
            Enabled(mask, Bits::Decals),      Enabled(mask, Bits::Ragdolls),
            Enabled(mask, Bits::VisualPunch), Enabled(mask, Bits::Shadows)};
}

namespace Detail {
// Flash: only a positive duration needs clearing. The comparison also rejects NaN,
// which a stale or partially updated netvar can contain.
inline bool ShouldClear(float duration) { return duration > 0.f; }

// Punch: recomputed by the game every frame, so removal is a per-frame zero rather
// than a stored value. Any non-zero component is worth writing; NaN also compares
// unequal to zero and is therefore cleared.
inline bool ShouldZero(float x, float y, float z) { return x != 0.f || y != 0.f || z != 0.f; }

// Blood and decals share the mat_decals override: blood in CS:GO is a decal.
inline bool DecalsActive(bool blood, bool decals) { return blood || decals; }

// The change-driven cvar / material-flag write: write exactly when the wanted state
// differs from the asserted state. While enabled the override value is written,
// while disabled the captured original comes back.
inline bool ShouldWrite(bool wanted, bool asserted) { return wanted != asserted; }
} // namespace Detail

// Called from the menu bridge on the render thread.
void Publish(std::uint64_t mask);

// Called on the game thread from FrameStageNotify.
void FrameStage(int stage);

// Hand every cvar and material flag back (no game / unload).
void Restore();

// DrawModelExecute hook: true when the model must not be drawn at all (ragdoll
// removal). Checked before the chams pass so a removed ragdoll gets no chams either.
bool SkipModel(const ModelRenderInfo_t& info);
} // namespace Removals
