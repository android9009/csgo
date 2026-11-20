#include "hooks.hpp"
#include "../interfaces/interfaces.hpp"
#include "../../features/visuals/misc/misc.hpp"
#include "../../context.hpp"
#include "../../utils/detourhook.hpp"
#include "../../utils/log.hpp"
#include "../../utils/patterns/patterns.hpp"

namespace HooksInternal {
namespace {
// Both detours hand the feature a plain argument and nothing else: the hook owns the
// virtual call, the feature owns the maths. An exception in the feature must cost one
// frame, never the camera, so each one keeps its own latch for the log.
bool g_viewmodel_fov_failed{};
bool g_aspect_failed{};

// The two inline detours. `*_original` is the trampoline MinHook hands back; the detours
// below live in the same translation unit, so they read it directly instead of going
// through Hooks:: (there is exactly one of each).
Detour_Hook g_scope_mask_hook;
Detour_Hook g_viewmodel_hook;
void* g_scope_mask_original{};
void* g_viewmodel_original{};
bool g_scope_mask_failed{};
bool g_viewmodel_failed{};
} // namespace

// IEngineClient::GetScreenAspectRatio - vtable slot 101 (main: GET_SCREEN_ASPECT_RATIO_IDX).
//
// The original is `float __thiscall(int width, int height)`: `this` in ecx, both integers on
// the stack, callee pops 8 bytes (`ret 8`, confirmed in the supplied engine.dll). The thunk
// is __fastcall - `this` in ecx, an unused edx, and both integers taken from the stack - so
// that it pops exactly the 8 bytes the caller pushed. The previous version declared a single
// stack argument, popped 4, and the first frame returned into a shifted stack: instant crash.
float __fastcall GetScreenAspectRatio(void* ecx, void* edx, int width, int height) {
    using Fn = float (__thiscall*)(void*, int, int);
    static Fn original = nullptr;
    if (!original) original = Hooks::GetScreenAspectRatioOriginal();
    (void)edx; // a __thiscall callee never reads it
    const float value = original ? original(ecx, width, height) : 0.f;
    // One-shot proof that slot 101 really is the aspect-ratio getter: the log line carries the
    // width/height the engine asked about plus the value it would have used on its own.
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOG_INFO("visuals misc: GetScreenAspectRatio(%d, %d) = %.4f", width, height, value);
    }
    if (g_aspect_failed) return value;
    __try { return VisualsMisc::ScreenAspectRatio(value); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_aspect_failed = true;
        LOG_ERROR("visuals misc: exception on the aspect-ratio hook 0x%08lX; original value returned",
                  GetExceptionCode());
        return value;
    }
}

// ClientMode::GetViewModelFOV - vtable slot 35 (main: GET_VIEWMODEL_FOV_IDX).
float __fastcall GetViewModelFov(void* ecx, void* /*edx*/) {
    using Fn = float (__thiscall*)(void*);
    static Fn original = nullptr;
    if (!original) original = Hooks::GetViewModelFovOriginal();
    const float value = original ? original(ecx) : 0.f;
    if (g_viewmodel_fov_failed) return value;
    __try { return VisualsMisc::ViewModelFov(value); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_viewmodel_fov_failed = true;
        LOG_ERROR("visuals misc: exception on the GetViewModelFOV hook 0x%08lX; original value returned",
                  GetExceptionCode());
        return value;
    }
}

