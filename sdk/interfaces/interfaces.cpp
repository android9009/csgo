#include "interfaces.hpp"
#include "../skin_icon_file.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include "../../utils/offsets/offsets.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <cmath>

namespace Interfaces {
using CreateInterfaceFn = void* (__cdecl*)(const char*, int*);
void* CaptureRaw(std::string_view module, std::string_view prefix) {
    void* base = MEM::GetModuleBase(module);
    auto create = reinterpret_cast<CreateInterfaceFn>(MEM::GetExport(base, "CreateInterface"));
    if (!create) return nullptr;
    const std::string name(prefix);
    // Exact names (e.g. GAMEEVENTSMANAGER002) are tried first, then version suffixes.
    int status = 0;
    if (void* exact = create(name.c_str(), &status)) return exact;
    for (int version = 99; version >= 1; --version) {
        const std::string candidate = name + (version < 10 ? "00" : version < 100 ? "0" : "") + std::to_string(version);
        if (void* result = create(candidate.c_str(), &status)) return result;
    }
    return nullptr;
}
static std::uintptr_t ReadAbsoluteOperand(const char* module, const char* signature, int operand_offset) {
    auto at = MEM::FindPattern(module, signature);
    if (!at) return 0;
    std::uint32_t address = 0;
    std::memcpy(&address, reinterpret_cast<const void*>(at + operand_offset), sizeof(address));
    return address;
}
static void* ReadModulePointer(std::string_view module, std::uintptr_t rva) {
    auto base = reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase(module));
    if (!base || !rva) return nullptr;
    std::uintptr_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(base + rva), sizeof(std::uint32_t));
    return reinterpret_cast<void*>(value);
}
static bool ValidGlobals(IGlobalVarsBase* candidate) {
    if(!candidate || (reinterpret_cast<std::uintptr_t>(candidate)&3u))return false;
    __try {
        return std::isfinite(candidate->realtime) && candidate->realtime>=0.f &&
            std::isfinite(candidate->curtime) && std::isfinite(candidate->frametime) &&
            candidate->frametime>=0.f && candidate->frametime<10.f &&
            std::isfinite(candidate->interval_per_tick) && candidate->interval_per_tick>=.001f &&
            candidate->interval_per_tick<=.1f && candidate->maxClients>=0 && candidate->maxClients<=128;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool Setup() {
    Client = Capture<IBaseClientDll>("client.dll", "VClient");
    EntityList = Capture<IClientEntityList>("client.dll", "VClientEntityList");
    Engine = Capture<IEngineClient>("engine.dll", "VEngineClient");
    Prediction = Capture<IPrediction>("client.dll", "VClientPrediction");
    GameMovement = Capture<IGameMovement>("client.dll", "GameMovement");
    EngineVGui = Capture<IEngineVGui>("engine.dll", "VEngineVGui");
    EngineTrace = Capture<IEngineTrace>("engine.dll", "EngineTraceClient");
    EngineSound = Capture<IEngineSound>("engine.dll", "IEngineSoundClient");
    GameEvent = Capture<IGameEventManager2>("engine.dll", "GAMEEVENTSMANAGER002");
    RenderView = Capture<IRenderView>("engine.dll", "VEngineRenderView");
    DebugOverlay = Capture<IVDebugOverlay>("engine.dll", "VDebugOverlay");
    Effects = Capture<IVEffects>("engine.dll", "VEngineEffects");
    ModelInfo = Capture<IVModelInfo>("engine.dll", "VModelInfoClient");
    ModelRender = Capture<IVModelRender>("engine.dll", "VEngineModel");
    ViewRender = Capture<IViewRender>("client.dll", "VViewRender");
    WeaponSystem = Capture<IWeaponSystem>("client.dll", "VWeaponSystem");
    MDLCache = Capture<IMDLCache>("datacache.dll", "MDLCache");
    StudioRender = Capture<IStudioRender>("studiorender.dll", "VStudioRender");
    ConVar = Capture<IConVar>("vstdlib.dll", "VEngineCvar");
    PhysicsProps = Capture<IPhysicsSurfaceProps>("vphysics.dll", "VPhysicsSurfaceProps");
    MaterialSystem = Capture<IMaterialSystem>("materialsystem.dll", "VMaterialSystem");
    Surface = Capture<ISurface>("vguimatsurface.dll", "VGUI_Surface");
    Panel = Capture<IVPanel>("vgui2.dll", "VGUI_Panel");
    Localize = Capture<ILocalize>("localize.dll", "Localize_");
    FileSystem = Capture<void>("filesystem_stdio.dll", "VFileSystem");
    BaseFileSystem = Capture<void>("filesystem_stdio.dll", "VBaseFileSystem");
    NetworkContainer = Capture<void>("engine.dll", "VEngineClientStringTable");

    ClientState = static_cast<IClientState*>(ReadModulePointer("engine.dll", offsets::Get("dwClientState")));
    // The PUSH-immediate signature resolves the globals object itself.
    // Dereferencing its first DWORD interprets realtime float bits as a pointer.
    const auto globals_base=reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("engine.dll"));
    const auto globals_rva=offsets::Get("dwGlobalVars");
    auto* direct_globals=globals_base && globals_rva ? reinterpret_cast<IGlobalVarsBase*>(globals_base+globals_rva) : nullptr;
    Globals=ValidGlobals(direct_globals) ? direct_globals : nullptr;
    if(Globals)LOG_INFO("globals: validated direct object=%p interval=%.6f max_clients=%d",Globals,Globals->interval_per_tick,Globals->maxClients);
    else LOG_WARN("globals: invalid direct candidate=%p; dependent game features will not read it",direct_globals);
    Input = static_cast<IInput*>(ReadModulePointer("client.dll", offsets::Get("dwInput")));
    // dwGlowObjectManager is the address of the CUtlVector-backed manager,
    // not a pointer slot. Dereferencing it here would yield the entry array.
    const auto glow_rva = offsets::Get("dwGlowObjectManager");
    GlowManager = glow_rva ? reinterpret_cast<IGlowObjectManager*>(reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll")) + glow_rva) : nullptr;
    DirectDevice = ReadModulePointer("shaderapidx9.dll", offsets::Get("dwppDirect3DDevice9"));

    const auto client_mode_operand = ReadAbsoluteOperand("client.dll", "A1 ? ? ? ? 8B 80 ? ? ? ? 5D C3", 1);
    if (client_mode_operand) {
        std::uint32_t value = 0;
        std::memcpy(&value, reinterpret_cast<const void*>(client_mode_operand), sizeof(value));
        ClientMode = reinterpret_cast<IClientModeShared*>(value);
    }
    const auto move_helper_operand = ReadAbsoluteOperand("client.dll", "8B 0D ? ? ? ? 8B 46 08 68", 2);
    if (move_helper_operand) {
        std::uint32_t value = 0;
        std::memcpy(&value, reinterpret_cast<const void*>(move_helper_operand), sizeof(value));
        MoveHelper = reinterpret_cast<IMoveHelper*>(value);
    }

    if (auto export_fn = MEM::GetExport(MEM::GetModuleBase("vstdlib.dll"), "KeyValuesSystem"))
        KeyValuesSystem = reinterpret_cast<void* (__cdecl*)()>(export_fn)();
    if (auto alloc = MEM::GetExport(MEM::GetModuleBase("tier0.dll"), "g_pMemAlloc"))
        MemAlloc = *reinterpret_cast<void**>(alloc);

    return Check();
}
bool Check() {
    const bool ok = Client && EntityList && Engine && ClientState && Globals && Input
                 && ClientMode && Panel && ModelRender && ConVar && MaterialSystem && Surface;
    if (!ok) LOG_ERROR("Interface capture failed; client=%p entitylist=%p engine=%p clientstate=%p globals=%p clientmode=%p",
                       Client, EntityList, Engine, ClientState, Globals, ClientMode);
    return ok;
}
}
