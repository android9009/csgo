#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "runtime.hpp"
#include "debug_console.hpp"
#include "fonts.hpp"
#include "steam_avatar.hpp"

#include <Windows.h>
#include <d3d9.h>

#include <cstring>
#include <cstddef>
#include <mutex>

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
void* game_panel = nullptr;
ULONGLONG next_panel_probe = 0;
unsigned int focus_overlay_panel = 0;
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
using panel_set_mouse_input_t = void(__thiscall*)(void*, unsigned int, bool);
using panel_get_name_t = const char*(__thiscall*)(void*, unsigned int);
using create_interface_t = void* (__cdecl*)(const char*, int*);
paint_traverse_t original_paint_traverse = nullptr;

void* query_interface(HMODULE module, const char* name) {
    if (!module)
        return nullptr;
    const auto factory = reinterpret_cast<create_interface_t>(
        GetProcAddress(module, "CreateInterface"));
    return factory ? factory(name, nullptr) : nullptr;
}

void __fastcall hooked_paint_traverse(void* ecx, void*, unsigned int panel, bool repaint, bool force) {
    const auto get_name = (*reinterpret_cast<panel_get_name_t**>(ecx))[36];
    const char* name = get_name ? get_name(ecx, panel) : nullptr;
    // World > Removals: one filter for both renderers, exact panel names only.
    if (name && world_visuals::should_skip_panel(name)) {
        // Log each skipped panel once so a hidden HUD element (the old broad
        // "Panorama" match could hide the team select) is visible in the log.
        static char last_skipped[128] = {};
        if (std::strcmp(last_skipped, name) != 0) {
            std::strncpy(last_skipped, name, sizeof(last_skipped) - 1);
            last_skipped[sizeof(last_skipped) - 1] = '\0';
            AIRFLOW_DEBUG_LOGF("panel skipped: %s", name);
        }
        return;
    }

    original_paint_traverse(ecx, panel, repaint, force);

    // This is the cursor path used by main: enabling mouse input on the
    // FocusOverlayPanel releases the game's centred/locked cursor. Do not
    // replace it with per-frame ISurface::UnlockCursor calls.
    if (!focus_overlay_panel) {
        if (!name || std::strcmp(name, "FocusOverlayPanel") != 0)
            return;
        focus_overlay_panel = panel;
    }
    if (panel != focus_overlay_panel)
        return;

    const auto set_mouse_input = (*reinterpret_cast<panel_set_mouse_input_t**>(ecx))[32];
    if (!set_mouse_input)
        return;
    // Own the cursor only while the menu is open; on close restore it exactly
    // once. Forcing "false" every frame kept mouse input disabled for the game
    // itself, so the team select screen (and buy menu) could not be clicked.
    static bool restored_closed = false;
    if (g_menu->is_opened()) {
        set_mouse_input(ecx, panel, true);
        restored_closed = false;
    } else if (!restored_closed) {
        set_mouse_input(ecx, panel, false);
        restored_closed = true;
    }
}