// C_CSPlayer::UpdatePostScreenEffects - the pass the client runs after the frame's post
// processing. While the local player is the one being processed, "Remove scope" clears the
// scoped flag for the duration of the call: the pass then skips the scope overlay, which is
// exactly the black mask, and the four lines this module draws are all that is left. The flag
// is restored immediately afterwards so the weapon, the sensitivity multiplier and the fov
// all keep behaving as if the player were still scoped.
//
// `this` is compared against the local player rather than written blindly: if the pass is
// ever invoked for another entity (or the pointer is not a player at all) the detour only
// forwards the call.
void __fastcall UpdatePostScreenEffects(void* ecx, void* /*edx*/) {
    using Fn = void(__thiscall*)(void*);
    const auto original = reinterpret_cast<Fn>(g_scope_mask_original);
    if (!original) return;
    if (g_scope_mask_failed || !VisualsMisc::RemoveScopeActive()) {
        original(ecx);
        return;
    }
    CCSPlayer* local = ctx.local;
    if (!local || ecx != reinterpret_cast<void*>(local)) {
        original(ecx);
        return;
    }
    bool cleared = false;
    __try {
        if (local->m_bIsScoped()) {
            local->m_bIsScoped() = false;
            cleared = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_scope_mask_failed = true;
        LOG_ERROR("visuals misc: exception clearing the scoped flag 0x%08lX; the game's scope "
                  "mask stays for this session", GetExceptionCode());
        cleared = false;
    }
    original(ecx);
    if (cleared) {
        __try { local->m_bIsScoped() = true; } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_scope_mask_failed = true;
        }
    }
}

// C_BaseViewModel::CalcViewModelView - the base pose of the viewmodel for this frame is
// written into `eye_pos` and `eye_angles` by the original, and the three rotation sliders are
// added afterwards. Adding them after the original (and not before) is what keeps the pose
// from accumulating: every frame starts from the game's own value, never from last frame's
// rotated one.
void __fastcall CalcViewModelView(void* ecx, void* /*edx*/, void* owner, float* eye_pos,
                                  float* eye_angles) {
    using Fn = void(__thiscall*)(void*, void*, float*, float*);
    const auto original = reinterpret_cast<Fn>(g_viewmodel_original);
    if (!original) return;
    original(ecx, owner, eye_pos, eye_angles);
    if (g_viewmodel_failed || !eye_angles) return;
    __try {
        const VisualsMisc::Settings settings = VisualsMisc::Detail::Snapshot();
        if (!settings.vm_fov_enabled) return;
        const float roll = VisualsMisc::Detail::ViewmodelAngle(settings.vm_roll);
        const float yaw = VisualsMisc::Detail::ViewmodelAngle(settings.vm_yaw);
        const float pitch = VisualsMisc::Detail::ViewmodelAngle(settings.vm_pitch);
        if (roll == 0.f && yaw == 0.f && pitch == 0.f) return;
        // Same axis order as QAngle: pitch on x, yaw on y, roll on z. The viewmodel is placed
        // with these angles, the camera is not: the world keeps its own view.
        eye_angles[0] += pitch;
        eye_angles[1] += yaw;
        eye_angles[2] += roll;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_viewmodel_failed = true;
        LOG_ERROR("visuals misc: exception in the CalcViewModelView detour 0x%08lX; the "
                  "viewmodel rotation stays off", GetExceptionCode());
    }
}

void InstallVisualMiscDetours() {
    const auto scope_mask = Patterns::Get("update_postscreen_effects");
    if (!scope_mask) {
        LOG_WARN("visuals misc: update_postscreen_effects did not resolve; \"Remove scope\" "
                 "keeps the game's own scope mask");
    } else if (g_scope_mask_hook.attach(reinterpret_cast<void*>(scope_mask),
                                        reinterpret_cast<void*>(&UpdatePostScreenEffects),
                                        &g_scope_mask_original)) {
        LOG_INFO("visuals misc: scope-mask detour attached at 0x%08lX",
                 static_cast<unsigned long>(scope_mask));
    } else {
        LOG_WARN("visuals misc: the scope-mask detour could not be created");
    }

    const auto calc_view = Patterns::Get("calc_viewmodel_view");
    if (!calc_view) {
        LOG_WARN("visuals misc: calc_viewmodel_view did not resolve; the viewmodel rotation "
                 "sliders stay inactive");
    } else if (g_viewmodel_hook.attach(reinterpret_cast<void*>(calc_view),
                                       reinterpret_cast<void*>(&CalcViewModelView),
                                       &g_viewmodel_original)) {
        LOG_INFO("visuals misc: viewmodel control detour attached at 0x%08lX",
                 static_cast<unsigned long>(calc_view));
    } else {
        LOG_WARN("visuals misc: the viewmodel detour could not be created");
    }
}

void RemoveVisualMiscDetours() {
    g_scope_mask_hook.detach();
    g_viewmodel_hook.detach();
}
} // namespace HooksInternal
