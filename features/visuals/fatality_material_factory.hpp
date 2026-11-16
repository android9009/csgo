#pragma once
#include "fatality_material_definitions.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include <cstdint>
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
    if(!Interfaces::MaterialSystem || kind<1 || kind>6)return nullptr;
    using namespace FatalityMaterialDefinition;
    static const char* definitions[]={"",normal,flat,rim_glow,contour,flow,acrylic};
    static const char* names[]={"","pastavar/chams_v2_normal","pastavar/chams_v2_flat","pastavar/chams_v2_rim","pastavar/chams_v2_contour","pastavar/chams_v2_flow","pastavar/chams_v2_acrylic"};
    auto* kv=Parse(names[kind],definitions[kind]);
    if(!kv){LOG_WARN("Fatality material parser unavailable: kind=%d",kind);return nullptr;}
    auto* material=Interfaces::MaterialSystem->CreateMaterial(names[kind],kv);
    if(material && !material->IsErrorMaterial())LOG_INFO("chams created kind=%d shader=%s",kind,material->GetShaderName());
    return material;
}
}
