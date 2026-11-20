#include "misc.hpp"
#include "../../../context.hpp"
#include "../../../sdk/convars.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/entity/weapon.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/netvars.hpp"
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
// WEAPONTYPE_GRENADE in CS:GO's CSWeaponType enum: the "Disable on grenade" input and the
// "+ with nades" transparency slot.
constexpr int kWeaponTypeGrenade = 9;
// kRenderTransTexture: the render mode that makes the client blend the model with its
// colour alpha instead of ignoring it.
constexpr int kRenderTransTexture = 2;
constexpr int kRenderNormal = 0;

// True while the camera flag currently on `CInput` is ours. The engine never sets it on its
// own for a live player, but a demo, a spec camera or another tool can, and clearing those
// would fight them.
bool g_camera_owned{};
bool g_reported{};

// The chase camera's own cvars. `cam_idealdist` is the desired distance the camera code
// reads, `c_mindistance` / `c_maxdistance` are the bounds the game clamps it with - the game
// defaults (30 and 250) cut the menu's slider short, so they are widened once and handed
// back on unload. The originals are captured before the first write, never after.
struct ChaseVars {
    bool attempted{};
    bool ready{};
    ConVars::SavedVar min{};
    ConVars::SavedVar max{};
    ConVars::SavedVar ideal{};
    float written{-1.f}; // the distance this module last put into cam_idealdist
} g_chase;

// The local model's own alpha while this module owns it, so switching third person off hands
// the game's values back instead of leaving a ghost model behind.
struct OwnedModel {
    CBaseEntity* entity{};
    unsigned char alpha{255};
    int render_mode{};
    bool active{};
} g_model;
} // namespace

