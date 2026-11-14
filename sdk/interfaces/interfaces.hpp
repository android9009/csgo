#pragma once
// Capture<T> preserves Havoc's lightweight interface-acquisition style; wrappers only bind used vfuncs.
#include <string_view>
#include <cstdint>
#include <windows.h>
#include "../clientclass.hpp"
#include "../../math/vector.hpp"
#include "../../math/qangle.hpp"
#include "../trace_ray.hpp"

class IClientEntityList {
public:
    void* GetClientEntity(int index) {
        using Fn = void* (__thiscall*)(void*, int);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[3])(this, index);
    }
    void* GetClientEntityFromHandle(std::uint32_t handle) {
        using Fn = void* (__thiscall*)(void*, std::uint32_t);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[4])(this, handle);
    }
    int GetHighestEntityIndex() {
        using Fn = int (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[6])(this);
    }
};
class IBaseClientDll {
public:
    ClientClass* GetAllClasses() {
        using Fn = ClientClass* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[8])(this);
    }
};
class IEngineClient {
public:
    void GetScreenSize(int& width, int& height) {
        using Fn = void (__thiscall*)(void*, int&, int&);
        reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[5])(this, width, height);
    }
    int GetLocalPlayer() {
        using Fn = int (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[12])(this);
    }
    void GetViewAngles(QAngle& angles) {
        using Fn = void (__thiscall*)(void*, QAngle&);
        reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[18])(this, angles);
    }
    void SetViewAngles(const QAngle& angles) {
        using Fn = void (__thiscall*)(void*, const QAngle&);
        reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[19])(this, angles);
    }
    int GetMaxClients() {
        using Fn = int (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[20])(this);
    }
    bool IsInGame() {
        using Fn = bool (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[26])(this);
    }
    bool IsConnected() {
        using Fn = bool (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[27])(this);
    }
};
struct IGlobalVarsBase {
    float realtime = 0.f;
    int framecount = 0;
    float absoluteframetime = 0.f;
    float absoluteframestarttimestddev = 0.f;
    float curtime = 0.f;
    float frametime = 0.f;
    int maxClients = 0;
    int tickcount = 0;
    float interval_per_tick = 0.f;
    float interpolation_amount = 0.f;
    int simTicksThisFrame = 0;
    int network_protocol = 0;
    void* saveData = nullptr;
    bool client = false;
    bool remoteClient = false;
};
class IClientState;
class IPrediction;
class IGameMovement;
class IMoveHelper;
class IInput;
class InterfaceView {
protected:
    template <typename Fn> Fn VFunc(std::size_t index) {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[index]);
    }
};
class IClientModeShared : public InterfaceView {};
class IVPanel : public InterfaceView {
public:
    const char* GetName(unsigned int panel) { using Fn = const char* (__thiscall*)(void*, unsigned int); auto fn = VFunc<Fn>(36); return fn ? fn(this, panel) : ""; }
};
class IVModelRender : public InterfaceView {};
class IVModelInfo : public InterfaceView {};
class IVDebugOverlay : public InterfaceView {};
class IEngineTrace : public InterfaceView {
public:
    void TraceRay(const Ray_t& ray, unsigned int mask, ITraceFilter* filter, CGameTrace* trace) {
        using Fn = void (__thiscall*)(void*, const Ray_t&, unsigned int, ITraceFilter*, CGameTrace*);
        if (auto fn = VFunc<Fn>(5)) fn(this, ray, mask, filter, trace);
    }
};
class IMaterialSystem : public InterfaceView {};
class ISurface : public InterfaceView {
public:
    void SetDrawColor(int r, int g, int b, int a) { using Fn = void (__thiscall*)(void*, int, int, int, int); if (auto fn = VFunc<Fn>(15)) fn(this, r, g, b, a); }
    void DrawFilledRect(int x0, int y0, int x1, int y1) { using Fn = void (__thiscall*)(void*, int, int, int, int); if (auto fn = VFunc<Fn>(16)) fn(this, x0, y0, x1, y1); }
    void DrawOutlinedRect(int x0, int y0, int x1, int y1) { using Fn = void (__thiscall*)(void*, int, int, int, int); if (auto fn = VFunc<Fn>(18)) fn(this, x0, y0, x1, y1); }
    void DrawLine(int x0, int y0, int x1, int y1) { using Fn = void (__thiscall*)(void*, int, int, int, int); if (auto fn = VFunc<Fn>(19)) fn(this, x0, y0, x1, y1); }
};
class IConVar;
class IGameEventManager2;
class IPhysicsSurfaceProps;
class IViewRender;
class IViewRenderBeams;
class IWeaponSystem;
class IGlowObjectManager;
class IStudioRender;
class IMDLCache;
class ILocalize;
class IGameConsole;
class IEngineSound;
class IVEffects;
class IEngineVGui;
class IRenderView;

namespace Interfaces {
    void* CaptureRaw(std::string_view module, std::string_view name);
    template <typename T> T* Capture(std::string_view module, std::string_view name) {
        return static_cast<T*>(CaptureRaw(module, name));
    }
    bool Setup();
    bool Check();

    inline IBaseClientDll* Client = nullptr;
    inline IClientEntityList* EntityList = nullptr;
    inline IEngineClient* Engine = nullptr;
    inline IClientState* ClientState = nullptr;
    inline IGlobalVarsBase* Globals = nullptr;
    inline IPrediction* Prediction = nullptr;
    inline IGameMovement* GameMovement = nullptr;
    inline IMoveHelper* MoveHelper = nullptr;
    inline IInput* Input = nullptr;
    inline IClientModeShared* ClientMode = nullptr;
    inline IVPanel* Panel = nullptr;
    inline IVModelRender* ModelRender = nullptr;
    inline IVModelInfo* ModelInfo = nullptr;
    inline IVDebugOverlay* DebugOverlay = nullptr;
    inline IEngineTrace* EngineTrace = nullptr;
    inline IMaterialSystem* MaterialSystem = nullptr;
    inline ISurface* Surface = nullptr;
    inline IConVar* ConVar = nullptr;
    inline IGameEventManager2* GameEvent = nullptr;
    inline IPhysicsSurfaceProps* PhysicsProps = nullptr;
    inline IViewRender* ViewRender = nullptr;
    inline IViewRenderBeams* ViewRenderBeams = nullptr;
    inline IWeaponSystem* WeaponSystem = nullptr;
    inline IGlowObjectManager* GlowManager = nullptr;
    inline IStudioRender* StudioRender = nullptr;
    inline IMDLCache* MDLCache = nullptr;
    inline ILocalize* Localize = nullptr;
    inline IGameConsole* GameConsole = nullptr;
    inline IEngineSound* EngineSound = nullptr;
    inline IVEffects* Effects = nullptr;
    inline IEngineVGui* EngineVGui = nullptr;
    inline IRenderView* RenderView = nullptr;
    inline void* DirectDevice = nullptr;
    inline void* MemAlloc = nullptr;
    inline void* KeyValuesSystem = nullptr;
    inline void* FileSystem = nullptr;
    inline void* NetworkContainer = nullptr;
}
