#include "hooks.hpp"
#include "../visual_runtime_state.hpp"
#include "../../utils/detourhook.hpp"
#include "../../utils/log.hpp"
#include "../interfaces/interfaces.hpp"
#include "../../utils/memory.hpp"
#include "../../context.hpp"
#include "../../utils/config.hpp"
#include "../../features/skins/local_inventory.hpp"
#include "../../features/visuals/shot_chams.hpp"
#include "../../features/visuals/world_modulation.hpp"
#include "../../features/visuals/world_fog.hpp"
#include "../datatypes/c_user_cmd.hpp"
#include <d3d9.h>
#include "../structs.hpp"
namespace Hooks {

static VMT_Hook panel_hook;
static VMT_Hook cursor_hook;
static void __fastcall LockCursor(void* surface, void*) {
    if (Config::settings.show_menu) {
        static_cast<ISurface*>(surface)->UnlockCursor();
        return;
    }
    using Fn = void (__thiscall*)(void*);
    if (auto original = cursor_hook.get_original<Fn>()) original(surface);
}
static VMT_Hook device_hook;
static VMT_Hook device_d3d9_hook;
static VMT_Hook device_ex_hook;
static VMT_Hook reset_hook, reset_d3d9_hook, reset_ex_hook;
static VMT_Hook client_hook;
static VMT_Hook client_mode_hook;
static VMT_Hook view_render_hook;
static VMT_Hook model_render_hook;
static WNDPROC original_wndproc = nullptr;
static HWND game_window = nullptr;
// Dummy devices are leaked intentionally: their vtables (in d3d9.dll) are the
// hook targets and must outlive Setup. CS:GO may render with D3D9Ex while
// g_ppDirect3DDevice9 points at a plain D3D9 device, so EndScene is hooked on
// every distinct vtable (captured device + dummy D3D9 + dummy D3D9Ex).
static void* dummy_d3d9 = nullptr;
static void* dummy_ex = nullptr;
static void* direct_vtable = nullptr;
static void* d3d9_vtable = nullptr;
static void* ex_vtable = nullptr;
PaintTraverseFn GetPaintTraverseOriginal() { return panel_hook.get_original<PaintTraverseFn>(); }
WNDPROC GetOriginalWndProc() { return original_wndproc; }
EndSceneFn GetEndSceneOriginal() { return device_hook.get_original<EndSceneFn>(); }
EndSceneFn GetEndSceneOriginalFor(void* device) {
    if (!device) return nullptr;
    void* vtable = *reinterpret_cast<void**>(device);
    if (device_hook.is_hooked() && vtable == direct_vtable) return device_hook.get_original<EndSceneFn>();
    if (device_d3d9_hook.is_hooked() && vtable == d3d9_vtable) return device_d3d9_hook.get_original<EndSceneFn>();
    if (device_ex_hook.is_hooked() && vtable == ex_vtable) return device_ex_hook.get_original<EndSceneFn>();
    return nullptr;
}
ResetFn GetResetOriginalFor(void* device) {
    if (!device) return nullptr;
    void* vtable = *reinterpret_cast<void**>(device);
    if (reset_hook.is_hooked() && vtable == direct_vtable) return reset_hook.get_original<ResetFn>();
    if (reset_d3d9_hook.is_hooked() && vtable == d3d9_vtable) return reset_d3d9_hook.get_original<ResetFn>();
    if (reset_ex_hook.is_hooked() && vtable == ex_vtable) return reset_ex_hook.get_original<ResetFn>();
    return nullptr;
}
FrameStageNotifyFn GetFrameStageNotifyOriginal() { return client_hook.get_original<FrameStageNotifyFn>(); }
CreateMoveFn GetCreateMoveOriginal() { return client_mode_hook.get_original<CreateMoveFn>(); }
SceneEndFn GetSceneEndOriginal() { return view_render_hook.get_original<SceneEndFn>(); }
DrawModelExecuteFn GetDrawModelExecuteOriginal() { return model_render_hook.get_original<DrawModelExecuteFn>(); }
static void* CreateDummyDevice(bool ex) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "csgo_base_dummy";
    RegisterClassA(&wc);
    HWND wnd = CreateWindowExA(0, wc.lpszClassName, "dummy", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!wnd) return nullptr;
    D3DPRESENT_PARAMETERS params{};
    params.BackBufferWidth = 64;
    params.BackBufferHeight = 64;
    params.BackBufferFormat = D3DFMT_UNKNOWN;
    params.BackBufferCount = 1;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow = wnd;
    params.Windowed = TRUE;
    params.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    void* device = nullptr;
    if (ex) {
        IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d9) { DestroyWindow(wnd); return nullptr; }
        IDirect3D9Ex* d3d = nullptr;
        if (FAILED(d3d9->QueryInterface(__uuidof(IDirect3D9Ex), reinterpret_cast<void**>(&d3d))) || !d3d) {
            d3d9->Release(); DestroyWindow(wnd); return nullptr;
        }
        d3d9->Release();
        IDirect3DDevice9Ex* dev = nullptr;
        if (FAILED(d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, nullptr, &dev)) || !dev) {
            d3d->Release(); DestroyWindow(wnd); return nullptr;
        }
        device = dev;
        d3d->Release(); // device keeps d3d9 alive; vtable outlives both
    } else {
        IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) { DestroyWindow(wnd); return nullptr; }
        IDirect3DDevice9* dev = nullptr;
        if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &dev)) || !dev) {
            d3d->Release(); DestroyWindow(wnd); return nullptr;
        }
        device = dev;
        d3d->Release();
    }
    DestroyWindow(wnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return device;
}
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
    LOG_INFO("game window=%p DirectDevice=%p", game_window, Interfaces::DirectDevice);

    // Hook EndScene on every distinct device vtable (deduped by vtable pointer).
    if (Interfaces::DirectDevice) {
        direct_vtable = *reinterpret_cast<void**>(Interfaces::DirectDevice);
        device_hook.attach(Interfaces::DirectDevice, 42, reinterpret_cast<void*>(&HooksInternal::EndScene));
        reset_hook.attach(Interfaces::DirectDevice, 16, reinterpret_cast<void*>(&HooksInternal::Reset));
        LOG_INFO("captured device vtable=%p hooked=%d", direct_vtable, device_hook.is_hooked());
    } else {
        LOG_ERROR("DirectDevice is null; captured-device EndScene hook skipped");
    }
    dummy_d3d9 = CreateDummyDevice(false);
    if (dummy_d3d9) {
        d3d9_vtable = *reinterpret_cast<void**>(dummy_d3d9);
        if (d3d9_vtable != direct_vtable) {
            device_d3d9_hook.attach(dummy_d3d9, 42, reinterpret_cast<void*>(&HooksInternal::EndScene));
            reset_d3d9_hook.attach(dummy_d3d9, 16, reinterpret_cast<void*>(&HooksInternal::Reset));
        }
        LOG_INFO("dummy D3D9 vtable=%p hooked=%d", d3d9_vtable, device_d3d9_hook.is_hooked());
    }
    dummy_ex = CreateDummyDevice(true);
    if (dummy_ex) {
        ex_vtable = *reinterpret_cast<void**>(dummy_ex);
        if (ex_vtable != direct_vtable && ex_vtable != d3d9_vtable) {
            device_ex_hook.attach(dummy_ex, 42, reinterpret_cast<void*>(&HooksInternal::EndScene));
            reset_ex_hook.attach(dummy_ex, 16, reinterpret_cast<void*>(&HooksInternal::Reset));
        }
        LOG_INFO("dummy D3D9Ex vtable=%p hooked=%d", ex_vtable, device_ex_hook.is_hooked());
    } else {
        // Not a problem: the D3D9 dummy device above (or the captured device) already owns
        // the EndScene hook, and D3D9Ex dummies fail on setups where the ex runtime is not
        // initialised yet. Only a total absence of hooks is worth an error.
        LOG_INFO("dummy D3D9Ex device not created; EndScene comes from the D3D9 device");
    }
    if (!device_hook.is_hooked() && !device_d3d9_hook.is_hooked() && !device_ex_hook.is_hooked())
        LOG_ERROR("no EndScene hook installed; menu will not render");

    if (Interfaces::Surface) {
        cursor_hook.attach(Interfaces::Surface, 67, reinterpret_cast<void*>(&LockCursor));
        LOG_INFO("surface cursor hook=%d", cursor_hook.is_hooked());
    }

    if (Interfaces::Panel)
        panel_hook.attach(Interfaces::Panel, 41, reinterpret_cast<void*>(&HooksInternal::PaintTraverse));
    else
        LOG_WARN("Panel is null; PaintTraverse not hooked");
    if (Interfaces::Client)
        client_hook.attach(Interfaces::Client, 37, reinterpret_cast<void*>(&HooksInternal::FrameStageNotify));
    // Movement needs the command itself, so this is the one hook that runs after the
    // client applies input. Slot 24 is the index `main` uses for this vtable.
    if (Interfaces::ClientMode)
        client_mode_hook.attach(Interfaces::ClientMode, kClientModeCreateMoveSlot,
                                reinterpret_cast<void*>(&HooksInternal::CreateMove));
    else
        LOG_WARN("ClientMode is null; movement and auto buy stay off (the pattern for this client changed)");
    if (Interfaces::RenderView)
        view_render_hook.attach(Interfaces::RenderView, 9, reinterpret_cast<void*>(&HooksInternal::SceneEnd));
    if (Interfaces::ModelRender)
        model_render_hook.attach(Interfaces::ModelRender, 21, reinterpret_cast<void*>(&HooksInternal::DrawModelExecute));
    // Visuals -> World: Nightmode / Sky ride the material colour path, Fullbright the engine's
    // own mat_fullbright switch.
    HooksInternal::InstallWorldDetours();

    ShotChams::Initialize();
    LOG_INFO("hooks installed (WndProc=%d EndScene=%d/%d/%d PaintTraverse=%d FrameStageNotify=%d SceneEnd=%d DrawModelExecute=%d)",
        original_wndproc != nullptr, device_hook.is_hooked(), device_d3d9_hook.is_hooked(),
        device_ex_hook.is_hooked(), panel_hook.is_hooked(),
        client_hook.is_hooked(), view_render_hook.is_hooked(), model_render_hook.is_hooked());
    // The cheat stays loaded as long as the menu can be toggled; feature hooks
    // are optional until features land.
    return original_wndproc != nullptr &&
        (device_hook.is_hooked() || device_d3d9_hook.is_hooked() || device_ex_hook.is_hooked());
}
void Restore() {
    HooksInternal::RemoveWorldDetours();
    WorldModulation::Shutdown();
    ShotChams::Shutdown();
    WorldFog::Restore();
    VisualRuntime::ShutdownGlow();
    LocalInventory::Shutdown();
    if (game_window && original_wndproc)
        SetWindowLongPtrA(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wndproc));
    original_wndproc = nullptr; game_window = nullptr;
    model_render_hook.detach(); view_render_hook.detach();
    client_mode_hook.detach(); client_hook.detach();
    reset_ex_hook.detach(); reset_d3d9_hook.detach(); reset_hook.detach();
    device_ex_hook.detach(); device_d3d9_hook.detach(); device_hook.detach();
    cursor_hook.detach(); panel_hook.detach();
    LOG_INFO("hooks restored");
}
}
