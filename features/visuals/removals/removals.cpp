#include "removals.hpp"
#include "flash.hpp"
#include "punch.hpp"
#include <atomic>

namespace Removals {
namespace {
std::atomic<std::uint64_t> g_mask{0};
} // namespace

void Publish(std::uint64_t mask) { g_mask.store(mask, std::memory_order_relaxed); }

void FrameStage(int stage) {
    const auto mask = g_mask.load(std::memory_order_relaxed);
    if (!mask) return;
    const auto flags = FromMask(mask);
    Flash::FrameStage(stage, flags.flash);
    Punch::FrameStage(stage, flags.visual_punch);
}
} // namespace Removals
