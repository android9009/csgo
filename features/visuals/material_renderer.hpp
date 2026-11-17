#pragma once
#include "../../sdk/material.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/visual_preview_state.hpp"
#include "../../utils/log.hpp"
#include "fatality_material_factory.hpp"
#include "chams_passes.hpp"

namespace PreviewMaterial {
// Fatality-derived materials with a shared, explicitly planned pass sequence.
inline IMaterial* cache[ChamsPasses::KindCount]{};
inline bool tried[ChamsPasses::KindCount]{};
struct Variables {IMaterialVar* env{};IMaterialVar* fresnel{};IMaterialVar* fresnel_range{};IMaterialVar* phong{};IMaterialVar* exponent{};};
inline Variables variables[ChamsPasses::KindCount]{};
inline IMaterialVar* Find(IMaterial* m,const char* name){bool found=false;auto* v=m->FindVar(name,&found,false);return found ? v : nullptr;}
inline void Set(IMaterialVar* v,const std::array<float,3>& c){if(v)v->SetVecValue(c[0],c[1],c[2]);}
inline IMaterial* active{};
inline float old_r{},old_g{},old_b{},old_a{};
inline bool old_z{},old_wire{},old_additive{};
inline float old_view_color[3]{1.f,1.f,1.f},old_blend{1.f};
inline bool view_saved{};
template<typename Fn> inline Fn ViewSlot(int slot){return MEM::GetVFunc<Fn>(Interfaces::RenderView,slot);}
inline IMaterial* Resolve(int kind) {
    if(!ChamsPasses::Selection(kind) || !Interfaces::MaterialSystem)return nullptr;
    if(tried[kind])return cache[kind];
    tried[kind]=true;
    const bool borrowed=kind==7;
    auto* material=borrowed ? Interfaces::MaterialSystem->FindMaterial("dev/glow_armsrace","Model textures",false) : FatalityMaterialFactory::Create(kind);
    // Never silently substitute a debug material for a missing chams shader.
    if(!material || material->IsErrorMaterial()){
        LOG_WARN("Fatality chams unavailable kind=%d; original model retained",kind);return nullptr;
    }
    if(borrowed)material->IncrementReferenceCount();
    auto& v=variables[kind];
    if(kind==7 || kind==ChamsPasses::Metallic)v.env=Find(material,"$envmaptint");
    if(kind==7){v.fresnel=Find(material,"$envmapfresnel");v.fresnel_range=Find(material,"$envmapfresnelminmaxexp");}
    if(kind==ChamsPasses::Shaded){v.phong=Find(material,"$phongtint");v.exponent=Find(material,"$phongexponent");}
    if(kind==ChamsPasses::Metallic)v.fresnel=Find(material,"$envmapfresnel");
    if(kind==7 && !v.env)LOG_WARN("chams kind=%d has no envmap tint",kind);
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
        material->SetMaterialVarFlag(MATERIAL_VAR_ADDITIVE,old_additive);
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
    old_additive=material->GetMaterialVarFlag(MATERIAL_VAR_ADDITIVE);
    active=material;
    // Fatality resets render-view modulation before setting material colors.
    if(Interfaces::RenderView){
        ViewSlot<void(__thiscall*)(void*,float*)>(7)(Interfaces::RenderView,old_view_color);
        old_blend=ViewSlot<float(__thiscall*)(void*)>(5)(Interfaces::RenderView);view_saved=true;
        constexpr float white[3]{1.f,1.f,1.f};
        ViewSlot<void(__thiscall*)(void*,const float*)>(6)(Interfaces::RenderView,white);
        ViewSlot<void(__thiscall*)(void*,float)>(4)(Interfaces::RenderView,1.f);
    }
    const auto primary=ChamsPasses::Tint(rgba),effect=ChamsPasses::Tint(secondary,true);
    material->ColorModulate(primary[0],primary[1],primary[2]);
    // Preserve the picker alpha continuously, including the last five percent.
    const float alpha=ChamsPasses::Alpha(rgba);
    auto& v=variables[kind];
    if(kind==7){
        // Only reflected Fresnel light is additive; no diffuse white veil.
        material->ColorModulate(0.f,0.f,0.f);
        Set(v.env,effect);
        material->SetMaterialVarFlag(MATERIAL_VAR_ADDITIVE,true);
        if(v.fresnel)v.fresnel->SetValue(1);
        Set(v.fresnel_range,{0.f,1.f,2.f});
    }
    if(kind==ChamsPasses::Shaded){
        Set(v.phong,ChamsPasses::Tint(secondary));
        if(v.exponent)v.exponent->SetValue(ChamsPasses::ShadedExponent(secondary));
    }else if(kind==ChamsPasses::Metallic){
        Set(v.env,ChamsPasses::Tint(secondary));
        if(v.fresnel)v.fresnel->SetValue(ChamsPasses::MetallicFresnel(secondary));
    }
    material->AlphaModulate(alpha);
    material->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ,ignore_z);
    material->SetMaterialVarFlag(MATERIAL_VAR_WIREFRAME,false);
    Interfaces::ModelRender->ForcedMaterialOverride(material);
    return true;
}
// Both callers draw exactly this plan. The preview has no occluder and requests
// visible passes only; live rendering requests hidden then visible passes.
template<typename DrawFn> inline bool Draw(const VisualPreview::Material& choice,bool hidden,DrawFn&& draw){
    const auto plan=ChamsPasses::Build(choice,hidden);bool drawn=false;
    for(int i=0;i<plan.count;++i){
        const auto& p=plan.passes[i];
        if(!Apply(p.kind,p.primary,p.ignore_z,p.secondary))continue;
        draw();drawn=true;Restore();
    }
    return drawn;
}

}
