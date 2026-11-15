#include "hooks.hpp"
#include "../context.hpp"
#include "../utils/input_manager.hpp"
#include "../utils/config.hpp"
#include "../utils/log.hpp"
#include <windows.h>
#include <imgui.h>
#include <imgui_impl_win32.h>

// imgui_impl_win32.h intentionally hides this behind #if 0 — declare it ourselves.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace HooksInternal {
LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    // Toggle first: ImGui's handler may swallow key events, so the menu key
    // must be checked before it sees the message.
    if (message == WM_KEYUP && wparam == static_cast<WPARAM>(Config::settings.menu_key)) {
        Config::settings.show_menu = !Config::settings.show_menu;
        LOG_INFO("menu toggled: %s", Config::settings.show_menu ? "open" : "closed");
    }
    // Feed input to ImGui so the menu is interactive when open.
    if (ImGui::GetCurrentContext() &&
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
        return true;

    // While the menu is open, swallow input so the game doesn't react.
    if (Config::settings.show_menu &&
        (message == WM_KEYDOWN || message == WM_KEYUP ||
         message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN ||
         message == WM_LBUTTONUP || message == WM_RBUTTONDOWN ||
         message == WM_RBUTTONUP || message == WM_MOUSEWHEEL))
        return true;

    if (auto original = Hooks::GetOriginalWndProc()) return CallWindowProcA(original, window, message, wparam, lparam);
    return DefWindowProcA(window, message, wparam, lparam);
}
}