bool ensure_panel_hook() {
    if (original_paint_traverse)
        return true;
    const ULONGLONG now = GetTickCount64();
    if (now < next_panel_probe)
        return false;
    next_panel_probe = now + 1000;

    if (!game_panel)
        game_panel = query_interface(GetModuleHandleW(L"vgui2.dll"), "VGUI_Panel009");
    if (!game_panel)
        return false;

    void** vtable = *reinterpret_cast<void***>(game_panel);
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

void release_menu_input(HWND hwnd) {
    if (!hwnd)
        hwnd = game_window;
    if (hwnd && ImGui::GetCurrentContext()) {
        // The Win32 backend keeps its own button mask in addition to ImGuiIO.
        // Feed all releases before clearing IO so a later click cannot inherit
        // a stuck capture or a held button from the closed menu.
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
    if (hwnd && GetCapture() == hwnd)
        ReleaseCapture();
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

bool movement_key(WPARAM key) {
    switch (key) {
    case 'W': case 'A': case 'S': case 'D':
    case 'w': case 'a': case 's': case 'd':
    case VK_SPACE: case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK hooked_wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!original_wndproc)
        return DefWindowProc(hwnd, message, wparam, lparam);

    // Match the main branch: another application's messages must not be
    // consumed by our menu or leave its Win32 capture behind.
    if (GetForegroundWindow() != hwnd) {
        if (GetCapture() == hwnd)
            ReleaseCapture();
        return CallWindowProc(original_wndproc, hwnd, message, wparam, lparam);
    }

    if (message == WM_KEYUP && wparam == VK_INSERT) {
        g_menu->toggle();
        if (g_menu->is_opened())
            menu_input_open = true;
        else
            release_menu_input(hwnd);
        return 0;
    }
    if ((message == WM_KEYUP || message == WM_KEYDOWN) && wparam == VK_INSERT)
        return 0;

    const bool opened = g_menu->is_opened();
    if (opened && ImGui::GetCurrentContext()) {
        // Feed each event to ImGui exactly once. Unlike the old selective
        // route, the same click is never also delivered to the game.
        const LRESULT imgui_result = ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
        if (menu_binds::is_capturing() &&
            (keyboard_message(message) || mouse_message(message)))
            return TRUE;

        // The open menu is a modal overlay. Raw mouse input and client mouse
        // messages stay out of the game until the menu is closed.
        if (message == WM_INPUT || mouse_message(message))
            return 0;

        // Keep movement keys available to the live player, as in the existing
        // menu, while text editing owns the remaining keyboard input.
        if (ImGui::GetIO().WantTextInput && keyboard_message(message) && !movement_key(wparam))
            return 0;
        if (imgui_result)
            return imgui_result;
    } else if (opened && (message == WM_INPUT || mouse_message(message))) {
        return 0;
    }

    return CallWindowProc(original_wndproc, hwnd, message, wparam, lparam);
}

bool initialize_renderer(IDirect3DDevice9* present_device) {
    if (!present_device)
        return false;
    AIRFLOW_DEBUG_STAGE("renderer.initialize");
#ifdef _DEBUG
    static bool initialization_logged = false;
    if (!initialization_logged) {
        AIRFLOW_DEBUG_LOG("renderer initialization enter");
        initialization_logged = true;
    }
#endif

    D3DDEVICE_CREATION_PARAMETERS parameters{};
    if (FAILED(present_device->GetCreationParameters(&parameters)) || !parameters.hFocusWindow)
        return false;

    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext())
        return false;

    if (!ImGui_ImplWin32_Init(parameters.hFocusWindow)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX9_Init(present_device)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    if (!initialize_menu_fonts()) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    WNDPROC previous = reinterpret_cast<WNDPROC>(SetWindowLongPtr(parameters.hFocusWindow,
        GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hooked_wndproc)));
    if (!previous) {
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
    AIRFLOW_DEBUG_LOG("renderer initialized");
    return true;
}

void render_frame(IDirect3DDevice9* present_device) {
    AIRFLOW_DEBUG_STAGE("renderer.render_frame");
    std::scoped_lock lock(frame_mutex);
    if (!initialized && !initialize_renderer(present_device))
        return;
    if (present_device != device)
        return;

    ensure_panel_hook();
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

LRESULT CALLBACK dummy_wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProc(hwnd, message, wparam, lparam);
}

bool install_hooks() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = dummy_wndproc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"airflow_shell_d3d9_probe";
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    HWND probe_window = CreateWindowExW(0, window_class.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 1, 1, nullptr, nullptr, window_class.hInstance, nullptr);
    if (!probe_window)
        return false;

    IDirect3D9* direct3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!direct3d) {
        DestroyWindow(probe_window);
        return false;
    }

    D3DPRESENT_PARAMETERS present_parameters{};
    present_parameters.BackBufferCount = 1;
    present_parameters.BackBufferWidth = 1;
    present_parameters.BackBufferHeight = 1;
    present_parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present_parameters.hDeviceWindow = probe_window;
    present_parameters.Windowed = TRUE;
    present_parameters.EnableAutoDepthStencil = TRUE;
    present_parameters.AutoDepthStencilFormat = D3DFMT_D16;
    present_parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* probe_device = nullptr;
    HRESULT result = direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, probe_window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present_parameters, &probe_device);
    if (FAILED(result)) {
        result = direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, probe_window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present_parameters, &probe_device);
    }
    if (FAILED(result) || !probe_device) {
        direct3d->Release();
        DestroyWindow(probe_window);
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(probe_device);
    void* reset_target = vtable[16];
    void* present_target = vtable[17];

    bool success = MH_Initialize() == MH_OK;
    if (success)
        success = MH_CreateHook(reset_target, &hooked_reset,
            reinterpret_cast<void**>(&original_reset)) == MH_OK;
    if (success)
        success = MH_CreateHook(present_target, &hooked_present,
            reinterpret_cast<void**>(&original_present)) == MH_OK;
    if (success)
        success = MH_EnableHook(MH_ALL_HOOKS) == MH_OK;

    if (!success) {
        if (original_present)
            MH_RemoveHook(present_target);
        if (original_reset)
            MH_RemoveHook(reset_target);
        MH_Uninitialize();
        original_present = nullptr;
        original_reset = nullptr;
    }

    probe_device->Release();
    direct3d->Release();
    DestroyWindow(probe_window);
    UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    return success;
}

DWORD WINAPI worker(LPVOID) {
    AIRFLOW_DEBUG_START();
    AIRFLOW_DEBUG_STAGE("install_hooks");
    AIRFLOW_DEBUG_LOG("runtime worker started");
    if (!install_hooks()) {
        AIRFLOW_DEBUG_LOG("D3D9 hook initialization failed");
    } else {
        AIRFLOW_DEBUG_LOG("D3D9 hooks installed");
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
