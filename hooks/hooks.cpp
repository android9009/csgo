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
    if (!Interfaces::ClientMode || !Interfaces::Panel) return false;
    if (!client_mode_hook.attach(Interfaces::ClientMode, 24, reinterpret_cast<void*>(&HooksInternal::CreateMove))) return false;
    if (!panel_hook.attach(Interfaces::Panel, 41, reinterpret_cast<void*>(&HooksInternal::PaintTraverse))) {
        client_mode_hook.detach(); return false;
    }
    if (Interfaces::DirectDevice)
        device_hook.attach(Interfaces::DirectDevice, 42, reinterpret_cast<void*>(&HooksInternal::EndScene));
    // FrameStageNotify (IBaseClientDll idx 37), SceneEnd (IViewRender idx 6) and
    // DrawModelExecute (IVModelRender idx 21) are best-effort: the core hooks
    // above are enough for the menu, these unlock features/visuals + rage.
    if (Interfaces::Client)
        client_hook.attach(Interfaces::Client, 37, reinterpret_cast<void*>(&HooksInternal::FrameStageNotify));
    if (Interfaces::ViewRender)
        view_render_hook.attach(Interfaces::ViewRender, 6, reinterpret_cast<void*>(&HooksInternal::SceneEnd));
    if (Interfaces::ModelRender)
        model_render_hook.attach(Interfaces::ModelRender, 21, reinterpret_cast<void*>(&HooksInternal::DrawModelExecute));
    // CS:GO's top-level window class is "Valve001"; no vfunc needed.
    game_window = FindWindowA("Valve001", nullptr);
    if (game_window)
        original_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(game_window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&HooksInternal::WndProc)));
    LOG_INFO("hooks installed (CreateMove, PaintTraverse%s%s%s%s)",
        device_hook.is_hooked() ? ", EndScene" : "",
        client_hook.is_hooked() ? ", FrameStageNotify" : "",
        view_render_hook.is_hooked() ? ", SceneEnd" : "",
        model_render_hook.is_hooked() ? ", DrawModelExecute" : "");
    return true;
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
