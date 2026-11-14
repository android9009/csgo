#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "runtime.hpp"
#include "debug_console.hpp"
#include "fonts.hpp"
#include "steam_avatar.hpp"

#include <Windows.h>
#include <d3d9.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <mutex>
#include <thread>

#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "../features/visuals/esp/esp.hpp"
#include "../features/misc/misc.hpp"
#include "../features/visuals/world/render.hpp"
#include "../menu/blur.hpp"
#include "../menu/binds.hpp"
#include "../menu/menu.hpp"
#include "../menu/weapon_icons.hpp"
#include "../sdk/config_system/config_system.hpp"
#include "../sdk/game/globals.hpp"

#pragma comment(lib, "d3d9.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg,
    WPARAM wparam, LPARAM lparam);

namespace {
using present_t = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND,
    const RGNDATA*);
using reset_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
present_t original_present = nullptr;
reset_t original_reset = nullptr;

WNDPROC original_wndproc = nullptr;
IDirect3DDevice9* device = nullptr;
bool initialized = false;
bool configuration_ready = false;
bool menu_ready = false;
HWND game_window = nullptr;
bool menu_input_open = false;
std::mutex frame_mutex;

#if defined(_DEBUG)
// The game appears to Present this device from two different threads, which
// would double-render (and double the grenade prediction) every frame. Log each
// new thread once and every 240th render per thread so a debug.log shows the
// callers and the rate. Release compiles this out entirely.
std::mutex present_diag_mutex;
DWORD present_diag_seen[8]{};
std::size_t present_diag_seen_count = 0;
void note_present_thread(HWND override_window) {
    const DWORD id = GetCurrentThreadId();
    {
        std::lock_guard<std::mutex> lock(present_diag_mutex);
        bool known = false;
        for (std::size_t i = 0; i < present_diag_seen_count; ++i) {
            if (present_diag_seen[i] == id) {
                known = true;
                break;
            }
        }
        if (!known) {
            if (present_diag_seen_count < 8)
                present_diag_seen[present_diag_seen_count++] = id;
            AIRFLOW_DEBUG_LOGF("present: new thread %lu presents the shared device (override_window=%p)", id, override_window);
        }
    }
    static thread_local unsigned long long renders = 0;
    ++renders;
    if (renders % 240 == 0)
        AIRFLOW_DEBUG_LOGF("present: thread %lu rendered %llu frames", id, renders);
}
#endif

using paint_traverse_t = void(__thiscall*)(void*, unsigned int, bool, bool);
paint_traverse_t original_paint_traverse = nullptr;

// VGUI PaintTraverse, same contract as main's vmt::paint_traverse: after the
// original has run, the FocusOverlayPanel's mouse input follows the menu state
// on every traversal. While the menu is open that panel owns the cursor (the
// game unlocks it and VGUI swallows the clicks); while it is closed the flag is
// written back to false every frame, so no stale state can survive a map load.
void __fastcall hooked_paint_traverse(void* ecx, void*, unsigned int panel, bool repaint, bool force) {
    const char* name = HACKS->panel->get_name(static_cast<int>(panel));
    // World > Removals: one filter for both renderers, exact panel names only.
    if (name && world_visuals::should_skip_panel(name))
        return;

    original_paint_traverse(ecx, panel, repaint, force);

    if (!name || std::strcmp(name, "FocusOverlayPanel") != 0)
        return;
    HACKS->panel->set_mouse_input_enabled(panel, g_menu->is_opened());
}

bool install_panel_hook() {
    if (original_paint_traverse)
        return true;
    if (!HACKS->panel)
        return false;
    void** vtable = *reinterpret_cast<void***>(HACKS->panel);
    if (!vtable || !vtable[41])
        return false;
    if (MH_CreateHook(vtable[41], reinterpret_cast<void*>(&hooked_paint_traverse),
                      reinterpret_cast<void**>(&original_paint_traverse)) != MH_OK)
        return false;
    if (MH_EnableHook(vtable[41]) != MH_OK) {
        MH_RemoveHook(vtable[41]);
        original_paint_traverse = nullptr;
        return false;
    }
    return true;
}

// Closing the menu: hand ImGui the button releases it will no longer receive,
// so a click cannot stay "held" (ImGui drops its own SetCapture on the last
// release). The engine's capture is never touched.
void release_menu_input(HWND hwnd) {
    if (!hwnd)
        hwnd = game_window;
    if (hwnd && ImGui::GetCurrentContext()) {
        static const UINT releases[] = {
            WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP
        };
        for (const UINT message : releases) {
            const WPARAM wparam = message == WM_XBUTTONUP ? (XBUTTON1 << 16) : 0;
            ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, 0);
        }
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().MouseDrawCursor = false;
    }
    menu_input_open = false;
}

void synchronize_menu_input() {
    if (g_menu->is_opened())
        menu_input_open = true;
    else if (menu_input_open)
        release_menu_input(game_window);
}

