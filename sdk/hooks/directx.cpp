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

    // The overlay-render pattern of the working reference build (airflow,
    // origin/main: vmt_hooks.cpp::end_scene): a state block is captured on top
    // of a forced set of render states - the game leaves the device in a mode
    // the 2D overlay cannot draw with (alpha colour write, sRGB write, MSAA,
    // antialiased lines, sRGB sampler) - and everything is restored afterwards.
    //
    // The block is D3DSBT_ALL, not just PIXELSTATE: the evo renderer (unlike
    // ImGui's d3d9 backend) restores only its pixel states, so the transforms,
    // FVF and stream bindings it sets would leak into the game's own VGUI/
    // panorama rendering (team select, console drew broken on the next frame)
    // with a pixel-only block.
    IDirect3DStateBlock9* state = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &state)) || !state)
        return original(dev);

    // Render states we force and put back manually (the block holds the forced
    // values, so a plain Apply would keep them).
    DWORD colorwrite{}, srgbwrite{}, multisample{}, antialias{};
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &colorwrite);
    dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgbwrite);
    dev->GetRenderState(D3DRS_MULTISAMPLEANTIALIAS, &multisample);
    dev->GetRenderState(D3DRS_ANTIALIASEDLINEENABLE, &antialias);

    // State that no state block covers: the FVF, the index buffer and the
    // sampler states we force (address modes + sRGB texture).
    DWORD fvf = 0; // GetFVF takes a DWORD* (SetFVF wants a UINT)
    dev->GetFVF(&fvf);
    IDirect3DIndexBuffer9* indices = nullptr;
    dev->GetIndices(&indices);
    DWORD addr_u{}, addr_v{}, addr_w{}, srgb_tex{};
    dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &addr_u);
    dev->GetSamplerState(0, D3DSAMP_ADDRESSV, &addr_v);
    dev->GetSamplerState(0, D3DSAMP_ADDRESSW, &addr_w);
    dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &srgb_tex);

    dev->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, 0);
    dev->SetRenderState(D3DRS_ANTIALIASEDLINEENABLE, 0);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xffffffff);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, 0);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);

    state->Capture();

    __try {
        RenderWithCatch(dev, FindWindowA("Valve001", nullptr), Config::settings.show_menu);
    } __except (LogRenderException(GetExceptionInformation())) {
    }

    state->Apply();
    state->Release();

    // Restore the game's state: the ALL block covers pixel/transform/lighting/
    // viewport, the manual tail covers the forced states plus everything a
    // state block cannot see.
    dev->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, multisample);
    dev->SetRenderState(D3DRS_ANTIALIASEDLINEENABLE, antialias);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, colorwrite);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, srgbwrite);
    dev->SetFVF(static_cast<UINT>(fvf));
    dev->SetIndices(indices);
    if (indices) indices->Release();
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, addr_u);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, addr_v);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSW, addr_w);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, srgb_tex);
    // The scissor rect rides in the D3DSBT_VIEWPORT part of the ALL block, so
    // the Apply above already put the game's back.

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
