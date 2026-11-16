#pragma once
#include "../../sdk/visual_preview_state.hpp"
#include <array>
#include <cstdint>
namespace ChamsPasses {
struct Pass {int kind{};std::uint32_t primary{},secondary{};bool ignore_z{};};
struct Plan {std::array<Pass,4> passes{};int count{};};
inline bool Composite(int kind){return kind==3 || kind==4 || kind==5 || kind==7;}
inline float Alpha(std::uint32_t c){return float((c>>24)&255)/255.f;}
inline std::array<float,3> Tint(std::uint32_t c,bool intensity=false){
    const float a=intensity ? Alpha(c) : 1.f;
    return {float((c>>16)&255)/255.f*a,float((c>>8)&255)/255.f*a,float(c&255)/255.f*a};
}
inline Plan Build(const VisualPreview::Material& material,bool include_hidden){
    Plan plan;
    if(!material.enabled || material.kind<1 || material.kind>7)return plan;
    auto layer=[&](std::uint32_t color,bool hidden){
        // Hidden effects keep the hidden hue, but use Secondary's strength.
        const auto effect=hidden ? (material.hidden&0xffffffu)|(material.secondary&0xff000000u) : material.secondary;
        if(Composite(material.kind))plan.passes[plan.count++]={material.kind==4 ? 1 : 2,color,effect,hidden};
        plan.passes[plan.count++]={material.kind,color,effect,hidden};
    };
    if(include_hidden && material.ignore_z)layer(material.hidden,true);
    layer(material.primary,false);
    return plan;
}
}
