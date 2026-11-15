// DLL entry and staged initialization.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include "injector/status.hpp"
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

namespace {
HANDLE status_mapping{};
PastavarStatus::Shared* shared_status{};
volatile LONG local_status = PastavarStatus::Initializing;
char local_detail[512]{};
const char* current_stage = "Starting DLL";

void Publish(PastavarStatus::State state, const char* detail = "") {
    strncpy_s(local_detail, detail ? detail : "", _TRUNCATE);
    if (shared_status) {
        strncpy_s(shared_status->detail, local_detail, _TRUNCATE);
        MemoryBarrier();
        InterlockedExchange(&shared_status->state, state);
    }
    InterlockedExchange(&local_status, state);
}
void OpenStatus() {
    wchar_t name[64]{};
    PastavarStatus::MappingName(GetCurrentProcessId(), name);
    status_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(PastavarStatus::Shared), name);
    if (status_mapping) {
        shared_status = static_cast<PastavarStatus::Shared*>(MapViewOfFile(status_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PastavarStatus::Shared)));
        if (shared_status) shared_status->protocol = PastavarStatus::version;
    }
    Publish(PastavarStatus::Initializing);
}
void Shutdown() {
    Hooks::Restore();
    EventListener::Destroy();
    PropManager::Get().Destroy();
}
DWORD InitializeAndRun(HMODULE self) {
    try {
        current_stage = "Waiting for serverbrowser.dll";
        while (!GetModuleHandleA("serverbrowser.dll")) Sleep(200);
        current_stage = "Signature scan";
        if (!Patterns::Init()) { Publish(PastavarStatus::Failed, "Signature scan failed"); return 1; }
        current_stage = "Offset setup";
        if (!offsets::Init()) { Publish(PastavarStatus::Failed, "Offset setup failed"); return 2; }
        current_stage = "Interface setup";
        if (!Interfaces::Setup()) LOG_ERROR("interface setup incomplete; menu hooks will still be attempted");
        current_stage = "Netvar setup";
        if (!PropManager::Get().Create()) LOG_ERROR("RecvTable setup failed; netvars unavailable");
        current_stage = "Displacement setup";
        Displacement::Init();
        current_stage = "Math setup";
        if (!Math::Setup()) LOG_ERROR("math setup failed");
        current_stage = "Font creation";
        Render::CreateFonts();
        current_stage = "Event listener setup";
        EventListener::Setup({"bullet_impact", "round_start", "player_hurt", "weapon_fire", "player_death"});
        current_stage = "Hook setup";
        if (!Hooks::Setup()) { Shutdown(); Publish(PastavarStatus::Failed, "Hook setup failed: game window, WndProc or EndScene hook unavailable"); return 6; }
        LOG_INFO("CS:GO base initialized");
        current_stage = "Running";
        Publish(PastavarStatus::Ready);
        while (!ctx.unload) Sleep(200);
        current_stage = "Shutdown";
        Shutdown();
        Publish(PastavarStatus::Unloaded);
        if (shared_status) { UnmapViewOfFile(shared_status); shared_status = nullptr; }
        if (status_mapping) { CloseHandle(status_mapping); status_mapping = nullptr; }
        FreeLibraryAndExitThread(self, 0);
    } catch (const std::exception& e) {
        char message[512]{};
        snprintf(message, sizeof(message), "%s: %s", current_stage, e.what());
        Publish(PastavarStatus::Failed, message);
    } catch (...) {
        char message[512]{};
        snprintf(message, sizeof(message), "%s: unknown C++ exception", current_stage);
        Publish(PastavarStatus::Failed, message);
    }
    // Keep the failed module loaded so the status remains available and do not
    // attempt unsafe cleanup after an unknown partial initialization.
    return 1;
}
DWORD WINAPI Entry(LPVOID parameter) {
    OpenStatus();
    __try {
        return InitializeAndRun(static_cast<HMODULE>(parameter));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        char message[512]{};
        _snprintf_s(message, _TRUNCATE, "%s: SEH exception 0x%08lX", current_stage, GetExceptionCode());
        Publish(PastavarStatus::Crashed, message);
        return 1;
    }
}
}
#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:PastavarGetStatus=_PastavarGetStatus")
#pragma comment(linker, "/EXPORT:PastavarCopyStartupError=_PastavarCopyStartupError")
#endif
extern "C" __declspec(dllexport) LONG __cdecl PastavarGetStatus() { return InterlockedCompareExchange(&local_status, 0, 0); }
extern "C" __declspec(dllexport) DWORD __cdecl PastavarCopyStartupError(char* buffer, DWORD capacity) {
    if (!buffer || !capacity) return 0;
    strncpy_s(buffer, capacity, local_detail, _TRUNCATE);
    return static_cast<DWORD>(strlen(buffer));
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, Entry, module, 0, nullptr);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
