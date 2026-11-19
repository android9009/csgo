#include "general.hpp"
#include "../../../sdk/entity/entity.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/netvars.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/math/vector.hpp"
#include <atomic>
#include <cstring>
#include <windows.h>

namespace Misc::Ragdoll {
namespace {
std::atomic<bool> g_enabled{false};
std::atomic<float> g_percent{100.f};
std::uint32_t g_frame{};
bool g_reported{};

// Netvar offsets are resolved once; m_vecForce lives on the ragdoll in some builds and
// on CBaseAnimating in others, so both tables are tried.
struct Offsets {
    std::ptrdiff_t force{};
    std::ptrdiff_t velocity{};
};

const Offsets& GetOffsets() {
    static const Offsets offsets = [] {
        Offsets resolved;
        resolved.force = PropManager::Get().GetOffset("DT_CSRagdoll", "m_vecForce");
        if (!resolved.force) resolved.force = PropManager::Get().GetOffset("CBaseAnimating", "m_vecForce");
        resolved.velocity = PropManager::Get().GetOffset("DT_CSRagdoll", "m_vecRagdollVelocity");
        if (!resolved.force || !resolved.velocity)
            LOG_WARN("misc: ragdoll thrust - netvars missing (force=0x%X velocity=0x%X); feature idle",
                     static_cast<unsigned>(resolved.force), static_cast<unsigned>(resolved.velocity));
        return resolved;
    }();
    return offsets;
}

Vector* Field(void* entity, std::ptrdiff_t offset) {
    if (!offset) return nullptr;
    return reinterpret_cast<Vector*>(reinterpret_cast<std::uintptr_t>(entity) + offset);
}

void Apply() {
    if (!Interfaces::EntityList || !Interfaces::Engine) return;
    const auto& offsets = GetOffsets();
    if (!offsets.force || !offsets.velocity) return;

    const float percent = g_percent.load(std::memory_order_relaxed);
    const int max_clients = Interfaces::Engine->GetMaxClients();
    const int highest = Interfaces::EntityList->GetHighestEntityIndex();
    int scaled = 0;
    // Ragdolls are entities above the player slots; the class name is checked instead of
    // a class id so a build with a different id table cannot make us write elsewhere.
    for (int index = max_clients + 1; index <= highest; ++index) {
        auto* entity = static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(index));
        if (!entity) continue;
        auto* client_class = entity->GetClientClass();
        if (!client_class || !client_class->m_pNetworkName) continue;
        if (std::strcmp(client_class->m_pNetworkName, "CCSRagdoll") != 0) continue;

        auto* force = Field(entity, offsets.force);
        auto* velocity = Field(entity, offsets.velocity);
        if (!force || !velocity) continue;
        if (!ShouldScale(velocity->Length(), kSpeedLimit)) continue;

        const float factor = ScalePercent(percent) / 100.f;
        *force = *force * factor;
        *velocity = *velocity * factor;
        ++scaled;
    }

    if (!g_reported && scaled > 0) {
        g_reported = true;
        LOG_INFO("misc: ragdoll thrust active (%.0f%%, %d ragdoll(s) scaled)", percent, scaled);
    }
}
} // namespace

void Publish(bool enabled, float percent) {
    g_enabled.store(enabled, std::memory_order_relaxed);
    const int value = static_cast<int>(percent + 0.5f);
    g_percent.store(ScalePercent(value), std::memory_order_relaxed);
}

void FrameStage(int stage) {
    if (stage != 4 || !g_enabled.load(std::memory_order_relaxed)) return;
    // A full entity sweep every frame is wasteful; ragdolls persist, so 1/4 is plenty.
    if ((++g_frame & 3u) != 0u) return;
    __try { Apply(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
} // namespace Misc::Ragdoll
