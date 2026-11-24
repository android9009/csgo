#pragma once
// Removals bit 8: the local player's visual punch angles.

namespace Removals::Punch {
// Punch is recomputed by the game every frame, so removal is a per-frame zero rather
// than a stored value. Any non-zero component is worth writing; NaN also compares
// unequal to zero and is therefore cleared.
inline bool ShouldZero(float x, float y, float z) { return x != 0.f || y != 0.f || z != 0.f; }

void FrameStage(int stage, bool enabled);
} // namespace Removals::Punch
