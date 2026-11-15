// DLL entry and staged initialization.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include "injector/status.hpp"
#include "context.hpp"
#include "sdk/interfaces/interfaces.hpp"
#include "sdk/model_preview.hpp"
#include "sdk/netvars.hpp"
#include "patterns/patterns.hpp"
#include "offsets/offsets.hpp"
#include "utils/displacement.hpp"
#include "utils/math.hpp"
#include "utils/render.hpp"
#include "utils/event_listener.hpp"
#include "utils/log.hpp"
#include "utils/memory.hpp"
#include "hooks/hooks.hpp"
#include "features/visuals/model_preview.hpp"

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
// Read-only compatibility probe for the unfinished Source 1 model preview.
// A signature match is diagnostic only, never permission to call the address.
void ProbeModelPreview() {
    // Availability only: capturing an interface does not validate its ABI.
    LOG_INFO("preview probe: interfaces MaterialSystem=%s RenderView=%s ModelRender=%s ModelInfo=%s StudioRender=%s MDLCache=%s MemAlloc=%s",
        Interfaces::MaterialSystem ? "present" : "missing", Interfaces::RenderView ? "present" : "missing",
        Interfaces::ModelRender ? "present" : "missing", Interfaces::ModelInfo ? "present" : "missing",
        Interfaces::StudioRender ? "present" : "missing", Interfaces::MDLCache ? "present" : "missing",
        Interfaces::MemAlloc ? "present" : "missing");
    const auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    std::uintptr_t start = 0, size = 0;
    if (!base || !MEM::GetSection(base, ".text", &start, &size) || !size) {
        LOG_WARN("preview probe: client.dll .text unavailable; renderer remains disabled");
        return;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    LOG_INFO("preview probe: client.dll timestamp=%08lX image_size=%08lX", nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    struct Probe { const char* name; const char* signature; };
    const Probe probes[] = {
        {"merged model constructor candidate", "53 8B D9 56 57 8D 4B 04 C7 03 ? ? ? ? E8 ? ? ? ? 6A"},
        {"attachment bone setup candidate", "55 8B EC 83 EC 14 83 3D ? ? ? ? ? 53"}
    };
    std::uintptr_t unique_matches[2]{};
    for (std::size_t index = 0; index < 2; ++index) {
        const auto& probe = probes[index];
        const auto first = MEM::FindPattern(reinterpret_cast<const std::uint8_t*>(start), size, probe.signature);
        if (!first) {
            LOG_WARN("preview probe: %s not found", probe.name);
            continue;
        }
        const auto remaining = start + size - (first + 1);
        const auto second = MEM::FindPattern(reinterpret_cast<const std::uint8_t*>(first + 1), remaining, probe.signature);
        LOG_INFO("preview probe: %s RVA=%08lX match=%s", probe.name,
                 static_cast<unsigned long>(first - base), second ? "multiple" : "unique");
        if (!second) unique_matches[index] = first - base;
    }
    // Both independently located functions must agree with the offline snapshot's
    // relative spacing. This yields a diagnostic candidate, not a callable address.
    if (unique_matches[0] && unique_matches[1]) {
        const auto constructor_shift = static_cast<std::intptr_t>(unique_matches[0]) -
            static_cast<std::intptr_t>(SourceModelPreview::reference_model_constructor_rva);
        const auto bones_shift = static_cast<std::intptr_t>(unique_matches[1]) -
            static_cast<std::intptr_t>(SourceModelPreview::reference_attachment_bones_rva);
        const auto candidate = static_cast<std::intptr_t>(SourceModelPreview::reference_set_merged_model_rva) + constructor_shift;
        if (size >= 16 && constructor_shift == bones_shift && candidate >= 0 && static_cast<std::uintptr_t>(candidate) >= start - base &&
            static_cast<std::uintptr_t>(candidate) <= start - base + size - 16) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(base + static_cast<std::uintptr_t>(candidate));
            SourceModelPreview::model_constructor.store(base + unique_matches[0], std::memory_order_relaxed);
            SourceModelPreview::attachment_bones.store(base + unique_matches[1], std::memory_order_relaxed);
            SourceModelPreview::set_merged_model.store(base + static_cast<std::uintptr_t>(candidate), std::memory_order_relaxed);
            LOG_INFO("preview probe: fatality offline set-merged candidate RVA=%08lX bytes=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X (not validated)",
                static_cast<unsigned long>(candidate), bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
        } else {
            LOG_WARN("preview probe: fatality offline snapshot does not match located functions");
        }
    }
    LOG_INFO("preview probe: candidate addresses prepared for experimental renderer");
}
void Shutdown() {
    Hooks::Restore();
    ModelPreview::Shutdown();
    EventListener::Destroy();
    PropManager::Get().Destroy();
}
DWORD InitializeAndRun(HMODULE self) {
    try {
        std::string warnings;
        current_stage = "Waiting for serverbrowser.dll";
        while (!GetModuleHandleA("serverbrowser.dll")) Sleep(200);
        current_stage = "Signature scan";
        if (!Patterns::Init()) { Publish(PastavarStatus::Failed, "Signature scan failed"); return 1; }
        current_stage = "Offset setup";
        if (!offsets::Init()) { Publish(PastavarStatus::Failed, "Offset setup failed"); return 2; }
        current_stage = "Interface setup";
        if (!Interfaces::Setup()) { LOG_ERROR("interface setup incomplete; menu hooks will still be attempted"); warnings += "Interface setup incomplete; "; }
        current_stage = "Preview compatibility probe";
        ProbeModelPreview();
        current_stage = "Netvar setup";
        if (!PropManager::Get().Create()) { LOG_ERROR("RecvTable setup failed; netvars unavailable"); warnings += "Netvar setup failed; "; }
        current_stage = "Displacement setup";
        Displacement::Init();
        current_stage = "Math setup";
        if (!Math::Setup()) { LOG_ERROR("math setup failed"); warnings += "Math setup failed; "; }
        current_stage = "Font creation";
        Render::CreateFonts();
        current_stage = "Event listener setup";
        EventListener::Setup({"bullet_impact", "round_start", "player_hurt", "weapon_fire", "player_death"});
        current_stage = "Hook setup";
        if (!Hooks::Setup()) { Shutdown(); Publish(PastavarStatus::Failed, "Hook setup failed: game window, WndProc or EndScene hook unavailable"); return 6; }
        LOG_INFO("CS:GO base initialized");
        current_stage = "Running";
        Publish(PastavarStatus::Ready, warnings.empty() ? "" : warnings.c_str());
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
