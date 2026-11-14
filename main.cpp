// DLL entry + staged initialization. The ordering follows Havoc's Entry flow.
#include <windows.h>
#include "context.hpp"
#include "sdk/interfaces/interfaces.hpp"
#include "sdk/netvars.hpp"
#include "patterns/patterns.hpp"
#include "offsets/offsets.hpp"
#include "utils/displacement.hpp"
#include "utils/math.hpp"
#include "utils/render.hpp"
#include "utils/event_listener.hpp"
#include "utils/log.hpp"
#include "hooks/hooks.hpp"

static void Shutdown() {
    Hooks::Restore();
    EventListener::Destroy();
    PropManager::Get().Destroy();
}
static DWORD WINAPI Entry(LPVOID parameter) {
    const auto self = static_cast<HMODULE>(parameter);
    while (!GetModuleHandleA("serverbrowser.dll")) {
        if (GetAsyncKeyState(VK_END) & 1) FreeLibraryAndExitThread(self, 0);
        Sleep(200);
    }
    // Discover addresses first, then capture interfaces that depend on them.
    if (!Patterns::Init()) { LOG_ERROR("signature scan failed"); FreeLibraryAndExitThread(self, 1); }
    if (!offsets::Init()) { LOG_ERROR("offset setup failed"); FreeLibraryAndExitThread(self, 2); }
    if (!Interfaces::Setup()) { LOG_ERROR("interface setup failed"); FreeLibraryAndExitThread(self, 3); }
    if (!PropManager::Get().Create()) { LOG_ERROR("RecvTable setup failed"); FreeLibraryAndExitThread(self, 4); }
    Displacement::Init();
    if (!Math::Setup()) { FreeLibraryAndExitThread(self, 5); }

    Render::CreateFonts();
    EventListener::Setup({"bullet_impact", "round_start", "player_hurt", "weapon_fire", "player_death"});
    if (!Hooks::Setup()) { Shutdown(); FreeLibraryAndExitThread(self, 6); }
    LOG_INFO("CS:GO base initialized");

    while (!ctx.unload) Sleep(200);
    Shutdown();
    FreeLibraryAndExitThread(self, 0);
    return 0;
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE thread = CreateThread(nullptr, 0, Entry, hModule, 0, nullptr);
        if (thread) CloseHandle(thread);
        else return FALSE;
    }
    return TRUE;
}
