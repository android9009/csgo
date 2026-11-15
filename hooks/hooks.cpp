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
static VMT_Hook client_hook;
static VMT_Hook view_render_hook;
static VMT_Hook model_render_hook;
static WNDPROC original_wndproc = nullptr;
static HWND game_window = nullptr;
CreateMoveFn GetCreateMoveOriginal() { return client_mode_hook.get_original<CreateMoveFn>(); }
PaintTraverseFn GetPaintTraverseOriginal() { return panel_hook.get_original<PaintTraverseFn>(); }
WNDPROC GetOriginalWndProc() { return original_wndproc; }
EndSceneFn GetEndSceneOriginal() { return device_hook.get_original<EndSceneFn>(); }
FrameStageNotifyFn GetFrameStageNotifyOriginal() { return client_hook.get_original<FrameStageNotifyFn>(); }
SceneEndFn GetSceneEndOriginal() { return view_render_hook.get_original<SceneEndFn>(); }
DrawModelExecuteFn GetDrawModelExecuteOriginal() { return model_render_hook.get_original<DrawModelExecuteFn>(); }
bool Setup() {
    // Menu hooks first: the menu only needs the game window (WndProc) and the
    // D3D9 device (EndScene), so it must work even if other interfaces fail.
    // CS:GO's top-level window class is "Valve001"; no vfunc needed.
    game_window = FindWindowA("Valve001", nullptr);
    if (game_window)
        original_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(game_window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&HooksInternal::WndProc)));
    else
        LOG_ERROR("game window not found (Valve001); menu toggle unavailable");
    if (Interfaces::DirectDevice)
        device_hook.attach(Interfaces::DirectDevice, 42, reinterpret_cast<void*>(&HooksInternal::EndScene));
    else
        LOG_ERROR("DirectDevice is null; EndScene hook not installed, menu will not render");

    // Feature hooks are best-effort: missing interfaces only disable features.
    if (Interfaces::ClientMode)
        client_mode_hook.attach(Interfaces::ClientMode, 24, reinterpret_cast<void*>(&HooksInternal::CreateMove));
    else
        LOG_WARN("ClientMode is null; CreateMove not hooked");
    if (Interfaces::Panel)
        panel_hook.attach(Interfaces::Panel, 41, reinterpret_cast<void*>(&HooksInternal::PaintTraverse));
    else
        LOG_WARN("Panel is null; PaintTraverse not hooked");
    if (Interfaces::Client)
        client_hook.attach(Interfaces::Client, 37, reinterpret_cast<void*>(&HooksInternal::FrameStageNotify));
    if (Interfaces::ViewRender)
        view_render_hook.attach(Interfaces::ViewRender, 6, reinterpret_cast<void*>(&HooksInternal::SceneEnd));
    if (Interfaces::ModelRender)
        model_render_hook.attach(Interfaces::ModelRender, 21, reinterpret_cast<void*>(&HooksInternal::DrawModelExecute));

    LOG_INFO("hooks installed (WndProc=%d EndScene=%d CreateMove=%d PaintTraverse=%d FrameStageNotify=%d SceneEnd=%d DrawModelExecute=%d)",
        original_wndproc != nullptr, device_hook.is_hooked(), client_mode_hook.is_hooked(),
        panel_hook.is_hooked(), client_hook.is_hooked(), view_render_hook.is_hooked(),
        model_render_hook.is_hooked());
    // The cheat stays loaded as long as the menu can be toggled; feature hooks
    // are optional until features land.
    return original_wndproc != nullptr;
}
void Restore() {
    if (game_window && original_wndproc)
        SetWindowLongPtrA(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wndproc));
    original_wndproc = nullptr; game_window = nullptr;
    model_render_hook.detach(); view_render_hook.detach(); client_hook.detach();
    device_hook.detach(); panel_hook.detach(); client_mode_hook.detach();
    LOG_INFO("hooks restored");
}
}
