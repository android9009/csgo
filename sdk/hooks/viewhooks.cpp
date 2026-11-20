#include "hooks.hpp"
#include "../interfaces/interfaces.hpp"
#include "../../features/visuals/misc/misc.hpp"
#include "../../utils/log.hpp"

namespace HooksInternal {
namespace {
// Both detours hand the feature a plain argument and nothing else: the hook owns the
// virtual call, the feature owns the maths. An exception in the feature must cost one
// frame, never the camera, so each one keeps its own latch for the log.
bool g_viewmodel_fov_failed{};
bool g_aspect_failed{};
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
} // namespace HooksInternal
