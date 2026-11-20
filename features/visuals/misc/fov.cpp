#include "misc.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/entity/weapon.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
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
// Zoom bookkeeping for the current scope session. The game writes the zoom field of view
// itself; we only observe it, because the zoom table lives in the weapon's scripts and is not
// part of anything this build reads.
struct ZoomSession {
    bool open{};
    int reference{}; // the first level seen in this session
    int observed{};  // the last value the game itself wrote
} g_zoom;
// Last value handed to the engine, so the log carries state changes only.
float g_vm_applied{};
// Last `hide_viewmodel_in_zoom` value this module wrote, and the object it wrote it into.
void* g_scope_weapon{};
int g_scope_written{-1};

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
        LOG_INFO("visuals misc: fov %d -> %d (scope reference %d)", g_fov.fov, target_fov,
                 g_zoom.reference);
    }
    g_fov.fov = target_fov;
    g_fov.default_fov = target_default;
}

// "Force viewmodel in scope": main keeps the viewmodel visible by clearing the weapon info's
// own flag. Written only when it actually differs, and only on the weapon info of the weapon
// that is held now.
void ApplyScopeViewmodel(const Settings& settings, CCSWeaponData* data) {
    if (!data) {
        g_scope_weapon = nullptr;
        g_scope_written = -1;
        return;
    }
    const int wanted = settings.force_viewmodel_scope ? 0 : 1;
    if (g_scope_weapon == data && g_scope_written == wanted) return;
    __try { data->hide_viewmodel_in_zoom = wanted != 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    g_scope_weapon = data;
    g_scope_written = wanted;
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
    const float value = Detail::TargetAspectRatio(settings, original);
    if (value != original) {
        static float reported{};
        if (value != reported) {
            reported = value;
            LOG_INFO("visuals misc: aspect ratio %.3f -> %.3f", static_cast<double>(original),
                     static_cast<double>(value));
        }
    }
    return value;
}

void FrameStage(int stage) {
    const Settings settings = Detail::Snapshot();
    // FRAME_RENDER_START: the client has finished the net update for this frame, the
    // renderer has not sampled the camera, the fov or the weapon's zoom flag yet.
    if (stage != 5) return;

    CCSPlayer* local = ctx.local;
    LocalState state;
    if (!ReadLocal(local, state)) return;

    if (state.scoped) {
        if (!g_zoom.open) {
            g_zoom = ZoomSession{};
            g_zoom.open = true;
        }
        // A value that is neither ours nor the one we already know is the game writing a new
        // zoom level; that value becomes the session reference the first time it appears.
        if (state.fov != g_fov.fov && state.fov > 0) {
            if (g_zoom.reference <= 0) g_zoom.reference = state.fov;
            g_zoom.observed = state.fov;
        }
    } else {
        g_zoom = ZoomSession{};
    }

    const int world = Detail::WorldFov(settings);
    int target_default = 0;
    int target_fov = 0;
    if (state.scoped) {
        if (settings.fov_scope > 0.f || settings.force_second_zoom) {
            const int level_fov =
                Detail::ForcedFirstZoomFov(settings, g_zoom.reference, g_zoom.observed);
            target_fov = Detail::RemoveZoomFov(settings, level_fov, world);
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
    ApplyScopeViewmodel(settings, ctx.weapon_data);
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
    g_zoom = ZoomSession{};
    g_scope_weapon = nullptr;
    g_scope_written = -1;
    g_vm_applied = 0.f;
}
} // namespace VisualsMisc