bool mouse_message(UINT message) {
    return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
}

bool keyboard_message(UINT message) {
    return message >= WM_KEYFIRST && message <= WM_KEYLAST;
}

// Window procedure, same flow as main's hooks::hooked_wnd_proc:
//   1. Insert (key up) toggles the menu;
//   2. while another window is in the foreground everything goes to the game;
//   3. while the menu is open ImGui sees the message first and it is swallowed
//      only if ImGui handled it or a text field is being edited;
//   4. everything else goes to the game untouched.
// The game's own input is kept away from the open menu by the FocusOverlayPanel
// (see hooked_paint_traverse), not by eating window messages here.
LRESULT CALLBACK hooked_wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!original_wndproc)
        return DefWindowProcA(hwnd, message, wparam, lparam);

    if (message == WM_KEYUP && wparam == VK_INSERT) {
        g_menu->toggle();
        if (g_menu->is_opened())
            menu_input_open = true;
        else
            release_menu_input(hwnd);
    }

    if (GetForegroundWindow() != hwnd)
        return CallWindowProcA(original_wndproc, hwnd, message, wparam, lparam);

    if (g_menu->is_opened() && ImGui::GetCurrentContext()) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam) ||
            ImGui::GetIO().WantTextInput)
            return TRUE;
        // Keybind capture: the key or button being bound must not reach the game.
        if (menu_binds::is_capturing() && (keyboard_message(message) || mouse_message(message)))
            return TRUE;
    }

    return CallWindowProcA(original_wndproc, hwnd, message, wparam, lparam);
}

bool initialize_renderer(IDirect3DDevice9* present_device) {
    if (!present_device)
        return false;
    AIRFLOW_DEBUG_STAGE("renderer.initialize");
#ifdef _DEBUG
    static bool initialization_logged = false;
    if (!initialization_logged) {
        AIRFLOW_DEBUG_LOG("[2/3] first frame: initializing the renderer (ImGui, fonts, window procedure, config, menu)");
        initialization_logged = true;
    }
#endif

    D3DDEVICE_CREATION_PARAMETERS parameters{};
    if (FAILED(present_device->GetCreationParameters(&parameters)) || !parameters.hFocusWindow) {
        AIRFLOW_DEBUG_LOG_ONCE("[FAIL] renderer: the D3D9 device has no focus window (GetCreationParameters)");
        return false;
    }

    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) {
        AIRFLOW_DEBUG_LOG_ONCE("[FAIL] renderer: ImGui::CreateContext failed");
        return false;
    }

    if (!ImGui_ImplWin32_Init(parameters.hFocusWindow)) {
        AIRFLOW_DEBUG_LOG_ONCE("[FAIL] renderer: ImGui Win32 backend failed to initialize");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX9_Init(present_device)) {
        AIRFLOW_DEBUG_LOG_ONCE("[FAIL] renderer: ImGui DX9 backend failed to initialize");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    if (!initialize_menu_fonts()) {
        AIRFLOW_DEBUG_LOG_ONCE("[FAIL] renderer: menu fonts failed to load");
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    // Same call main uses (hooks::init): ANSI subclass of the game window.
    WNDPROC previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(parameters.hFocusWindow,
        GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hooked_wndproc)));
    if (!previous) {
        AIRFLOW_DEBUG_LOG_ONCE("[FAIL] renderer: the game window procedure could not be replaced (SetWindowLongPtrA)");
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    original_wndproc = previous;
    game_window = parameters.hFocusWindow;
    device = present_device;
    blur::device = present_device;

    if (!configuration_ready) {
        g_config_system->setup_values();
        configuration_ready = true;
    }
    if (!menu_ready) {
        g_menu->init();
        menu_ready = true;
    }

    initialized = true;
    AIRFLOW_DEBUG_LOG(debug_session::renderer_ready);
    return true;
}

void render_frame(IDirect3DDevice9* present_device) {
    AIRFLOW_DEBUG_STAGE("renderer.render_frame");
    std::scoped_lock lock(frame_mutex);
    if (!initialized && !initialize_renderer(present_device))
        return;
    if (present_device != device)
        return;

    synchronize_menu_input();

#ifdef _DEBUG
    static bool menu_state_initialized = false;
    static bool previous_menu_open = false;
    static bool trace_menu_draw = false;
    const bool menu_open = g_menu->is_opened();
    if (!menu_state_initialized || menu_open != previous_menu_open) {
        AIRFLOW_DEBUG_LOGF("menu state: %s", menu_open ? "opened" : "closed");
        trace_menu_draw = menu_open;
        menu_state_initialized = true;
        previous_menu_open = menu_open;
    }
#endif

    AIRFLOW_DEBUG_STAGE("ImGui frame setup");
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().MouseDrawCursor = g_menu->is_opened();
    // Set the font scale before NewFrame(): ImGui snapshots FontBaseSize and the default item
    // width there. Doing it after Begin() leaves popup controls using the previous DPI metrics.
    ImGui::GetIO().FontGlobalScale = gui.m_scale;
    ImGui::NewFrame();
    AIRFLOW_DEBUG_STAGE("misc_features.initialize");
    misc_features::initialize();
    AIRFLOW_DEBUG_STAGE("player_esp.render");
    player_esp::render();
    AIRFLOW_DEBUG_STAGE("c_menu.draw");
#ifdef _DEBUG
    if (trace_menu_draw)
        AIRFLOW_DEBUG_LOG("menu draw enter");
#endif
    g_menu->draw();
#ifdef _DEBUG
    if (trace_menu_draw) {
        AIRFLOW_DEBUG_LOG("menu draw returned");
        trace_menu_draw = false;
    }
#endif
    AIRFLOW_DEBUG_STAGE("c_menu.draw returned");
    AIRFLOW_DEBUG_STAGE("ImGui::Render");
    ImGui::Render();
    AIRFLOW_DEBUG_STAGE("DX9 RenderDrawData");
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    AIRFLOW_DEBUG_STAGE("DX9 RenderDrawData returned");
}

