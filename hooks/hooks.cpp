#include "hooks.hpp"
#include "../utils/detourhook.hpp"
#include "../utils/log.hpp"
#include "../sdk/interfaces/interfaces.hpp"
#include "../utils/memory.hpp"
#include "../context.hpp"
#include <d3d9.h>
#include "../sdk/structs.hpp"
namespace Hooks {
static VMT_Hook client_mode_hook;
static VMT_Hook panel_hook;
static VMT_Hook device_hook;
static WNDPROC original_wndproc = nullptr;
static HWND game_window = nullptr;
CreateMoveFn GetCreateMoveOriginal() { return client_mode_hook.get_original<CreateMoveFn>(); }
PaintTraverseFn GetPaintTraverseOriginal() { return panel_hook.get_original<PaintTraverseFn>(); }
WNDPROC GetOriginalWndProc() { return original_wndproc; }
EndSceneFn GetEndSceneOriginal() { return device_hook.get_original<EndSceneFn>(); }
bool Setup() {
    if (!Interfaces::ClientMode || !Interfaces::Panel) return false;
    if (!client_mode_hook.attach(Interfaces::ClientMode, 24, reinterpret_cast<void*>(&HooksInternal::CreateMove))) return false;
    if (!panel_hook.attach(Interfaces::Panel, 41, reinterpret_cast<void*>(&HooksInternal::PaintTraverse))) {
        client_mode_hook.detach(); return false;
    }
    if (Interfaces::DirectDevice)
        device_hook.attach(Interfaces::DirectDevice, 42, reinterpret_cast<void*>(&HooksInternal::EndScene));
    // CS:GO's top-level window class is "Valve001"; no vfunc needed.
    game_window = FindWindowA("Valve001", nullptr);
    if (game_window)
        original_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(game_window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&HooksInternal::WndProc)));
    LOG_INFO("hooks installed (CreateMove, PaintTraverse%s)", device_hook.is_hooked() ? ", EndScene" : "");
    return true;
}
void Restore() {
    if (game_window && original_wndproc)
        SetWindowLongPtrA(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wndproc));
    original_wndproc = nullptr; game_window = nullptr;
    device_hook.detach(); panel_hook.detach(); client_mode_hook.detach();
    LOG_INFO("hooks restored");
}
}
