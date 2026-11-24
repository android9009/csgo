#include "fov.hpp"
#include "../../../../context.hpp"
#include "../../../../sdk/interfaces/interfaces.hpp"
#include "../../../../sdk/entity/player.hpp"
#include <mutex>

namespace FovChanger {
namespace {
Settings g_snapshot;
std::mutex g_mutex;
}

void Publish(const Settings& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_snapshot = s;
}

Settings Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_snapshot;
}

void FrameStage() {
    const auto s = Snapshot();
    auto* local = ctx.local;
    if (!s.enabled || !local || !ctx.in_game) return;
    const int fov = Detail::ClampFov(s.fov);
    // Hold the fov constant: writing both the live and the pre-zoom value keeps the scope from
    // fighting us and gives the "remove zoom" effect for free.
    local->m_iFOV() = fov;
    local->m_iFOVStart() = fov;
}

void Shutdown() {
    // Nothing to restore: the next frame without the feature leaves m_iFOV to the game.
}
} // namespace FovChanger
