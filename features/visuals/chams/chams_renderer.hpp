#pragma once
#include "../../../sdk/material.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/visual_preview_state.hpp"
#include "../../../utils/log.hpp"
#include "../../../utils/memory.hpp"
#include "chams_material_factory.hpp"
#include "chams_passes.hpp"

namespace PreviewMaterial {
// Chams runtime material set, one instance per recipe (see chams_materials.hpp).
inline IMaterial* cache[ChamsMaterial::Count]{};
inline bool tried[ChamsMaterial::Count]{};
struct Variables {IMaterialVar* env{};IMaterialVar* phong{};IMaterialVar* fresnel_range{};};
inline Variables variables[ChamsMaterial::Count]{};
inline IMaterialVar* Find(IMaterial* m,const char* name){bool found=false;auto* v=m->FindVar(name,&found,false);return found ? v : nullptr;}
inline void Set(IMaterialVar* v,const std::array<float,3>& c){if(v)v->SetVecValue(c[0],c[1],c[2]);}
inline IMaterial* active{};
inline float old_view_color[3]{1.f,1.f,1.f},old_blend{1.f};
inline bool view_saved{},studio_override{};
// IStudioRender slot 33 = ForcedMaterialOverride(material, type, overrides). Verified on the
// supplied build: type 0 replaces the override, overrides == -1 covers every submodel, and
// type 3 pushes onto the engine's 4-deep override stack (docs/interface-verification.md).
inline void OverrideStudio(){if(Interfaces::StudioRender){MEM::GetVFunc<void(__thiscall*)(void*,IMaterial*,int,int)>(Interfaces::StudioRender,33)(Interfaces::StudioRender,active,0,-1);studio_override=true;}}
template<typename Fn> inline Fn ViewSlot(int slot){return MEM::GetVFunc<Fn>(Interfaces::RenderView,slot);}
// cham_material bit index -> recipe index (see chams_materials.hpp).
inline int IndexForKind(int kind){
    switch(kind){
        case 1:return ChamsMaterial::Soft;
        case 2:return ChamsMaterial::Flat;
        case 5:return ChamsMaterial::FadeUp;
        case 6:return ChamsMaterial::Bubble;
        case ChamsPasses::GlowKind:return ChamsMaterial::Glow;
        case 8:return ChamsMaterial::World;
        case ChamsPasses::Shaded:return ChamsMaterial::Shaded;
        case ChamsPasses::Metallic:return ChamsMaterial::Shiny;
        default:return -1;
    }
}
inline IMaterial* Resolve(int index) {
    if(index<0 || index>=ChamsMaterial::Count || !Interfaces::MaterialSystem)return nullptr;
    if(tried[index])return cache[index];
    tried[index]=true;
    // Never silently substitute a debug material for a missing chams shader.
    cache[index]=ChamsMaterialFactory::CreateVMT(ChamsMaterial::Name(index),
        ChamsMaterial::Shader(index),ChamsMaterial::VMT(index));
    if(!cache[index])LOG_WARN("chams material unavailable recipe=%d; original model retained",index);
    else{
        auto& v=variables[index];
        v.env=Find(cache[index],"$envmaptint");
        v.phong=Find(cache[index],"$phongtint");
        v.fresnel_range=Find(cache[index],"$envmapfresnelminmaxexp");
        // Fade Up shows only the fresnel rim: kill the diffuse permanently.
        if(index==ChamsMaterial::FadeUp)cache[index]->ColorModulate(0.f,0.f,0.f);
    }
    return cache[index];
}
inline void Restore() {
    if(!active && !view_saved && !studio_override)return;
    if(studio_override && Interfaces::StudioRender){MEM::GetVFunc<void(__thiscall*)(void*,IMaterial*,int,int)>(Interfaces::StudioRender,33)(Interfaces::StudioRender,nullptr,0,-1);studio_override=false;}
    active=nullptr;
    if(Interfaces::ModelRender)Interfaces::ForcedMaterialOverride(nullptr);
    if(view_saved && Interfaces::RenderView){
        view_saved=false;
        ViewSlot<void(__thiscall*)(void*,const float*)>(6)(Interfaces::RenderView,old_view_color);
        ViewSlot<void(__thiscall*)(void*,float)>(4)(Interfaces::RenderView,old_blend);
    }
}
inline bool Apply(int kind,std::uint32_t rgba,bool ignore_z=false,std::uint32_t secondary=0xffffffff) {
    Restore();
    const int index=IndexForKind(kind);
    auto* material=Resolve(index);
    if(!material || !Interfaces::ModelRender)return false;
    // Airflow pass state: translucent + nofog + per-pass ignore-Z on the material;
    // the colour comes from the render view's modulation/blend, not from the
    // material's own ColorModulate/AlphaModulate (that was the broken part).
    material->SetMaterialVarFlag(MATERIAL_VAR_TRANSLUCENT,true);
    material->SetMaterialVarFlag(MATERIAL_VAR_NOFOG,true);
    material->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ,ignore_z);
    material->SetMaterialVarFlag(MATERIAL_VAR_WIREFRAME,false);
    const bool glow=index==ChamsMaterial::Glow;
    // The glow overlay is drawn in the secondary (glow) colour; every other
    // recipe is drawn in the primary colour.
    const std::uint32_t color=glow ? secondary : rgba;
    auto& v=variables[index];
    // Recipes with an envmap are tinted by the draw colour (airflow sets
    // $envmaptint to the main colour on every pass).
    if(v.env)Set(v.env,ChamsPasses::Tint(color));
    if(glow && v.fresnel_range){
        const auto fill=ChamsPasses::GlowFill(color);
        v.fresnel_range->SetVecValue(0.f,1.f,5.f-5.f*fill);
    }
    if(v.phong)Set(v.phong,ChamsPasses::Tint(secondary));
    if(Interfaces::RenderView){
        ViewSlot<void(__thiscall*)(void*,float*)>(7)(Interfaces::RenderView,old_view_color);
        old_blend=ViewSlot<float(__thiscall*)(void*)>(5)(Interfaces::RenderView);view_saved=true;
        const auto rgb=ChamsPasses::Tint(color);
        ViewSlot<void(__thiscall*)(void*,const float*)>(6)(Interfaces::RenderView,rgb.data());
        ViewSlot<void(__thiscall*)(void*,float)>(4)(Interfaces::RenderView,ChamsPasses::Alpha(color));
    }
    active=material;
    Interfaces::ForcedMaterialOverride(material);
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
