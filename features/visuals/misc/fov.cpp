#include "misc.hpp"
#include "../../../context.hpp"
#include "../../../sdk/convars.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/entity/weapon.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/visual_runtime_state.hpp"
#include "../../../utils/log.hpp"
#include <windows.h>
#include <cmath>

namespace VisualsMisc {
namespace {
// What this module currently owns on the player, so switching a feature off hands the game's
// own values back instead of freezing whatever was last written.
struct OwnedFov {
    bool active{};
    int default_fov{};   // m_iDefaultFOV we wrote
    int fov{};           // m_iFOV we wrote
    int saved_default{}; // the game's m_iDefaultFOV before the first write
} g_fov;

// Zoom bookkeeping for the current scope session (Detail::ZoomSession keeps the rules).
Detail::ZoomSession g_zoom;


// Last value handed to the engine, so the log carries state changes only.
float g_vm_applied{};

// The aspect ratio the renderer is actually given. The slider publishes an integer percent,
// so the ratio itself steps by 0.01; `shown` walks to it once per frame instead of moving in
// those steps, and the value returned for the rest of the frame is constant.
struct AspectState {
    int frame{-1};
    float shown{};
    float target{};
    float reported{};
} g_aspect;

// The viewmodel changer's own offsets. The game reads them from viewmodel_offset_x/y/z, so
// writing those cvars is what actually moves the model: the world camera and the world fov
// are not involved. The values are captured once and handed back when the block is switched
// off or the module unloads.
struct OwnedViewmodel {
    bool ready{};
    bool attempted{};
    bool active{};
    ConVars::SavedVar x{};
    ConVars::SavedVar y{};
    ConVars::SavedVar z{};
    float written_x{};
    float written_y{};
    float written_z{};
} g_vm;

// Reading the player's fov/state can fault on a stale entity; the caller treats a fault as
// "nothing to do this frame".
struct LocalState {
    bool alive{};
    bool scoped{};
    int fov{};
    int default_fov{};
};

bool ReadLocal(CCSPlayer* local, LocalState& state) {
    if (!local) return false;
    __try {
        state.alive = local->IsAlive();
        state.scoped = local->m_bIsScoped();
        state.fov = local->m_iFOV();
        state.default_fov = local->m_iDefaultFOV();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

void RestoreFov(CCSPlayer* local) {
    if (!g_fov.active) return;
    __try {
        local->m_iDefaultFOV() = g_fov.saved_default;
        local->m_iFOV() = 0;
        local->m_iFOVStart() = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_fov = OwnedFov{};
        return;
    }
    LOG_INFO("visuals misc: fov handed back to the game (%d)", g_fov.saved_default);
    g_fov = OwnedFov{};
}

void ApplyFov(CCSPlayer* local, int target_default, int target_fov) {
    if (target_fov <= 0) {
        RestoreFov(local);
        return;
    }
    if (target_default <= 0) target_default = target_fov;
    if (g_fov.active && g_fov.fov == target_fov && g_fov.default_fov == target_default) return;
    __try {
        if (!g_fov.active) {
            g_fov.saved_default = local->m_iDefaultFOV();
            g_fov.active = true;
        }
        local->m_iDefaultFOV() = target_default;
        local->m_iFOV() = target_fov;
        local->m_iFOVStart() = target_fov;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (g_fov.fov != target_fov) {
        LOG_INFO("visuals misc: fov %d -> %d (scope levels %d/%d)", g_fov.fov, target_fov,
                 g_zoom.first_level, g_zoom.level);
    }
    g_fov.fov = target_fov;
    g_fov.default_fov = target_default;
}

// "Force viewmodel in scope" is NOT applied yet. main clears a `hide_viewmodel_in_zoom`
// byte on the weapon info (+0x1BD in its build); in this build the only writer/reader of a
// byte at that displacement belongs to the viewmodel, and this repo's hand-written
// CCSWeaponData places its own tail field at +0x1DD, so neither the object nor the offset is
// proven. Writing an unverified byte into live weapon state is not worth a checkbox, so the
// setting is published and ignored until the field is pinned down.

void RestoreViewmodelOffsets() {
    if (!g_vm.ready || !g_vm.active) return;
    const bool ok = g_vm.x.Restore() && g_vm.y.Restore() && g_vm.z.Restore();
    LOG_INFO("visuals misc: viewmodel offsets handed back to the game (%d)", static_cast<int>(ok));
    g_vm.active = false;
    g_vm.written_x = g_vm.written_y = g_vm.written_z = 0.f;
}

void ApplyViewmodelOffsets(const Settings& settings) {
    if (!g_vm.ready) {
        if (!Interfaces::ConVar || g_vm.attempted) return;
        g_vm.attempted = true;
        if (!g_vm.x.Capture("viewmodel_offset_x") || !g_vm.y.Capture("viewmodel_offset_y") ||
            !g_vm.z.Capture("viewmodel_offset_z")) {
            LOG_WARN("visuals misc: viewmodel_offset_* cvars not found; the viewmodel "
                     "position sliders stay inactive");
            return;
        }
        g_vm.ready = true;
    }
    if (!settings.vm_fov_enabled) {
        RestoreViewmodelOffsets();
        return;
    }
    const float x = Detail::ViewmodelOffset(settings.vm_x);
    const float y = Detail::ViewmodelOffset(settings.vm_y);
    const float z = Detail::ViewmodelOffset(settings.vm_z);
    if (!g_vm.active) {
        g_vm.active = true;
        LOG_INFO("visuals misc: viewmodel offsets %.1f/%.1f/%.1f (game %.1f/%.1f/%.1f)",
                 static_cast<double>(x), static_cast<double>(y), static_cast<double>(z),
                 static_cast<double>(g_vm.x.value), static_cast<double>(g_vm.y.value),
                 static_cast<double>(g_vm.z.value));
    }
    if (std::fabs(g_vm.written_x - x) > 0.001f) {
        if (ConVars::SetFloat(g_vm.x.var, x)) g_vm.written_x = x;
    }
    if (std::fabs(g_vm.written_y - y) > 0.001f) {
        if (ConVars::SetFloat(g_vm.y.var, y)) g_vm.written_y = y;
    }
    if (std::fabs(g_vm.written_z - z) > 0.001f) {
        if (ConVars::SetFloat(g_vm.z.var, z)) g_vm.written_z = z;
    }
}
} // namespace

float ViewModelFov(float original) {
    const Settings settings = Detail::Snapshot();
    const float value = Detail::TargetViewModelFov(settings, original);
    if (value != original) {
        if (value != g_vm_applied) {
            g_vm_applied = value;
            LOG_INFO("visuals misc: viewmodel fov %.0f -> %.0f", static_cast<double>(original),
                     static_cast<double>(value));
        }
    } else if (g_vm_applied != 0.f) {
        g_vm_applied = 0.f;
    }
    return value;
}

float ScreenAspectRatio(float original) {
    const Settings settings = Detail::Snapshot();
    const float target = Detail::TargetAspectRatio(settings, original);
    if (target == original) {
        // The slider is off (0 = the engine's own value): hand the game back immediately,
        // with no smoothing in the way.
        g_aspect = AspectState{};
        return target;
    }

    // One advance per rendered frame. The engine may ask for the ratio several times in one
    // frame (both eyes, the viewmodel, the shadow pass); every one of those has to get the
    // same value or the picture tears, so the state is keyed on the frame counter.
    int frame = 0;
    float dt = 0.f;
    __try {
        if (Interfaces::Globals) {
            frame = Interfaces::Globals->framecount;
            dt = Interfaces::Globals->frametime;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        frame = 0;
        dt = 0.f;
    }
    if (frame == 0) {
        // No frame clock available: correctness first, no smoothing.
        g_aspect.target = target;
        g_aspect.shown = target;
        return target;
    }
    if (frame != g_aspect.frame) {
        g_aspect.frame = frame;
        // A slider that moved faster than the smoothing is not chased forever: if the target
        // jumped by more than half the previous value the state is simply restarted.
        if (!(g_aspect.shown > 0.f) || std::fabs(target - g_aspect.shown) > g_aspect.shown)
            g_aspect.shown = target;
        else
            g_aspect.shown = Detail::SmoothAspectRatio(g_aspect.shown, target, dt);
        g_aspect.target = target;
    }
    // Log the slider target, not every smoothed frame: a drag is one line per moved pixel,
    // not one line per frame.
    if (target != g_aspect.reported) {
        g_aspect.reported = target;
        LOG_INFO("visuals misc: aspect ratio target %.3f (showing %.3f, engine %.3f)",
                 static_cast<double>(target), static_cast<double>(g_aspect.shown),
                 static_cast<double>(original));
    }
    return g_aspect.shown;
}

void FrameStage(int stage) {
    const Settings settings = Detail::Snapshot();
    // FRAME_RENDER_START: the client has finished the net update for this frame, the
    // renderer has not sampled the camera, the fov or the weapon's zoom flag yet.
    if (stage != 5) return;

    ApplyViewmodelOffsets(settings);

    CCSPlayer* local = ctx.local;
    LocalState state;
    if (!ReadLocal(local, state)) {
        // No local player this frame (main menu, disconnect): the overlay has nothing to
        // draw, and saying so here keeps a stale "scoped" from the last life off the screen.
        VisualRuntime::PublishScreen(VisualRuntime::ScreenState{});
        return;
    }

    // The scope overlay (menu/overlays/screen_overlay.hpp) draws on the render thread and may
    // not touch an entity, so the three values its maths needs are published here: whether
    // the player is scoped, the fov the picture is drawn with, and the weapon's current
    // inaccuracy in the unit the gap formula wants (radians). A missing weapon leaves the
    // inaccuracy at 0, which makes the Dynamic gap collapse to the centre instead of holding
    // a stale value from the previous weapon.
    {
        VisualRuntime::ScreenState screen;
        screen.scoped = state.scoped;
        screen.fov = state.fov > 1 ? static_cast<float>(state.fov)
                                   : static_cast<float>(Detail::WorldFov(settings));
        __try { screen.inaccuracy = ctx.weapon ? ctx.weapon->m_fAccuracyPenalty() : 0.f; }
        __except (EXCEPTION_EXECUTE_HANDLER) { screen.inaccuracy = 0.f; }
        if (!std::isfinite(screen.inaccuracy) || screen.inaccuracy < 0.f) screen.inaccuracy = 0.f;
        if (Interfaces::Engine) {
            int width = 0, height = 0;
            Interfaces::Engine->GetScreenSize(width, height);
            screen.width = width;
            screen.height = height;
        }
        VisualRuntime::PublishScreen(screen);
    }

    if (state.scoped) {
        const std::uintptr_t weapon = reinterpret_cast<std::uintptr_t>(ctx.weapon);
        g_zoom.Observe(weapon, state.fov, g_fov.active ? g_fov.fov : 0);
    } else if (g_zoom.open) {
        g_zoom.Reset();
    }

    const int world = Detail::WorldFov(settings);
    int target_default = 0;
    int target_fov = 0;
    if (state.scoped) {
        if (settings.fov_scope > 0.f || settings.force_second_zoom) {
            // Order: the game's level fov -> "Force second zoom" -> "Remove zoom".
            target_fov = Detail::ZoomFov(settings, g_zoom.first_level, g_zoom.level, world);
            // While scoped the weapon owns m_iDefaultFOV; only the zoomed value is ours.
            target_default = 0;
        }
    } else {
        target_fov = Detail::TargetFov(settings, state.alive, false);
        target_default = target_fov;
    }

    if (!state.alive && !g_fov.active) {
        // Dead players have no fov fields worth writing.
    } else {
        ApplyFov(local, target_default, target_fov);
    }
}

void ResetFov() {
    if (ctx.local && g_fov.active) {
        __try {
            ctx.local->m_iDefaultFOV() = g_fov.saved_default;
            ctx.local->m_iFOV() = 0;
            ctx.local->m_iFOVStart() = 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_fov = OwnedFov{};
    g_zoom.Reset();
    g_vm_applied = 0.f;
    g_aspect = AspectState{};
    VisualRuntime::PublishScreen(VisualRuntime::ScreenState{});
    RestoreViewmodelOffsets();
    g_vm = OwnedViewmodel{};
}
} // namespace VisualsMisc
