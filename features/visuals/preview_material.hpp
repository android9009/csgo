#pragma once
#include "../../sdk/material.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/visual_preview_state.hpp"
#include "../../utils/log.hpp"

namespace PreviewMaterial {
// Preview-only overrides. Do not install a global DrawModelExecute override.
// Borrow engine materials, cache lookups, and restore every modified property.
inline IMaterial* cache[8]{};
inline bool tried[8]{};
inline IMaterial* active{};
inline float old_r{},old_g{},old_b{},old_a{};
inline bool old_z{};
inline IMaterial* Resolve(int kind) {
    if(kind<=0 || kind>=8 || !Interfaces::MaterialSystem)return nullptr;
    if(tried[kind])return cache[kind];
    tried[kind]=true;
    static const char* paths[]={"", "debug/debugambientcube", "debug/debugdrawflat", "dev/glow_armsrace",
        "models/inventory_items/dogtags/dogtags_outline", "models/inventory_items/trophy_majors/crystal_clear",
        "models/inventory_items/trophy_majors/gloss", "models/inventory_items/cologne_prediction/cologne_prediction_glass"};
    auto* material=Interfaces::MaterialSystem->FindMaterial(paths[kind],"Model textures",false);
    if(!material || material->IsErrorMaterial()) {
        LOG_WARN("chams preview: material %s unavailable; using flat fallback",paths[kind]);
        material=Interfaces::MaterialSystem->FindMaterial("debug/debugdrawflat","Model textures",false);
    }
    if(material && !material->IsErrorMaterial())cache[kind]=material;
    return cache[kind];
}
inline void Restore() {
    if(!active)return;
    if(Interfaces::ModelRender)Interfaces::ModelRender->ForcedMaterialOverride(nullptr);
    active->ColorModulate(old_r,old_g,old_b);active->AlphaModulate(old_a);
    active->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ,old_z);
    active=nullptr;
}
inline bool Apply(int kind,std::uint32_t rgba,bool ignore_z=false) {
    Restore();
    auto* material=Resolve(kind);
    if(!material || !Interfaces::ModelRender)return false;
    old_r=old_g=old_b=1.f;
    material->GetColorModulation(&old_r,&old_g,&old_b);
    old_a=material->GetAlphaModulation();old_z=material->GetMaterialVarFlag(MATERIAL_VAR_IGNOREZ);
    active=material;
    material->ColorModulate(float((rgba>>16)&255)/255.f,float((rgba>>8)&255)/255.f,float(rgba&255)/255.f);
    material->AlphaModulate(float((rgba>>24)&255)/255.f);
    material->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ,ignore_z);
    Interfaces::ModelRender->ForcedMaterialOverride(material);
    return true;
}
}
