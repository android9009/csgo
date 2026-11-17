#pragma once
#include "../tabs/tabs.hpp"
#include "../config.hpp"
#include "../../features/visuals/misc_runtime.hpp"
namespace VisualMiscBridge {
inline void Configure(){
    auto& m=cfg.misc;auto& v=cfg.visuals;auto& w=v.world_effects;VisualMisc::Settings s;
    s.third=m.thirdperson.get();s.dead=m.thirdperson_dead.get();s.grenade_off=m.thirdperson_grenade.get();s.instant=m.thirdperson_no_interp.get();s.distance=m.thirdperson_dist.get();
    s.scope_alpha=m.transparency_in_scope.get();s.nade_alpha=m.transparency_nade.get();s.fov_enabled=m.fov_enabled.get();s.fov=m.fov.get();s.zoom=m.fov_scope.get();s.aspect=m.aspect.get();s.second_zoom=m.force_second_zoom.get();s.viewmodel_scope=m.force_viewmodel_scope.get();
    s.vm_enabled=m.vm_fov_enabled.get();s.vm_fov=m.vm_fov.get();s.vm_position={m.viewmodel_x.get(),m.viewmodel_y.get(),m.viewmodel_z.get()};s.vm_angles={m.viewmodel_pitch.get(),m.viewmodel_yaw.get(),m.viewmodel_roll.get()};s.landing_bob=m.disable_landing_bob.get();
    s.scope=static_cast<unsigned>(static_cast<std::uint64_t>(m.noscope.get()));s.scope_invert=m.scope_invert.get();s.scope_size=m.scope_line_size.get();s.scope_color=m.scope_line_color.get().rgba();s.crosshair=m.force_crosshair.get();
    s.spread=v.spread_circle_type.get().test(cfg_t::spread_circle_gradient);s.removals=static_cast<unsigned>(static_cast<std::uint64_t>(m.removals.get()));s.texture_alpha=m.texture_transparency.get();s.prop_alpha=v.prop_transparency.get();
    s.night=w.nightmode.get();s.fullbright=w.fullbright.get();s.sky=w.sky.get();s.fog=w.fog.get();s.exposure=w.exposure_enabled.get();s.bloom=w.bloom.get();
    s.night_color=w.nightmode_color.get().rgba();s.fullbright_color=w.fullbright_color.get().rgba();s.sky_color=w.sky_color.get().rgba();s.fog_color=w.fog_color.get().rgba();s.exposure_value=w.exposure.get();s.bloom_value=w.bloom_strength.get();
    VisualMisc::Publish(s);
}
inline void Render(){
    using namespace evo::ren;using namespace evo::gui;
    const auto frame=VisualMisc::GetLocal();if(!frame.alive)return;
    const auto s=VisualMisc::Get();auto& layer=draw.layers[l_back];const auto display=draw.display/draw.scale;const vec2 center=display*.5f;
    std::array<float,16> matrix{};float radius=0.f;
    if(VisualMisc::CurrentMatrix(matrix))radius=std::clamp(frame.spread*std::abs(matrix[0])*display.x*.5f,0.f,display.y);
    if(s.spread && radius>0.f)layer->add_circle_filled_multicolor(center,radius,{cfg.visuals.spread_circle_color1.get(),cfg.visuals.spread_circle_color2.get()},64);
    if(!frame.scoped || !(s.scope&~1u))return;
    const bool dynamic=(s.scope&((1u<<cfg_t::noscope_dynamic)|(1u<<cfg_t::noscope_gradient_dynamic)))!=0;
    const bool gradient=(s.scope&((1u<<cfg_t::noscope_gradient_static)|(1u<<cfg_t::noscope_gradient_dynamic)))!=0;
    const float gap=dynamic ? (std::min)(radius*.5f,display.y*.3f) : 0.f;
    const float length=std::clamp(s.scope_size*.01f,.05f,1.f);const float x=display.x*.5f*length,y=display.y*.5f*length;
    const color line{s.scope_color};const color transparent{line,0.f};
    auto segment=[&](const vec2& a,const vec2& b,bool vertical,bool reverse){
        if(!gradient){layer->add_line(a,b,line);return;}
        color first=line,last=transparent;if(reverse!=s.scope_invert){first=transparent;last=line;}
        const vec2 padding=vertical ? vec2{1.f,0.f} : vec2{0.f,1.f};
        layer->add_rect_filled_multicolor({a,b+padding},vertical ? std::array<color,4>{first,first,last,last} : std::array<color,4>{first,last,last,first});
    };
    if(x>gap){segment(center-vec2{x,0},center-vec2{gap,0},false,true);segment(center+vec2{gap,0},center+vec2{x,0},false,false);}
    if(y>gap){segment(center-vec2{0,y},center-vec2{0,gap},true,true);segment(center+vec2{0,gap},center+vec2{0,y},true,false);}
}
}
