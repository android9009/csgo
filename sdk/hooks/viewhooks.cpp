#include "hooks.hpp"
#include "../interfaces/interfaces.hpp"
#include "../../features/visuals/misc/misc.hpp"
#include "../../utils/log.hpp"
#include <cstdint>

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
// The original is `float __thiscall(int width, int height)`, so the fastcall thunk receives
// the object in ecx, the width in edx and the height on the stack. The renderer asks once per
// view, and the value it gets back is what decides the horizontal stretch, which is why
// "Aspect ratio" is a real hook and not a post-process.
float __fastcall GetScreenAspectRatio(void* ecx, void* edx, int height) {
    using Fn = float (__thiscall*)(void*, int, int);
    static Fn original = nullptr;
    if (!original) original = Hooks::GetScreenAspectRatioOriginal();
    const int width = static_cast<int>(reinterpret_cast<std::intptr_t>(edx));
    const float value = original ? original(ecx, width, height) : 0.f;
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