namespace {
// Called from the frame stage, i.e. once the ConVar interface exists. A missing interface is
// not a failure: the next frame tries again.
bool InitChaseVars() {
    if (g_chase.ready) return true;
    if (!Interfaces::ConVar) return false;
    if (g_chase.attempted) return g_chase.ready;
    g_chase.attempted = true;
    if (!g_chase.min.Capture("c_mindistance") || !g_chase.max.Capture("c_maxdistance") ||
        !g_chase.ideal.Capture("cam_idealdist")) {
        LOG_WARN("visuals misc: third person cvars not found (c_mindistance=%p c_maxdistance=%p "
                 "cam_idealdist=%p); the distance slider falls back to the menu value",
                 static_cast<void*>(g_chase.min.var), static_cast<void*>(g_chase.max.var),
                 static_cast<void*>(g_chase.ideal.var));
        return false;
    }
    ConVars::SetFloat(g_chase.min.var, Detail::kChaseMinDistance);
    ConVars::SetFloat(g_chase.max.var, Detail::kChaseMaxDistance);
    g_chase.ready = true;
    LOG_INFO("visuals misc: chase camera cvars prepared (c_mindistance %.0f -> %.0f, "
             "c_maxdistance %.0f -> %.0f, cam_idealdist %.0f)",
             static_cast<double>(g_chase.min.value), static_cast<double>(Detail::kChaseMinDistance),
             static_cast<double>(g_chase.max.value), static_cast<double>(Detail::kChaseMaxDistance),
             static_cast<double>(g_chase.ideal.value));
    return true;
}

void RestoreChaseVars() {
    if (!g_chase.ready) return;
    const bool min_ok = g_chase.min.Restore();
    const bool max_ok = g_chase.max.Restore();
    const bool ideal_ok = g_chase.ideal.Restore();
    LOG_INFO("visuals misc: chase camera cvars restored (%d%d%d)", static_cast<int>(min_ok),
             static_cast<int>(max_ok), static_cast<int>(ideal_ok));
    g_chase = ChaseVars{};
}

// The slider -> cam_idealdist write-through. It runs on every frame the module is alive,
// whatever the camera plan says, so dragging the slider lands in the cvar even while third
// person is momentarily off (dead, grenade out, wall behind the head). Only a changed value is
// written, so this is one comparison per frame in the steady state.
void PublishDistance(const Settings& settings) {
    if (!InitChaseVars() || !g_chase.ideal.var) return;
    const float slider = Detail::DesiredDistance(settings);
    if (!(slider > 0.f)) return;
    if (std::fabs(g_chase.written - slider) <= 0.01f) return;
    if (!ConVars::SetFloat(g_chase.ideal.var, slider)) return;
    g_chase.written = slider;
    LOG_INFO("visuals misc: cam_idealdist = %.0f (from the Distance slider)", static_cast<double>(slider));
}

// The desired distance the camera works with: the current value of cam_idealdist, which the
// slider has just published. Reading the cvar (instead of the slider alone) keeps a single
// source of truth - if the game or the user changes cam_idealdist, the camera follows it. The
// value clipped by the world trace is never written back into either of them.
float DesiredDistance(const Settings& settings) {
    const float slider = Detail::DesiredDistance(settings);
    if (!g_chase.ready || !g_chase.ideal.var) return slider;
    const float live = g_chase.ideal.var->GetFloat();
    if (std::isfinite(live) && live > 0.f && live < 5000.f) return live;
    return slider;
}

// The two netvars the transparency needs. `PropManager::GetOffset` answers 0 for a property
// it could not find, and writing at offset 0 would overwrite the entity's vtable pointer, so
// the pair is resolved once and the feature simply stays off if the build does not have them.
bool ModelWritable() {
    static const bool writable = []() {
        const auto colour = PropManager::Get().GetOffset("DT_BaseEntity", "m_clrRender");
        const auto mode = PropManager::Get().GetOffset("DT_BaseEntity", "m_nRenderMode");
        if (colour <= 0 || mode <= 0) {
            LOG_WARN("visuals misc: third person transparency disabled (m_clrRender=%td "
                     "m_nRenderMode=%td)", colour, mode);
            return false;
        }
        return true;
    }();
    return writable;
}

// "Transparency" / "+ with nades": the local player's own model. The alpha goes into the
// entity's colour modulation and the render mode into kRenderTransTexture, which is the pair
// the client reads when it decides what to blend; a negative alpha means "not ours".
void ApplyLocalModel(CBaseEntity* model, int alpha) {
    if (!ModelWritable()) return;
    if (!model || alpha < 0) {
        if (g_model.active && g_model.entity) {
            __try {
                g_model.entity->m_clrRender() =
                    (g_model.entity->m_clrRender() & 0x00FFFFFFu) |
                    (static_cast<unsigned int>(g_model.alpha) << 24);
                g_model.entity->m_nRenderMode() = g_model.render_mode;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        g_model = OwnedModel{};
        return;
    }
    if (g_model.active && g_model.entity != model) {
        // A different model (respawn, team change): the previous one is no longer ours.
        ApplyLocalModel(nullptr, -1);
    }
    __try {
        const std::uint32_t colour = model->m_clrRender();
        const unsigned char current = static_cast<unsigned char>((colour >> 24) & 0xFFu);
        if (!g_model.active) {
            g_model.entity = model;
            g_model.alpha = current;
            g_model.render_mode = model->m_nRenderMode();
            g_model.active = true;
            LOG_INFO("visuals misc: third person transparency owns the local model "
                     "(alpha %d -> %d, render mode %d)", static_cast<int>(current), alpha,
                     g_model.render_mode);
        }
        model->m_clrRender() = (colour & 0x00FFFFFFu) | (static_cast<unsigned int>(alpha) << 24);
        if (alpha < 255)
            model->m_nRenderMode() = kRenderTransTexture;
        else
            model->m_nRenderMode() = g_model.render_mode ? g_model.render_mode : kRenderNormal;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_model = OwnedModel{};
    }
}
} // namespace

void PrepareChaseCvars() {
    if (!InitChaseVars()) return;
    // The first publish has to happen here as well: at inject the slider may already hold a
    // saved value, and cam_idealdist has to carry it before the first frame.
    PublishDistance(Detail::Snapshot());
}

// Called from the frame stage; see FrameStageNotify in sdk/hooks/framestage.cpp.
void ApplyThirdPerson() {
    // The distance goes into cam_idealdist first, and unconditionally: the camera below may
    // decide to stay in first person for this frame, but the slider has to reach the cvar.
    const Settings settings = Detail::Snapshot();
    PublishDistance(settings);

    IInput* input = Interfaces::Input;
    if (!input || !Interfaces::Engine) {
        g_camera_owned = false;
        return;
    }

    bool in_game = false;
    __try { in_game = Interfaces::Engine->IsInGame() && Interfaces::Engine->IsConnected(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { in_game = false; }

    CCSPlayer* local = ctx.local;
    bool alive = false;
    bool grenade_out = false;
    __try {
        alive = local && local->IsAlive();
        grenade_out = ctx.weapon_data && ctx.weapon_data->weapon_type == kWeaponTypeGrenade;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        alive = false;
        grenade_out = false;
    }

    const auto plan = Detail::Plan(settings, in_game, alive,
                                   settings.thirdperson_grenade && grenade_out);

    if (!alive && plan.dead_observer && local) {
        __try {
            if (local->m_iObserverMode() == kObserverInEye) local->m_iObserverMode() = kObserverChase;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    // The model transparency is independent of the camera: it follows the same "third person
    // is on and nothing told us to stand down" decision.
    if (local) ApplyLocalModel(local, Detail::LocalModelAlpha(settings, plan.active, grenade_out));

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

    // Desired distance: the Distance slider, published through cam_idealdist.
    const float desired = DesiredDistance(settings);

    // Walk backwards from the eye and stop at the first thing the world throws in the way,
    // so the camera never ends up inside a wall. main does the same trace and multiplies its
    // distance by the fraction; the engine only clamps against the map hull, which lets the
    // camera clip through props. This is the *actual* distance for this frame and it is
    // deliberately not stored anywhere: walking away from the wall restores the desired one.
    float distance = desired;
    if (Interfaces::EngineTrace && desired > 0.f) {
        Vector eye;
        __try { eye = local->GetEyePosition(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { eye = {}; }
        const Vector forward = Math::angle_vector(QAngle{angles.x, angles.y, 0.f});
        if (std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z)) {
            Ray_t ray;
            ray.Init(eye, eye - forward * desired,
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
            distance = Detail::ClampedDistance(desired, fraction);
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
                 "(desired=%.0f, distance=%.0f, cam_idealdist=%d)",
                 static_cast<double>(desired), static_cast<double>(distance),
                 static_cast<int>(g_chase.ready));
    }
}

void ResetThirdPerson() {
    if (g_camera_owned && Interfaces::Input) Interfaces::Input->ClearThirdPersonCamera();
    g_camera_owned = false;
    ApplyLocalModel(nullptr, -1);
    RestoreChaseVars();
    g_reported = false;
}
} // namespace VisualsMisc
