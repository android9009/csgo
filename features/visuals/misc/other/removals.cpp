#include "removals.hpp"
#include "../../../../context.hpp"
#include "../../../../sdk/entity/player.hpp"
#include <atomic>

namespace Removals {
namespace {
std::atomic<std::uint64_t> g_mask{0};
} // namespace

void Publish(std::uint64_t mask) { g_mask.store(mask, std::memory_order_relaxed); }

// Bit 0: the local player's flash overlay.
void FlashFrameStage(int stage, bool enabled) {
    if (!enabled || (stage != 0 && stage != 5)) return;
    if (!ctx.in_game || !ctx.local || !ctx.local->IsAlive()) return;
    auto& duration = ctx.local->m_flFlashDuration();
    if (!Detail::ShouldClear(duration)) return;
    duration = 0.f;
}

// Bit 8: the local player's visual punch angles.
void PunchFrameStage(int stage, bool enabled) {
    if (!enabled || (stage != 0 && stage != 5)) return;
    if (!ctx.in_game || !ctx.local) return;
    auto& aim = ctx.local->m_aimPunchAngle();
    if (Detail::ShouldZero(aim.x, aim.y, aim.z)) aim = {};
    auto& view = ctx.local->m_viewPunchAngle();
    if (Detail::ShouldZero(view.x, view.y, view.z)) view = {};
}

void FrameStage(int stage) {
    const auto mask = g_mask.load(std::memory_order_relaxed);
    if (!mask) return;
    const auto flags = FromMask(mask);
    FlashFrameStage(stage, flags.flash);
    PunchFrameStage(stage, flags.visual_punch);
}
} // namespace Removals
