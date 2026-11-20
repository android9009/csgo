#include "misc.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/entity/weapon.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/trace_ray.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/math/math.hpp"
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
// True while the camera flag currently on `CInput` is ours. The engine never sets it on its
// own for a live player, but a demo, a spec camera or another tool can, and clearing those
// would fight them.
bool g_camera_owned{};
bool g_reported{};
} // namespace

// ClientMode::CreateMove calls this through the frame stage; see FrameStage().
void ApplyThirdPerson() {
    IInput* input = Interfaces::Input;
    if (!input || !Interfaces::Engine) {
        g_camera_owned = false;
        return;
    }
    const Settings settings = Detail::Snapshot();

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

    if (!plan.active || !local) {
        // Hand the camera back exactly once; after that the flag is not ours.
        if (g_camera_owned) {
            input->ClearThirdPersonCamera();
            g_camera_owned = false;
        }
        return;
    }

    QAngle angles;
    __try { Interfaces::Engine->GetViewAngles(angles); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_camera_owned) { input->ClearThirdPersonCamera(); g_camera_owned = false; }
        return;
    }
    if (!angles.IsValid()) {
        if (g_camera_owned) { input->ClearThirdPersonCamera(); g_camera_owned = false; }
        return;
    }

    // Walk backwards from the eye and stop at the first thing the world throws in the way,
    // so the camera never ends up inside a wall. main does the same trace and multiplies its
    // distance by the fraction; the engine only clamps against the map hull, which lets the
    // camera clip through props.
    float distance = plan.distance;
    if (Interfaces::EngineTrace) {
        Vector eye;
        __try { eye = local->GetEyePosition(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { eye = {}; }
        const Vector forward = Math::angle_vector(QAngle{angles.x, angles.y, 0.f});
        if (std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z)) {
            Ray_t ray;
            ray.Init(eye, eye - forward * plan.distance,
                     Vector{-kCameraExtent, -kCameraExtent, -kCameraExtent},
                     Vector{kCameraExtent, kCameraExtent, kCameraExtent});
            CGameTrace trace;
            CTraceFilterSimple filter(local);
            float fraction = 1.f;
            __try {
                Interfaces::EngineTrace->TraceRay(ray, kMaskWorld, &filter, &trace);
                fraction = trace.fraction;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                fraction = 1.f;
            }
            distance = Detail::ClampedDistance(plan.distance, fraction);
        }
    }

    if (distance <= 0.f) {
        // Something is right behind the head: stay in first person rather than inside it.
        if (g_camera_owned) { input->ClearThirdPersonCamera(); g_camera_owned = false; }
        return;
    }

    // The engine reads this pair while it builds the view for the frame: x/y are the angles
    // the camera looks along, z is how far back it sits. main writes the engine's own view
    // angles here, and so do we - the camera must not lag the crosshair.
    input->SetThirdPersonCamera(angles.x, angles.y, distance);
    g_camera_owned = true;

    if (!g_reported) {
        g_reported = true;
        LOG_INFO("visuals misc: third person drives CInput::camera_offset "
                 "(distance=%.0f, CInput at %p)", static_cast<double>(distance),
                 static_cast<void*>(input));
    }
}

void ResetThirdPerson() {
    if (g_camera_owned && Interfaces::Input) Interfaces::Input->ClearThirdPersonCamera();
    g_camera_owned = false;
}
} // namespace VisualsMisc
