#pragma once
#include "../../sdk/material.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/visual_preview_state.hpp"
#include "../../utils/log.hpp"
#include "fatality_material_factory.hpp"

namespace PreviewMaterial {
// Material definitions, tint choices and pass ordering follow the supplied
// Fatality chams.cpp. Cache and retain borrowed engine materials as it does.
inline IMaterial* cache[8]{};
inline bool tried[8]{};
inline IMaterial* active{};
inline float old_r{},old_g{},old_b{},old_a{};
inline bool old_z{},old_wire{};
inline float old_view_color[3]{1.f,1.f,1.f},old_blend{1.f};
inline bool view_saved{};
template<typename Fn> inline Fn ViewSlot(int slot){return MEM::GetVFunc<Fn>(Interfaces::RenderView,slot);}
inline IMaterial* Resolve(int kind) {
    if(kind<=0 || kind>=8 || !Interfaces::MaterialSystem)return nullptr;
    if(tried[kind])return cache[kind];
    tried[kind]=true;
    const bool borrowed=kind==7;
    auto* material=borrowed ? Interfaces::MaterialSystem->FindMaterial("dev/glow_armsrace","Model textures",false) : FatalityMaterialFactory::Create(kind);
    // Never silently substitute a debug material for a missing chams shader.
    if(!material || material->IsErrorMaterial()){
        LOG_WARN("Fatality chams unavailable kind=%d; original model retained",kind);return nullptr;
    }
    if(borrowed)material->IncrementReferenceCount();
    cache[kind]=material;
    LOG_INFO("Fatality chams resolved kind=%d material=%s wireframe=%d",kind,material->GetName(),material->GetMaterialVarFlag(MATERIAL_VAR_WIREFRAME));
    return material;
}
inline void Restore() {
    if(!active && !view_saved)return;
    auto* material=active;active=nullptr;
    if(Interfaces::ModelRender)Interfaces::ModelRender->ForcedMaterialOverride(nullptr);
    if(material){
        material->ColorModulate(old_r,old_g,old_b);material->AlphaModulate(old_a);
        material->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ,old_z);
        material->SetMaterialVarFlag(MATERIAL_VAR_WIREFRAME,old_wire);
    }
    if(view_saved && Interfaces::RenderView){
        view_saved=false;
        ViewSlot<void(__thiscall*)(void*,const float*)>(6)(Interfaces::RenderView,old_view_color);
        ViewSlot<void(__thiscall*)(void*,float)>(4)(Interfaces::RenderView,old_blend);
    }
}
inline bool Apply(int kind,std::uint32_t rgba,bool ignore_z=false,std::uint32_t secondary=0xffffffff) {
    Restore();
    auto* material=Resolve(kind);
    if(!material || !Interfaces::ModelRender)return false;
    old_r=old_g=old_b=1.f;
    material->GetColorModulation(&old_r,&old_g,&old_b);
    old_a=material->GetAlphaModulation();old_z=material->GetMaterialVarFlag(MATERIAL_VAR_IGNOREZ);
    old_wire=material->GetMaterialVarFlag(MATERIAL_VAR_WIREFRAME);
    active=material;
    // Fatality resets render-view modulation before setting material colors.
    if(Interfaces::RenderView){
        ViewSlot<void(__thiscall*)(void*,float*)>(7)(Interfaces::RenderView,old_view_color);
        old_blend=ViewSlot<float(__thiscall*)(void*)>(5)(Interfaces::RenderView);view_saved=true;
        constexpr float white[3]{1.f,1.f,1.f};
        ViewSlot<void(__thiscall*)(void*,const float*)>(6)(Interfaces::RenderView,white);
        ViewSlot<void(__thiscall*)(void*,float)>(4)(Interfaces::RenderView,1.f);
    }
    material->ColorModulate(float((rgba>>16)&255)/255.f,float((rgba>>8)&255)/255.f,float(rgba&255)/255.f);
    material->AlphaModulate(float((rgba>>24)&255)/255.f);
    material->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ,ignore_z);
    material->SetMaterialVarFlag(MATERIAL_VAR_WIREFRAME,false);
    if(kind==3 || kind==4 || kind==7){
        bool found=false;
        auto* var=material->FindVar(kind==4 ? "$selfillumtint" : "$envmaptint",&found,false);
        // Reference code only writes the tint. Do not call the extra GetVector
        // virtual getter that was added by this port, particularly on Legacy.
        if(found && var){
            const auto c=kind==7 ? rgba : secondary;
            var->SetValue(float((c>>16)&255)/255.f,float((c>>8)&255)/255.f,float(c&255)/255.f);
        }
        if(kind==3)material->ColorModulate(0.f,0.f,0.f);
        if(kind==7)material->ColorModulate(1.f,1.f,1.f);
    }
    Interfaces::ModelRender->ForcedMaterialOverride(material);
    return true;
}
}
