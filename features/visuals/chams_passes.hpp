#pragma once
#include "../../sdk/visual_preview_state.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace ChamsPasses {
struct Pass {int kind{};std::uint32_t primary{},secondary{};bool ignore_z{};};
struct Plan {std::array<Pass,4> passes{};int count{};};
// Material selection = the cham_material bitset index (menu/config.hpp). The
// The old "airflow" project used eight material slots: 1 Textured, 2 Flat,
// 5 Fade Up, 6 Bubble, 7 Glow, 8 Custom Sprite, 9 Shaded, 10 Metallic (3, 4 were
// already dead). Shaded (9) and Metallic (10) are no longer offered (removed on
// request): their bit indices are kept so saved configs do not shift, but
// Selection() now maps them to off, leaving six active modes.
inline constexpr int Shaded=9,Metallic=10,GlowKind=7,KindCount=11;
inline int Selection(int kind){
    switch(kind){
        case 1:case 2:case 5:case 6:case GlowKind:case 8:return kind;
        default:return 0;
    }
}
// Only Glow consumes the secondary (effect) colour (the additive rim tint).
inline bool UsesSecondary(int kind){return kind==GlowKind;}
inline bool Composite(int kind){return kind==GlowKind;}
inline bool FreshSnapshot(float captured,float now){return std::isfinite(captured) && std::isfinite(now) && now>=captured && now-captured<=.25f;}
inline float GhostFade(float age){return !std::isfinite(age) || age<0.f || age>=1.f ? 0.f : std::clamp((1.f-age)/.4f,0.f,1.f);}
inline float Alpha(std::uint32_t c){return float((c>>24)&255)/255.f;}
// Old project: fill_amount = clamp(glow_fill/100 - 0.15, 0, 1). Here the
// secondary colour's alpha (0..1) is the fill amount.
inline float GlowFill(std::uint32_t color){
    const float a=Alpha(color);
    return a<=.15f ? 0.f : ((std::min)(a-.15f,1.f));
}
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
