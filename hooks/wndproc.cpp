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
    // Never let GUI input handling consume the game's shutdown messages.
    // Avoid waiting or unloading here: WndProc can be executing inside a hook.
    if (message == WM_CLOSE || message == WM_DESTROY || message == WM_NCDESTROY || message == WM_QUERYENDSESSION || message == WM_ENDSESSION) {
        if (auto original = Hooks::GetOriginalWndProc()) return CallWindowProcA(original, window, message, wparam, lparam);
        return DefWindowProcA(window, message, wparam, lparam);
    }
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
    const bool consumed = ForwardGuiInput(message, wparam, lparam, Config::settings.show_menu);
    // Always deliver releases: a key/button may have gone down before opening
    // the menu. Dropping its up event leaves the engine's +command latched.
    const bool release = message == WM_KEYUP || message == WM_SYSKEYUP ||
        message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
        message == WM_MBUTTONUP || message == WM_XBUTTONUP;
    const bool keyboard = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    if (!release && Config::settings.show_menu &&
        ((keyboard && consumed) || message == WM_CHAR ||
         (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)))
        return true;

    if (auto original = Hooks::GetOriginalWndProc()) return CallWindowProcA(original, window, message, wparam, lparam);
    return DefWindowProcA(window, message, wparam, lparam);
}
}
