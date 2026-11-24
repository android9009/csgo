#include "flash.hpp"
#include "../../../../../context.hpp"
#include "../../../../../sdk/entity/player.hpp"

namespace Removals::Flash {
void FrameStage(int stage, bool enabled) {
    if (!enabled || (stage != 0 && stage != 5)) return;
    if (!ctx.in_game || !ctx.local || !ctx.local->IsAlive()) return;
    auto& duration = ctx.local->m_flFlashDuration();
    if (!ShouldClear(duration)) return;
    duration = 0.f;
}
} // namespace Removals::Flash
