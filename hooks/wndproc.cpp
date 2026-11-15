#include "hooks.hpp"
#include "../context.hpp"
#include "../utils/input_manager.hpp"
#include "../utils/config.hpp"
#include "../utils/log.hpp"
#include <windows.h>
#include "../third_party/fatality/port.hpp"

namespace HooksInternal {
static int LogGuiInputException(EXCEPTION_POINTERS* info) {
    if (info && info->ExceptionRecord)
        LOG_ERROR("Fatality input exception code=0x%08lX address=%p", info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool ForwardGuiInput(UINT message, WPARAM wparam, LPARAM lparam, bool open) {
    __try {
        return FatalityMenu::Input(message, wparam, lparam, open);
    } __except (LogGuiInputException(GetExceptionInformation())) {
        return false;
    }
}

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    // Check the menu key before passing input to the Fatality GUI.
    // Debounce rapid repeated key-up messages to avoid flicker.
    if (message == WM_KEYUP && wparam == static_cast<WPARAM>(Config::settings.menu_key)) {
        static DWORD last_toggle = 0;
        const DWORD now = GetTickCount();
        if (now - last_toggle >= 200) {
            last_toggle = now;
            Config::settings.show_menu = !Config::settings.show_menu;
            LOG_INFO("menu toggled: %s", Config::settings.show_menu ? "open" : "closed");
        }
    }
    if (ForwardGuiInput(message, wparam, lparam, Config::settings.show_menu))
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
