#include "misc.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/entity/weapon.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/structs.hpp"
#include "../../../sdk/trace_ray.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/math/math.hpp"
#include <windows.h>
#include <cmath>

namespace VisualsMisc {
namespace {
// The mask `main` traces third person with (MASK_NPCWORLDSTATIC: solid, window, monster,
// grate - world geometry and props, never players).
constexpr unsigned int kMaskWorld = 0x200400B;
// main: a +-16 unit box around the camera ray, so a thin wall cannot be slipped through.
constexpr float kCameraExtent = 16.f;
// Observer modes from the SDK: 4 is in-eye, 5 is chase. That pair is what "Force when
// dead" flips, exactly like main's c_thirdperson::run_alive does.
constexpr int kObserverInEye = 4;
constexpr int kObserverChase = 5;
bool g_reported{};
} // namespace

void OverrideView(void* view_setup) {
    auto* setup = static_cast<ViewSetup*>(view_setup);
    if (!setup) return;
    const Settings settings = Detail::Snapshot();
    if (!Interfaces::Engine) {
        Detail::SetHideLocalModel(false);
        return;
    }

    bool in_game = false;
    __try { in_game = Interfaces::Engine->IsInGame() && Interfaces::Engine->IsConnected(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { in_game = false; }

    CCSPlayer* local = ctx.local;
    bool alive = false;
    bool grenade = false;
    __try {
        alive = local && local->IsAlive();
        // WEAPONTYPE_GRENADE in CS:GO's CSWeaponType enum.
        grenade = settings.thirdperson_grenade && ctx.weapon_data && ctx.weapon_data->weapon_type == 9;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        alive = false;
    }

    const auto plan = Detail::Plan(settings, in_game, alive, grenade);

    if (!alive && plan.dead_observer && local) {
        __try {
            if (local->m_iObserverMode() == kObserverInEye) local->m_iObserverMode() = kObserverChase;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (!plan.active || !local || !Interfaces::EngineTrace) {
        Detail::SetHideLocalModel(false);
        return;
    }

    Vector eye;
    QAngle angles;
    __try {
        eye = local->GetEyePosition();
        Interfaces::Engine->GetViewAngles(angles);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Detail::SetHideLocalModel(false);
        return;
    }
    if (!angles.IsValid()) {
        Detail::SetHideLocalModel(false);
        return;
    }

    const Vector forward = Math::angle_vector(angles);
    if (!std::isfinite(forward.x) || !std::isfinite(forward.y) || !std::isfinite(forward.z)) {
        Detail::SetHideLocalModel(false);
        return;
    }

    // Walk backwards from the eye and stop at the first thing the world throws in the way,
    // so the camera never ends up inside a wall.
    const Vector target = eye - forward * plan.distance;
    Ray_t ray;
    ray.Init(eye, target, Vector{-kCameraExtent, -kCameraExtent, -kCameraExtent},
             Vector{kCameraExtent, kCameraExtent, kCameraExtent});
    CGameTrace trace;
    CTraceFilterSimple filter(local);
    float fraction = 1.f;
    __try {
        Interfaces::EngineTrace->TraceRay(ray, kMaskWorld, &filter, &trace);
        fraction = trace.fraction;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fraction = 0.f;
    }

    const float distance = Detail::ClampedDistance(plan.distance, fraction);
    if (distance <= 0.f) {
        // Something is right behind the head: keep the first-person camera rather than
        // placing it inside the wall.
        Detail::SetHideLocalModel(false);
        return;
    }

    setup->origin = eye - forward * distance;
    Detail::SetHideLocalModel(true);

    if (!g_reported) {
        g_reported = true;
        LOG_INFO("visuals misc: third person active from ClientMode::OverrideView "
                 "(distance=%.0f dead=force chase)", static_cast<double>(plan.distance));
    }
}
} // namespace VisualsMisc
