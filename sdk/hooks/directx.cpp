#include "hooks.hpp"
#include "../../menu/preview/model_preview.hpp"
#include "../../utils/config.hpp"
#include "../../utils/log.hpp"
#include "../../menu/menu.hpp"
#include <exception>

namespace HooksInternal {
namespace {
// Safety net for the render hook: a fault inside the overlay render must
// never tear the game down (a C++ exception unwinding through the d3d9 vtable,
// or an unhandled SEH fault, would crash it). SEH and C++ handling cannot
// coexist in one function (C2712/C2713), so the C++ catch lives here and the
// SEH net stays in EndScene. A looping fault is reported once per process -
// the first one is the interesting one.
void RenderWithCatch(IDirect3DDevice9* dev, HWND hwnd, bool open) {
    static bool logged = false;
    try {
        FatalityMenu::Render(dev, hwnd, open);
    } catch (const std::exception& e) {
        if (!logged) { logged = true; LOG_ERROR("menu render C++ exception: %s", e.what()); }
    } catch (...) {
        if (!logged) { logged = true; LOG_ERROR("menu render C++ exception: unknown type"); }
    }
}
} // namespace

int LogRenderException(EXCEPTION_POINTERS* info) {
    static bool logged = false;
    if (!logged && info && info->ExceptionRecord) {
        logged = true;
        LOG_ERROR("menu render fault: 0x%08lX", (unsigned long)info->ExceptionRecord->ExceptionCode);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
long __stdcall EndScene(void* device) {
    auto* dev = static_cast<IDirect3DDevice9*>(device);
    if (!dev) return D3D_OK;
    auto original = Hooks::GetEndSceneOriginalFor(dev);
    if (!original) return D3D_OK;

    // The state handling of the build that worked in-game: a full D3DSBT_ALL
    // block captured over the game's state, applied again after our render.
    // On top of it - and only on top, because it happens AFTER the capture -
    // the forced render states of the reference build (airflow,
    // origin/main: vmt_hooks.cpp::end_scene): the game leaves the device in a
    // mode the 2D overlay cannot draw with (alpha colour write, sRGB write,
    // MSAA, antialiased lines, sRGB sampler). The Apply puts every state back
    // exactly as the game had it, so nothing manual needs restoring.
    IDirect3DStateBlock9* state = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &state)) || !state)
        return original(dev);

    state->Capture();

    dev->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, 0);
    dev->SetRenderState(D3DRS_ANTIALIASEDLINEENABLE, 0);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xffffffff);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, 0);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);

    __try {
        RenderWithCatch(dev, FindWindowA("Valve001", nullptr), Config::settings.show_menu);
    } __except (LogRenderException(GetExceptionInformation())) {
    }

    state->Apply();
    state->Release();

    return original(dev);
}
long __stdcall Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* params) {
    auto original = Hooks::GetResetOriginalFor(dev);
    if (!original) return D3DERR_INVALIDCALL;
    ModelPreview::BeforeReset();
    FatalityMenu::BeforeReset(dev);
    const auto result = original(dev, params);
    FatalityMenu::AfterReset(dev, result);
    return result;
}
}
