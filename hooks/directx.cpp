#include "hooks.hpp"
#include "../features/visuals/model_preview.hpp"
#include "../utils/config.hpp"
#include "../utils/log.hpp"
#include "../third_party/fatality/port.hpp"

bool Menu_IsOpen() { return Config::settings.show_menu; }

namespace HooksInternal {
long __stdcall EndScene(void* device) {
    auto* dev = static_cast<IDirect3DDevice9*>(device);
    if (!dev) return D3D_OK;
    // Preserve the game's complete device state around the original GUI renderer.
    IDirect3DStateBlock9* state = nullptr;
    if (SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &state)) && state) state->Capture();
    __try {
        FatalityMenu::Render(dev, FindWindowA("Valve001", nullptr), Config::settings.show_menu);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool logged = false;
        if (!logged) { LOG_ERROR("Fatality render raised a structured exception"); logged = true; }
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
