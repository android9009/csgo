#include "misc.hpp"
#include "../../../context.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/log.hpp"
#include <windows.h>
#include <cmath>

namespace VisualsMisc {
namespace {
// Written one frame before the renderer samples the field of view, and only when the
// value actually changes, so the game is not fighting a write every frame.
int g_applied_fov{};
// The player's own value, captured before the first override so switching the feature off
// restores the game instead of forcing a hard-coded 90.
int g_saved_default{};
bool g_saved{};
// Last override handed to the engine, so the log carries state changes only.
float g_vm_applied{};
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

void FrameStage(int stage) {
    // FRAME_RENDER_START: the client has finished the net update for this frame and the
    // renderer has not sampled the fov yet.
    if (stage != 5) return;
    const Settings settings = Detail::Snapshot();
    CCSPlayer* local = ctx.local;
    if (!local) return;

    int target = 0;
    bool scoped = false;
    __try {
        scoped = local->m_bIsScoped();
        target = Detail::TargetFov(settings, local->IsAlive(), scoped);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (target == 0) {
        // Feature off (or scoped): hand the fields back once, then leave them alone.
        if (g_applied_fov == 0 && !g_saved) return;
        __try {
            if (g_saved) local->m_iDefaultFOV() = g_saved_default;
            local->m_iFOV() = 0;
            local->m_iFOVStart() = 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        g_applied_fov = 0;
        g_saved = false;
        return;
    }

    if (target == g_applied_fov) return;
    __try {
        if (!g_saved) {
            g_saved_default = local->m_iDefaultFOV();
            g_saved = true;
        }
        local->m_iDefaultFOV() = target;
        local->m_iFOV() = target;
        local->m_iFOVStart() = target;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    g_applied_fov = target;
    LOG_INFO("visuals misc: fov set to %d (was %d)", target, g_saved_default);
}
} // namespace VisualsMisc
