#include "general.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include <algorithm>
#include <atomic>

namespace Misc::Radar {
namespace {
std::atomic<bool> g_enabled{false};
constexpr int kMaxPlayerSlots = 64;

// One pass over the player slots. Netvar offsets are cached by the NETVAR_FALLBACK
// statics and a write happens only when the flag is not already set, so a settled
// frame performs no writes at all.
void Apply() {
    if (!ctx.in_game || !Interfaces::Engine || !Interfaces::EntityList) return;
    const int max_clients = Interfaces::Engine->GetMaxClients();
    if (max_clients <= 0) return;
    const int last = (std::min)(max_clients, kMaxPlayerSlots);
    for (int index = 1; index <= last; ++index) {
        auto* player = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(index));
        if (!player) continue;
        Target target;
        target.local = player == ctx.local;
        target.team = player->m_iTeamNum();
        target.alive = player->IsAlive();
        target.dormant = player->IsDormant();
        if (!ShouldSpot(target)) continue;
        if (!player->m_bSpotted()) player->m_bSpotted() = true;
    }
}
} // namespace

void SetEnabled(bool enabled) { g_enabled.store(enabled, std::memory_order_relaxed); }

void FrameStage(int stage) {
    if (stage != 4 || !g_enabled.load(std::memory_order_relaxed)) return;
    Apply();
}
} // namespace Misc::Radar