HRESULT WINAPI hooked_present(IDirect3DDevice9* present_device, const RECT* source,
    const RECT* destination, HWND override_window, const RGNDATA* dirty_region) {
#if defined(_DEBUG)
    note_present_thread(override_window);
#endif
    render_frame(present_device);
    return original_present(present_device, source, destination, override_window, dirty_region);
}

HRESULT WINAPI hooked_reset(IDirect3DDevice9* reset_device, D3DPRESENT_PARAMETERS* parameters) {
    AIRFLOW_DEBUG_STAGE("renderer.reset");
    {
        std::scoped_lock lock(frame_mutex);
        if (initialized && reset_device == device) {
            steam_avatar::on_device_reset();
            weapon_icons::on_device_reset();
            blur::on_device_reset();
            ImGui_ImplDX9_InvalidateDeviceObjects();
        }
    }

    const HRESULT result = original_reset(reset_device, parameters);

    if (SUCCEEDED(result)) {
        std::scoped_lock lock(frame_mutex);
        if (initialized && reset_device == device)
            ImGui_ImplDX9_CreateDeviceObjects();
    }
    return result;
}

// Startup: the SDK base taken from main (sdk/game) waits for the game, then
// resolves modules, patterns, interfaces, convars and netvars. The game's own
// IDirect3DDevice9 comes from there (offsets::d3d_device) and its virtual
// functions are detoured. No second Direct3D device or helper window is created
// inside the game process.
bool install_hooks() {
    if (!HACKS->init()) {
        AIRFLOW_DEBUG_LOG("[FAIL] SDK: a required game interface was not resolved");
        return false;
    }
    IDirect3DDevice9* game_device = HACKS->d3d_device;

    void** vtable = *reinterpret_cast<void***>(game_device);
    void* reset_target = vtable[16];
    void* present_target = vtable[17];

    bool success = MH_Initialize() == MH_OK;
    if (success)
        success = MH_CreateHook(reset_target, &hooked_reset,
            reinterpret_cast<void**>(&original_reset)) == MH_OK;
    if (success)
        success = MH_CreateHook(present_target, &hooked_present,
            reinterpret_cast<void**>(&original_present)) == MH_OK;
    if (success && !install_panel_hook())
        AIRFLOW_DEBUG_LOG("[FAIL] VGUI PaintTraverse hook was not installed: the menu cannot take the cursor");
    if (success)
        success = MH_EnableHook(MH_ALL_HOOKS) == MH_OK;

    if (!success) {
        AIRFLOW_DEBUG_LOGF("[FAIL] MinHook could not create or enable the hooks (Reset=%p, Present=%p)",
            reset_target, present_target);
        MH_Uninitialize();
        original_present = nullptr;
        original_reset = nullptr;
        original_paint_traverse = nullptr;
    }
    return success;
}


DWORD WINAPI worker(LPVOID) {
    AIRFLOW_DEBUG_START();
    AIRFLOW_DEBUG_STAGE("install_hooks");
    AIRFLOW_DEBUG_LOG("[1/3] runtime worker started: waiting for the game, then hooking its Direct3D 9 device and VGUI panel");
    if (!install_hooks()) {
        AIRFLOW_DEBUG_LOG(debug_session::hooks_failed);
    } else {
        AIRFLOW_DEBUG_LOG(debug_session::hooks_installed);
        AIRFLOW_DEBUG_LOG("      the renderer starts on the next frame; game interfaces and hooks follow on the render thread");
    }
    // Source interfaces, materials and gameplay hooks are initialized lazily on
    // the game's render thread, not on this background worker.
    AIRFLOW_DEBUG_STAGE("runtime worker idle");
    return 0;
}
} // namespace

namespace app_runtime {
bool start() {
    HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (!thread)
        return false;
    CloseHandle(thread);
    return true;
}
} // namespace app_runtime
