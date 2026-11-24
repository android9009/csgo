#include "thirdperson.hpp"
#include "../../context.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/entity/player.hpp"
#include <mutex>

namespace ThirdPerson {
namespace {
Settings g_snapshot;
std::mutex g_mutex;
bool g_active = false;

// C4 / HE / Flash / Smoke / Molotov / Incendiary / Decoy item-definition indices.
bool IsGrenade(int id) {
    switch (id) {
    case 39: case 43: case 44: case 45: case 46: case 47: case 48:
        return true;
    default:
        return false;
    }
}
} // namespace

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
    if (!Interfaces::Input) return;
    auto* local = ctx.local;
    if (!local || !ctx.in_game) {
        if (g_active) {
            Interfaces::Input->ClearThirdPersonCamera();
            g_active = false;
        }
        return;
    }

    const bool alive = local->IsAlive();
    const bool scoped = local->m_bIsScoped();
    const bool holding_grenade = (ctx.weapon != nullptr) && IsGrenade(ctx.weapon->m_iItemDefinitionIndex());

    if (Detail::Active(s, alive, holding_grenade)) {
        if (auto* engine = Interfaces::Engine) {
            QAngle ang;
            engine->GetViewAngles(ang);
            Interfaces::Input->SetThirdPersonCamera(ang.x, ang.y, s.distance);
        }
        local->m_nRenderMode() = RenderTransTexture;
        local->m_clrRender() = (static_cast<std::uint32_t>(Detail::ModelAlpha(s, scoped, holding_grenade)) << 24) | 0x00FFFFFFu;
        g_active = true;
    } else if (g_active) {
        Interfaces::Input->ClearThirdPersonCamera();
        local->m_nRenderMode() = RenderNormal;
        local->m_clrRender() = 0xFFFFFFFFu;
        g_active = false;
    }
}

void Shutdown() {
    if (Interfaces::Input) Interfaces::Input->ClearThirdPersonCamera();
    if (ctx.local) {
        ctx.local->m_nRenderMode() = RenderNormal;
        ctx.local->m_clrRender() = 0xFFFFFFFFu;
    }
    g_active = false;
}
} // namespace ThirdPerson
