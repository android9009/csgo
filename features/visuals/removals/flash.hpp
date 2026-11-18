#pragma once
// Removals bit 0: the local player's flash overlay.

namespace Removals::Flash {
// Only a positive duration needs clearing. The comparison also rejects NaN, which a
// stale or partially updated netvar can contain.
inline bool ShouldClear(float duration) { return duration > 0.f; }

void FrameStage(int stage, bool enabled);
} // namespace Removals::Flash
