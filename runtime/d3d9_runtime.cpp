#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "runtime.hpp"
#include "debug_console.hpp"
#include "fonts.hpp"
#include "steam_avatar.hpp"

#include <Windows.h>
#include <d3d9.h>

#include <mutex>

#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "../menu/agent_preview.hpp"
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
std::mutex frame_mutex;

LRESULT CALLBACK hooked_wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!original_wndproc)
        return DefWindowProc(hwnd, message, wparam, lparam);

    if (ImGui::GetCurrentContext())
        ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);

    if (message == WM_KEYUP && wparam == VK_INSERT &&
        GetForegroundWindow() == hwnd) {
        g_menu->toggle();
        return 0;
    }
    if ((message == WM_KEYUP || message == WM_KEYDOWN) && wparam == VK_INSERT)
        return 0;

    if (menu_binds::is_capturing() &&
        ((message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
         (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)))
        return TRUE;

    if (g_menu->is_opened()) {
        if (message == WM_INPUT)
            return 0;
        if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
            return 0;
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput &&
            message >= WM_KEYFIRST && message <= WM_KEYLAST)
            return 0;
    }

    return CallWindowProc(original_wndproc, hwnd, message, wparam, lparam);
}

bool initialize_renderer(IDirect3DDevice9* present_device) {
    if (!present_device)
        return false;

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
    return true;
}

void render_frame(IDirect3DDevice9* present_device) {
    std::scoped_lock lock(frame_mutex);
    if (!initialized && !initialize_renderer(present_device))
        return;
    if (present_device != device)
        return;

    // Panorama has already rendered into the backbuffer at this point. Copy the live agent region
    // before this frame's opaque ImGui menu covers it again.
    AIRFLOW_DEBUG_STAGE("agent_preview.begin_frame");
    agent_preview::begin_frame(present_device);
    AIRFLOW_DEBUG_STAGE("ImGui frame setup");
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().MouseDrawCursor = g_menu->is_opened();
    // Set the font scale before NewFrame(): ImGui snapshots FontBaseSize and the default item
    // width there. Doing it after Begin() leaves popup controls using the previous DPI metrics.
    ImGui::GetIO().FontGlobalScale = gui.m_scale;
    ImGui::NewFrame();
    AIRFLOW_DEBUG_STAGE("c_menu.draw");
    g_menu->draw();
    AIRFLOW_DEBUG_STAGE("agent_preview.end_frame");
    agent_preview::end_frame();
    AIRFLOW_DEBUG_STAGE("ImGui render");
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

HRESULT WINAPI hooked_present(IDirect3DDevice9* present_device, const RECT* source,
    const RECT* destination, HWND override_window, const RGNDATA* dirty_region) {
    render_frame(present_device);
    return original_present(present_device, source, destination, override_window, dirty_region);
}

HRESULT WINAPI hooked_reset(IDirect3DDevice9* reset_device, D3DPRESENT_PARAMETERS* parameters) {
    {
        std::scoped_lock lock(frame_mutex);
        if (initialized && reset_device == device) {
            agent_preview::on_device_reset();
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
