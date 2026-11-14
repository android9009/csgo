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

    ImGui::EndFrame();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    auto original = Hooks::GetEndSceneOriginal();
    return original ? original(dev) : D3D_OK;
}
}
