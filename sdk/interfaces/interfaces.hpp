#pragma once
// Compat adapter: the ported FATALITY menu tree reaches game interfaces
// through the old project's `Interfaces` namespace (raw-vtable lens classes).
// Airflow keeps the same interface objects inside c_hacks (c_engine,
// c_entity_list, ... are lens classes over the exact same vtables), so the
// namespace below is populated from HACKS. Members the menu only treats as
// raw pointers (MaterialSystem, StudioRender, ...) stay plain void*.
#include <cstdint>
#include <cstdio>
#include <windows.h>
// hacks.hpp (pulled below) references IDirect3DDevice9; include d3d9.h here
// so every consumer of this adapter gets it, not just the menu TUs.
#include <d3d9.h>
#include "../../hacks.hpp"
#include "../clientclass.hpp"
#include "../studiohdr.hpp"
#include "../material.hpp"

class IEngineClient {
public:
    void GetScreenSize(int& width, int& height) {
        using Fn = void (__thiscall*)(void*, int&, int&);
        reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[5])(this, width, height);
    }
    int GetPlayerForUserID(int user_id) {
        using Fn = int (__thiscall*)(void*, int);
        auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[9]);
        return fn ? fn(this, user_id) : 0;
    }
    int GetLocalPlayer() {
        using Fn = int (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[12])(this);
    }
    void ExecuteClientCmd(const char* command, const char* flag = nullptr) {
        using Fn = void (__thiscall*)(void*, const char*, const char*);
        if (!command) return;
        auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[114]);
        if (fn) fn(this, command, flag);
    }
    bool IsInGame() {
        using Fn = bool (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[26])(this);
    }
    bool IsHltv() {
        using Fn = bool (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[93])(this);
    }
};
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
    // VClient::GetAllClasses - vfunc 8 (Source 1 x86), same slot the Fatality
    // IBaseClientDll lens used.
    ClientClass* GetAllClasses() {
        using Fn = ClientClass* (__thiscall*)(void*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[8])(this);
    }
};
class IVModelInfo {
public:
    void* GetModel(int index) {
        using Fn = void* (__thiscall*)(void*, int);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[1])(this, index);
    }
    int GetModelIndex(const char* name) {
        using Fn = int (__thiscall*)(void*, const char*);
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[2])(this, name);
    }
    studiohdr_t* GetStudioModel(const void* model) {
        using Fn = studiohdr_t* (__thiscall*)(void*, const void*);
        auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(this))[32]);
        return fn ? fn(this, model) : nullptr;
    }
};

namespace Interfaces {
inline IEngineClient* Engine = nullptr;
inline IClientEntityList* EntityList = nullptr;
inline IVModelInfo* ModelInfo = nullptr;
inline void* BaseFileSystem = nullptr;
inline void* MDLCache = nullptr;
inline void* MaterialSystem = nullptr;
inline void* MemAlloc = nullptr;
inline void* ModelRender = nullptr;
inline void* RenderView = nullptr;
inline void* StudioRender = nullptr;

// Same CreateInterface resolution the old project used: each module exports
// CreateInterface and answers its own interface name.
inline void* CaptureRaw(const char* module, const char* name) {
    using Creator = void* (__cdecl*)(const char*, int*);
    auto dll = static_cast<HMODULE>(GetModuleHandleA(module));
    if (!dll) return nullptr;
    auto creator = reinterpret_cast<Creator>(GetProcAddress(dll, "CreateInterface"));
    if (!creator) return nullptr;
    int version = 0;
    return creator(name, &version);
}

// IMaterialSystem::CreateMaterial(name, key_values) - vfunc 83 on the Source 1
// x86 vtable (same slot the Fatality IKS lens verified: SEH + self-call
// through the singleton). key_values is the legacy 40-byte c_key_values.
// IVModelRender::ForcedMaterialOverride - vfunc 1 (Source 1 x86), same slot
// the Fatality IVModelRender lens used.
inline void ForcedMaterialOverride(IMaterial* material, int override_type = 0, int overrides = 0) {
    if (!ModelRender) return;
    using Fn = void (__thiscall*)(void*, IMaterial*, int, int);
    auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(ModelRender))[1]);
    if (fn) fn(ModelRender, material, override_type, overrides);
}

inline IMaterial* CreateMaterial(const char* name, void* key_values) {
    if (!MaterialSystem || !name) return nullptr;
    using Fn = IMaterial* (__thiscall*)(void*, const char*, void*);
    auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(MaterialSystem))[83]);
    return fn ? fn(MaterialSystem, name, key_values) : nullptr;
}

inline IBaseClientDll* Client = nullptr;
inline bool Check() {
    return Engine && EntityList && ModelInfo && ModelRender && MaterialSystem
           && RenderView && StudioRender;
}

// Idempotent and cheap: called once per frame by the menu draw entry point.
// Interfaces HACKS does not expose yet stay null until the game loads them.
inline bool Setup() {
    if (HACKS) {
        if (!Engine) Engine = reinterpret_cast<IEngineClient*>(HACKS->engine);
        if (!EntityList) EntityList = reinterpret_cast<IClientEntityList*>(HACKS->entity_list);
        if (!ModelInfo) ModelInfo = reinterpret_cast<IVModelInfo*>(HACKS->model_info);
        if (!ModelRender) ModelRender = HACKS->model_render;
        if (!MaterialSystem) MaterialSystem = HACKS->material_system;
        if (!RenderView) RenderView = HACKS->render_view;
        if (!StudioRender) StudioRender = HACKS->studio_render;
        if (!MDLCache) MDLCache = HACKS->model_cache;
    }
    if (!Client)
        Client = reinterpret_cast<IBaseClientDll*>(CaptureRaw("client.dll", "VClient"));
    if (!BaseFileSystem)
        BaseFileSystem = CaptureRaw("filesystem_stdio.dll", "VBaseFileSystem");
    if (!MemAlloc) {
        auto tier0 = GetModuleHandleA("tier0.dll");
        if (tier0) {
            auto alloc_export = reinterpret_cast<void**>(GetProcAddress(tier0, "g_pMemAlloc"));
            if (alloc_export) MemAlloc = *alloc_export;
        }
    }
    return Check();
}
}
