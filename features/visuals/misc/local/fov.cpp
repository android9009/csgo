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
    // Hold the fov constant: the game resets m_iFOV from m_iDefaultFOV every frame when the
    // player is unscoped, so all three base-player fields must carry our value (the `main`
    // branch writes exactly default_fov = fov_start = fov). Writing only the live pair lets
    // the reset win next frame, which is what made the feature look dead.
    local->m_iDefaultFOV() = fov;
    local->m_iFOVStart() = fov;
    local->m_iFOV() = fov;
}

void Shutdown() {
    // Nothing to restore: the next frame without the feature leaves m_iFOV to the game.
}
} // namespace FovChanger
