#pragma once
// Removals for the Visuals tab: the "visuals>misc>other>removals" combo box.
//
// The combo box stores a bitset, so the bit order below must match the item list in
// menu/tabs/visuals/misc/misc_other.cpp. tests/removals_test.cpp pins that order, because a silent
// mismatch between the UI order and the runtime bit is exactly how removals end up
// toggling the wrong effect.
#include <cstdint>

namespace Removals {
// Bit positions, in the combo box order. They live in their own scope because the
// feature namespaces (Removals::Flash, Removals::Punch) own the simple names.
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

// Called from the menu bridge on the render thread.
void Publish(std::uint64_t mask);

// Called on the game thread from FrameStageNotify.
void FrameStage(int stage);
} // namespace Removals
