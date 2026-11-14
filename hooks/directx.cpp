#include "hooks.hpp"
#include "../utils/detourhook.hpp"
#include "../utils/config.hpp"
#include "../context.hpp"
#include <d3d9.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

// ImGui context lives for the whole session; initialized lazily on first EndScene.
static bool g_imgui_ready = false;

bool Menu_IsOpen() { return Config::settings.show_menu; }

static void Menu_Draw() {
    if (!Config::settings.show_menu) return;
    ImGui::SetNextWindowSize(ImVec2(420, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("csgo_base", &Config::settings.show_menu,
                     ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("main")) {
            if (ImGui::BeginTabItem("Visuals")) {
                ImGui::Checkbox("Player ESP", &Config::settings.player_esp);
                ImGui::Checkbox("Player labels", &Config::settings.player_names);
                ImGui::ColorEdit4("Enemy color", reinterpret_cast<float*>(&Config::settings.enemy_color));
                ImGui::ColorEdit4("Friendly color", reinterpret_cast<float*>(&Config::settings.friendly_color));
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Misc")) {
                ImGui::Checkbox("Bunnyhop", &Config::settings.bunnyhop);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Config")) {
                if (ImGui::Button("Save")) Config::Save();
                ImGui::SameLine();
                if (ImGui::Button("Load")) Config::Load();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

namespace HooksInternal {
long __stdcall EndScene(void* device) {
    auto* dev = static_cast<IDirect3DDevice9*>(device);
    if (!g_imgui_ready) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(FindWindowA("Valve001", nullptr));
        ImGui_ImplDX9_Init(dev);
        g_imgui_ready = true;
    }
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    Menu_Draw();

    ImGui::EndFrame();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    auto original = Hooks::GetEndSceneOriginal();
    return original ? original(dev) : D3D_OK;
}
}
