#include "hooks.hpp"
#include "../interfaces/interfaces.hpp"
#include "../../features/visuals/misc/misc.hpp"
#include "../../utils/log.hpp"

namespace HooksInternal {
namespace {
// Both detours hand the feature a plain argument and nothing else: the hook owns the
// virtual call, the feature owns the maths. An exception in the feature must cost one
// frame, never the camera, so each one keeps its own latch for the log.
bool g_override_view_failed{};
bool g_viewmodel_fov_failed{};
} // namespace

// ClientMode::OverrideView - vtable slot 18 (main: OVERRIDE_VIEW_IDX).
//
// The original runs first: it fills the CViewSetup with the field of view, origin and
// angles the client computed for this frame, and third person then moves the origin
// backwards along the view direction, which is what main's run_alive does to CInput.
void __fastcall OverrideView(void* ecx, void* /*edx*/, void* setup) {
    using Fn = void (__thiscall*)(void*, void*);
    static Fn original = nullptr;
    if (!original) original = Hooks::GetOverrideViewOriginal();
    if (original) original(ecx, setup);
    if (!setup || g_override_view_failed) return;
    __try { VisualsMisc::OverrideView(setup); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_override_view_failed = true;
        LOG_ERROR("visuals misc: exception on the OverrideView hook 0x%08lX; third person disabled",
                  GetExceptionCode());
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
} // namespace HooksInternal
