#pragma once
#include "chams_materials.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/memory.hpp"
#include "../../../utils/offsets/offsets.hpp"
#include "../../../utils/log.hpp"
#include <cstdint>
#include <cstring>
namespace ChamsMaterialFactory {
// FromString calling convention follows Osiris csgo-end-of-life's
// Helpers/KeyValuesFactory.h (MIT, see third_party/licenses/Osiris-inventory.txt).
// Kept only as a last-resort fallback for when the legacy patterns miss on an
// unfamiliar build; the real creation path below is the legacy one from `main`.
inline void* Parse(const char* name,const char* value) {
    static const auto parser=[]()->std::uintptr_t {
        const auto call=MEM::FindPattern("client.dll","E8 ? ? ? ? 83 C4 04 89 45 D8");
        if(!call)return 0;
        return call+5+*reinterpret_cast<const std::int32_t*>(call+1);
    }();
    if(!parser)return nullptr;
    void* result=nullptr;
#if defined(_M_IX86)
    __asm {
        push 0
        mov edx,value
        mov ecx,name
        call parser
        add esp,4
        mov result,eax
    }
#endif
    return result;
}
// Legacy vstdlib KeyValues object, exactly as origin/main defines it
// (structs.hpp: `class c_key_values { PAD(40); }` -> 40 opaque bytes). The
// engine's KeyValues::Init and KeyValues::LoadFromBuffer member functions
// (addresses found by pattern) populate it, and IMaterialSystem::CreateMaterial
// (vfunc 83) consumes this legacy keyvalue -- NOT the newer IKS
// KeyValuesSystem::FromString tree, which the material factory does not bind the
// shader params for (the reason every chams mode rendered identically before).
struct c_key_values { unsigned char _pad[40]; };
// Create a runtime material from a full VMT buffer.
//
// Primary path (matches origin/main chams.cpp::create_material verbatim):
//   c_key_values* kv = new c_key_values;
//   KeyValues::Init(kv, shader, 0, 0);                  // offsets::init_key_values
//   KeyValues::LoadFromBuffer(kv, name, full_vmt, ...); // offsets::load_from_buffer
//   material = MaterialSystem->CreateMaterial(name, kv);
// Init has two build spellings (non-LEGACY E8 form primary, LEGACY `..._alt`
// fallback); LoadFromBuffer is shared by every `main` build block. If the
// legacy path is unavailable or fails, fall back to the previous IKS
// FromString path so chams keep rendering on an unfamiliar build.
inline IMaterial* CreateVMT(const char* name,const char* shader,const char* vmt) {
    if(!Interfaces::MaterialSystem)return nullptr;
    if(!shader || !vmt)return nullptr;

    auto* base=MEM::GetModuleBase("client.dll");
    const auto load_rva=offsets::Get("load_from_buffer");
    if(base && load_rva){
        const auto load_addr=reinterpret_cast<std::uintptr_t>(base)+load_rva;
        const auto init_rva=offsets::Get("init_key_values");
        const auto init_alt=offsets::Get("init_key_values_alt");
        if(init_rva){
            using InitFn=void(__thiscall*)(void*,const char*,int,int);
            using LoadFn=void(__thiscall*)(void*,const char*,const char*,void*,const char*,void*,void*);
            auto* key=new c_key_values();
            reinterpret_cast<InitFn>(reinterpret_cast<std::uintptr_t>(base)+init_rva)(key,shader,0,0);
            reinterpret_cast<LoadFn>(load_addr)(key,name,vmt,nullptr,nullptr,nullptr,nullptr);
            if(auto* material=Interfaces::MaterialSystem->CreateMaterial(name,key);
               material && !material->IsErrorMaterial()){
                material->IncrementReferenceCount();
                LOG_INFO("chams material created (legacy): %s (shader %s)",name,material->GetShaderName());
                return material;
            }
            LOG_WARN("chams legacy material creation failed: %s (shader %s)",name,shader);
        }else if(init_alt){
            using InitFn=void(__thiscall*)(void*,const char*);
            using LoadFn=void(__thiscall*)(void*,const char*,const char*,void*,const char*,void*);
            auto* key=new c_key_values();
            reinterpret_cast<InitFn>(reinterpret_cast<std::uintptr_t>(base)+init_alt)(key,shader);
            reinterpret_cast<LoadFn>(load_addr)(key,name,vmt,nullptr,nullptr,nullptr);
            if(auto* material=Interfaces::MaterialSystem->CreateMaterial(name,key);
               material && !material->IsErrorMaterial()){
                material->IncrementReferenceCount();
                LOG_INFO("chams material created (legacy): %s (shader %s)",name,material->GetShaderName());
                return material;
            }
            LOG_WARN("chams legacy material creation failed: %s (shader %s)",name,shader);
        }
    }

    // Last resort: the previous IKS KeyValuesSystem::FromString path.
    const char* body=ChamsMaterial::Parameters(vmt);
    if(!body){LOG_WARN("chams material has no parameters: %s",name);return nullptr;}
    auto* kv=Parse(shader,body);
    if(!kv){LOG_WARN("chams material parser unavailable: %s",name);return nullptr;}
    auto* material=Interfaces::MaterialSystem->CreateMaterial(name,kv);
    if(!material || material->IsErrorMaterial()){
        LOG_WARN("chams material creation failed: %s (shader %s)",name,shader);
        return nullptr;
    }
    material->IncrementReferenceCount();
    LOG_INFO("chams material created (iks fallback): %s (shader %s)",name,material->GetShaderName());
    return material;
}
}
