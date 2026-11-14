#include "hooks.hpp"
#include "../features/menu/menu.hpp"
#include "../context.hpp"
#include "../utils/input_manager.hpp"
#include <windows.h>
namespace HooksInternal {
LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_KEYUP) {
        if (wparam == VK_END) ctx.unload = true;
        Menu::OnKey(static_cast<int>(wparam));
    }
    if (auto original = Hooks::GetOriginalWndProc()) return CallWindowProcA(original, window, message, wparam, lparam);
    return DefWindowProcA(window, message, wparam, lparam);
}
}
