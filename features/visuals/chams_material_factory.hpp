#pragma once
#include "chams_materials.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include <cstdint>
#include <cstring>
namespace ChamsMaterialFactory {
// FromString calling convention follows Osiris csgo-end-of-life's
// Helpers/KeyValuesFactory.h (MIT, see third_party/licenses/Osiris-inventory.txt).
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
// Create a runtime material from a full VMT buffer (the old "airflow" creation
// path). This is KeyValues::FromString(shader, flat key/value pairs), NOT
// LoadFromBuffer(resourceName, full VMT): the body must be the flat parameter
// list, which is exactly what ChamsMaterial::Parameters extracts.
inline IMaterial* CreateVMT(const char* name,const char* shader,const char* vmt) {
    if(!Interfaces::MaterialSystem)return nullptr;
    const char* body=ChamsMaterial::Parameters(vmt);
    if(!shader || !body)return nullptr;
    auto* kv=Parse(shader,body);
    if(!kv){LOG_WARN("chams material parser unavailable: %s",name);return nullptr;}
    auto* material=Interfaces::MaterialSystem->CreateMaterial(name,kv);
    if(!material || material->IsErrorMaterial()){
        LOG_WARN("chams material creation failed: %s (shader %s)",name,shader);
        return nullptr;
    }
    material->IncrementReferenceCount();
    LOG_INFO("chams material created: %s (shader %s)",name,material->GetShaderName());
    return material;
}
}
