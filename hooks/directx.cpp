#include "hooks.hpp"
#include "../menu/preview/model_preview.hpp"
#include "../utils/config.hpp"
#include "../utils/log.hpp"
#include "../menu/menu.hpp"

bool Menu_IsOpen() { return Config::settings.show_menu; }

namespace HooksInternal {
static int LogRenderException(EXCEPTION_POINTERS* info) {
    static bool logged=false;
    if(!logged && info && info->ExceptionRecord) {
        logged=true;
        LOG_ERROR("Fatality render exception code=0x%08lX address=%p",info->ExceptionRecord->ExceptionCode,info->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
long __stdcall EndScene(void* device) {
    auto* dev = static_cast<IDirect3DDevice9*>(device);
    if (!dev) return D3D_OK;
    // Preserve the game's complete device state around the original GUI renderer.
    IDirect3DStateBlock9* state = nullptr;
    if (SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &state)) && state) state->Capture();
    __try {
        FatalityMenu::Render(dev, FindWindowA("Valve001", nullptr), Config::settings.show_menu);
    } __except (LogRenderException(GetExceptionInformation())) {
    }
    if (state) { state->Apply(); state->Release(); }
    auto original = Hooks::GetEndSceneOriginalFor(dev);
    return original ? original(dev) : D3D_OK;
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
