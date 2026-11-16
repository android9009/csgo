#pragma once
#include "../../sdk/material.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/visual_preview_state.hpp"
#include "../../utils/log.hpp"
#include "fatality_material_factory.hpp"

namespace PreviewMaterial {
// Scoped material overrides shared by the preview and game model passes.
// Borrow engine materials, cache lookups, and restore every modified property.
inline IMaterial* cache[8]{};
inline bool tried[8]{};
inline IMaterial* active{};
inline float old_r{},old_g{},old_b{},old_a{};
inline bool old_z{};
inline IMaterialVar* active_var{};inline float old_vec[3]{};
inline IMaterial* Resolve(int kind) {
    if(kind<=0 || kind>=8 || !Interfaces::MaterialSystem)return nullptr;
    if(tried[kind])return cache[kind];
    tried[kind]=true;
    auto* material=kind==7 ? Interfaces::MaterialSystem->FindMaterial("dev/glow_armsrace","Model textures",false) : FatalityMaterialFactory::Create(kind);
    if(!material || material->IsErrorMaterial()) {
        LOG_WARN("Fatality material unavailable kind=%d; safe flat fallback",kind);
        material=kind==2 ? Interfaces::MaterialSystem->FindMaterial("debug/debugdrawflat","Model textures",false) : Resolve(2);
    }
    if(material && !material->IsErrorMaterial())cache[kind]=material;
    return cache[kind];
}
inline void Restore() {
    if(!active)return;
    if(Interfaces::ModelRender)Interfaces::ModelRender->ForcedMaterialOverride(nullptr);
    if(active_var)active_var->SetValue(old_vec[0],old_vec[1],old_vec[2]);
    active_var=nullptr;
    active->ColorModulate(old_r,old_g,old_b);active->AlphaModulate(old_a);
    active->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ,old_z);
    active=nullptr;
}
inline bool Apply(int kind,std::uint32_t rgba,bool ignore_z=false,std::uint32_t secondary=0xffffffff) {
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
    if(kind==3 || kind==4 || kind==7) {
        bool found=false;
        auto* var=material->FindVar(kind==4 ? "$selfillumtint" : "$envmaptint",&found,false);
        if(found && var){
            if(auto* v=var->GetVector()){old_vec[0]=v[0];old_vec[1]=v[1];old_vec[2]=v[2];active_var=var;}
            if(active_var){const auto c=kind==7 ? rgba : secondary;var->SetValue(float((c>>16)&255)/255.f,float((c>>8)&255)/255.f,float(c&255)/255.f);}
        }
        if(kind==3)material->ColorModulate(0.f,0.f,0.f);
        if(kind==7)material->ColorModulate(1.f,1.f,1.f);
    }
    Interfaces::ModelRender->ForcedMaterialOverride(material);
    return true;
}
}
