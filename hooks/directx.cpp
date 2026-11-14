#include "hooks.hpp"
#include "../utils/detourhook.hpp"
#include "../utils/config.hpp"
#include "../context.hpp"
#include "../features/menu/menu.h"
#include <d3d9.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

// ImGui context lives for the whole session; initialized lazily on first EndScene.
static bool g_imgui_ready = false;

bool Menu_IsOpen() { return Config::settings.show_menu; }

namespace HooksInternal {
long __stdcall EndScene(void* device) {
    auto* dev = static_cast<IDirect3DDevice9*>(device);
    if (!dev)
        return 0;

    // Save D3D state (same approach as the working Airflow menu) so ImGui
    // drawing doesn't depend on whatever state the game left behind and
    // doesn't corrupt the game's own state.
    IDirect3DStateBlock9* state_block = nullptr;
    const bool have_state = dev->CreateStateBlock(D3DSBT_ALL, &state_block) >= 0 && state_block;
    if (have_state)
        state_block->Capture();

    DWORD colorwrite{}, srgbwrite{};
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &colorwrite);
    dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgbwrite);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xffffffff);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, false);

    if (!g_imgui_ready) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(FindWindowA("Valve001", nullptr));
        ImGui_ImplDX9_Init(dev);
        load_fonts();  // Fatality-шрифты (Josefin Sans + Font Awesome), один раз
        g_imgui_ready = true;
    }
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (Config::settings.show_menu)
        draw_menu(dev);  // меню Fatality (CS2-стиль), визуальная копия

    // IMPORTANT: ImGui::Render() must be called to build the draw data.
    // EndFrame() alone leaves viewport->DrawDataP invalid, GetDrawData()
    // returns NULL and RenderDrawData() silently draws nothing.
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    if (have_state) {
        state_block->Apply();
        state_block->Release();
    }
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, colorwrite);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, srgbwrite);

    auto original = Hooks::GetEndSceneOriginal();
    return original ? original(dev) : D3D_OK;
}
}
