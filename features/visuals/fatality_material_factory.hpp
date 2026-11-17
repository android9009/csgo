#pragma once
#include "fatality_material_definitions.hpp"
#include "chams_passes.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include <cstdint>
#include <cstring>
namespace FatalityMaterialFactory {
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
inline IMaterial* Create(int kind) {
    if(!Interfaces::MaterialSystem)return nullptr;
    const char* name=nullptr;const char* definition=nullptr;
    switch(kind){
        case 2:name="pastavar/chams_v3_flat";definition=FatalityMaterialDefinition::flat;break;
        case ChamsPasses::Shaded:name="pastavar/chams_shaded_v3";definition=FatalityMaterialDefinition::shaded;break;
        case ChamsPasses::Metallic:name="pastavar/chams_metallic_v3";definition=FatalityMaterialDefinition::metallic;break;
        default:return nullptr;
    }
    // This is KeyValues::FromString(shader, flat key/value pairs), NOT
    // LoadFromBuffer(resourceName, full VMT). Passing a full VMT here nests
    // all shader parameters under a child and makes the root the file name.
    const char* shader=kind==2 ? "UnlitGeneric" : "VertexLitGeneric";
    const char* body=std::strchr(definition,'{');if(!body)return nullptr;
    auto* kv=Parse(shader,body+1);
    if(!kv){LOG_WARN("Fatality material parser unavailable: kind=%d",kind);return nullptr;}
    auto* material=Interfaces::MaterialSystem->CreateMaterial(name,kv);
    if(material && !material->IsErrorMaterial())LOG_INFO("chams created kind=%d shader=%s",kind,material->GetShaderName());
    return material;
}
}
