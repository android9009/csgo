#include "punch.hpp"
#include "../../../../../context.hpp"
#include "../../../../../sdk/entity/player.hpp"

namespace Removals::Punch {
void FrameStage(int stage, bool enabled) {
    if (!enabled || (stage != 0 && stage != 5)) return;
    if (!ctx.in_game || !ctx.local) return;
    auto& aim = ctx.local->m_aimPunchAngle();
    if (ShouldZero(aim.x, aim.y, aim.z)) aim = {};
    auto& view = ctx.local->m_viewPunchAngle();
    if (ShouldZero(view.x, view.y, view.z)) view = {};
}
} // namespace Removals::Punch
