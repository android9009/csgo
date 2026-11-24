#pragma once
// Removals for the Visuals tab: the "visuals>misc>other>removals" combo box.
//
// The combo box stores a bitset, so the bit order below must match the item list in
// menu/tabs/tab_visuals.cpp. tests/removals_test.cpp pins that order, because a silent
// mismatch between the UI order and the runtime bit is exactly how removals end up
// toggling the wrong effect.
//
// One file per feature, like the rest of features/visuals: the mask plumbing and the
// two removals this build can act on with netvar writes alone (flash, visual punch)
// live in removals.cpp next to this header.
#include <cstdint>

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
    return index >= 0 && ((mask >> index) & 1ull) != 0;
}

// The subset this module can act on safely with netvar writes alone.
struct Flags {
    bool flash{};
    bool visual_punch{};
};

inline Flags FromMask(std::uint64_t mask) {
    return {Enabled(mask, Bits::Flash), Enabled(mask, Bits::VisualPunch)};
}

namespace Detail {
// Flash: only a positive duration needs clearing. The comparison also rejects NaN,
// which a stale or partially updated netvar can contain.
inline bool ShouldClear(float duration) { return duration > 0.f; }

// Punch: recomputed by the game every frame, so removal is a per-frame zero rather
// than a stored value. Any non-zero component is worth writing; NaN also compares
// unequal to zero and is therefore cleared.
inline bool ShouldZero(float x, float y, float z) { return x != 0.f || y != 0.f || z != 0.f; }
} // namespace Detail

// Called from the menu bridge on the render thread.
void Publish(std::uint64_t mask);

// Called on the game thread from FrameStageNotify.
void FrameStage(int stage);
} // namespace Removals
