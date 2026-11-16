#pragma once
#include "../../sdk/visual_preview_state.hpp"
#include <array>
#include <cstdint>
namespace ChamsPasses {
struct Pass {int kind{};std::uint32_t primary{},secondary{};bool ignore_z{};};
struct Plan {std::array<Pass,4> passes{};int count{};};
// Keep saved Flat / Legacy numeric values compatible with existing configs.
inline constexpr int Shaded=9,Metallic=10,KindCount=11;
inline int Selection(int kind){return kind==2 || kind==7 || kind==Shaded || kind==Metallic ? kind : 0;}
// Skeet prepare_material kinds 2 / 3 use Secondary alpha for highlight
// exponent / Fresnel response, not for diffuse opacity.
inline int ShadedExponent(std::uint32_t color){return (std::max)(1,255-static_cast<int>(color>>24));}
inline float MetallicFresnel(std::uint32_t color){return 1.f-float(color>>24)/255.f;}
inline bool Composite(int kind){return kind==7;}
inline bool FreshSnapshot(float captured,float now){return std::isfinite(captured) && std::isfinite(now) && now>=captured && now-captured<=.25f;}
inline float GhostFade(float age){return !std::isfinite(age) || age<0.f || age>=1.f ? 0.f : std::clamp((1.f-age)/.4f,0.f,1.f);}
inline float Alpha(std::uint32_t c){return float((c>>24)&255)/255.f;}
inline std::array<float,3> Tint(std::uint32_t c,bool intensity=false){
    const float a=intensity ? Alpha(c) : 1.f;
    return {float((c>>16)&255)/255.f*a,float((c>>8)&255)/255.f*a,float(c&255)/255.f*a};
}
inline Plan Build(const VisualPreview::Material& material,bool include_hidden){
    Plan plan;
    if(!material.enabled || Selection(material.kind)==0)return plan;
    auto layer=[&](std::uint32_t color,bool hidden){
        // Hidden effects keep the hidden hue, but use Secondary's strength.
        const auto effect=hidden ? (material.hidden&0xffffffu)|(material.secondary&0xff000000u) : material.secondary;
        if(Composite(material.kind))plan.passes[plan.count++]={2,color,effect,hidden};
        plan.passes[plan.count++]={material.kind,color,effect,hidden};
    };
    if(include_hidden && material.ignore_z)layer(material.hidden,true);
    layer(material.primary,false);
    return plan;
}
}
