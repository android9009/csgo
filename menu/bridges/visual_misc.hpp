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
    s.tracer_targets=static_cast<unsigned>(static_cast<std::uint64_t>(m.tracer_targets.get()));s.hitmarkers=static_cast<unsigned>(static_cast<std::uint64_t>(m.hitmarker.get()));s.impacts=m.impacts.get();s.impact_color=m.impacts_color2.get().rgba();
    for(int i=0;i<3;++i){s.tracer_styles[i]=static_cast<unsigned>(static_cast<std::uint64_t>(m.tracer_style[i].get()));s.tracer_colors[i]=m.tracer_colors[i].get().rgba();}
    for(int i=1;i<4;++i)s.hit_duration[i]=m.hitmarker_duration[i].get();
    s.hit_colors={0,m.hitmarker_world_color.get().rgba(),m.hitmarker_screen_color.get().rgba(),m.hitmarker_kibit_color.get().rgba()};
    s.vignette=w.vignette.get();s.local_contrast=w.local_contrast.get();s.correction=w.color_correction.get();s.grayscale=w.grayscale.get();s.tone_curve=static_cast<unsigned>(static_cast<std::uint64_t>(w.tone_curve.get()));s.vignette_color=w.vignette_color.get().rgba();
    s.contrast=w.correction_contrast.get();s.highlights=w.highlights.get();s.shadows=w.shadows.get();s.saturation=w.saturation.get();s.vibrance=w.vibrance.get();s.temperature=w.temperature.get();s.tint=w.tint.get();
    s.local_strength=w.contrast_strength.get();s.edge_strength=w.contrast_edge_strength.get();s.vignette_start=w.contrast_vignette_start.get();s.vignette_end=w.contrast_vignette_end.get();s.vignette_blur=w.contrast_vignette_blur.get();
    s.trajectory=m.grenade_tracer.get();s.trajectory_hit=m.tracer_color.get().rgba();s.trajectory_miss=m.box_color.get().rgba();
    s.arrows=v.out_of_fov.get();s.arrow_distance=v.out_of_fov_dst.get();s.arrow_size=v.out_of_fov_size.get();s.arrow_color=v.out_of_fov_col.get().rgba();
    s.bloom_screen=w.bloom_screen_strength.get();s.bloom_blur=w.bloom_blur_strength.get();s.bloom_threshold=w.bloom_threshold.get();s.bloom_width=w.bloom_threshold_width.get();
    for(int i=0;i<5;++i){s.bloom_weights[i]=w.bloom_weights[i].get();s.bloom_colors[i]=w.bloom_weight_colors[i].get().rgba();}
    s.sunlight=w.sunlight.get();s.sunlight_color=w.sunlight_color.get().rgba();
    s.dof=w.depth_of_field.get();s.dof_mode=static_cast<unsigned>(static_cast<std::uint64_t>(w.dof_mode.get()));s.focus_distance=w.focus_distance.get();s.focus_speed=w.focus_speed.get();s.focus_range=w.focus_range.get();
    VisualMisc::Publish(s);
}
inline void RenderEvents(){
    using namespace evo::ren;using namespace evo::gui;
    VisualMisc::EventFrame frame;if(!VisualMisc::CopyEvents(frame))return;
    const auto s=VisualMisc::Get();std::array<float,16> m{};if(!VisualMisc::CurrentMatrix(m))return;
    const auto display=draw.display/draw.scale;auto& d=draw.layers[l_back];
    auto project=[&](const Vector& v,vec2& p){const float w=m[12]*v.x+m[13]*v.y+m[14]*v.z+m[15];if(w<.01f)return false;p={display.x*.5f*(1.f+(m[0]*v.x+m[1]*v.y+m[2]*v.z+m[3])/w),display.y*.5f*(1.f-(m[4]*v.x+m[5]*v.y+m[6]*v.z+m[7])/w)};return true;};
    VisualMisc::ArrowFrame arrows;
    if(s.arrows && VisualMisc::CopyArrows(arrows))for(const auto& point:arrows.points){
        if(point.alpha<.01f)continue;vec2 screen;
        if(project(point.position,screen) && screen.x>=0.f && screen.x<=display.x && screen.y>=0.f && screen.y<=display.y)continue;
        const Vector delta=point.position-arrows.eye;
        const float right=m[0]*delta.x+m[1]*delta.y+m[2]*delta.z;
        const float forward=m[12]*delta.x+m[13]*delta.y+m[14]*delta.z;
        const float angle=std::atan2(-forward,right);const vec2 direction{std::cos(angle),std::sin(angle)},side{-direction.y,direction.x};
        const vec2 center=display*.5f;const float radius=(std::min)(display.x,display.y)*.45f*std::clamp(s.arrow_distance*.01f,.01f,1.f),size=std::clamp(s.arrow_size,1.f,50.f);
        const vec2 tip=center+direction*radius,base=tip-direction*size;const color c{color{s.arrow_color},point.alpha};
        d->add_line(tip,base+side*(size*.5f),c);d->add_line(tip,base-side*(size*.5f),c);d->add_line(base+side*(size*.5f),base-side*(size*.5f),c);
    }
    VisualMisc::Trajectory path;
    if(s.trajectory && VisualMisc::CopyTrajectory(path)){
        const color c{path.hit?s.trajectory_hit:s.trajectory_miss};vec2 a,b;
        for(std::size_t i=1;i<path.count;++i)if(project(path.points[i-1],a) && project(path.points[i],b)){d->add_line(a,b,c);if(path.bounces[i]){d->add_line(b-vec2{3,0},b+vec2{3,0},c);d->add_line(b-vec2{0,3},b+vec2{0,3},c);}}
        if(path.count && path.detonated && project(path.points[path.count-1],b)){d->add_line(b-vec2{5,5},b+vec2{5,5},c);d->add_line(b-vec2{-5,5},b+vec2{-5,5},c);}
    }
    auto cross=[&](vec2 p,color c,float size){for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})d->add_line(p+vec2{x*2.f,y*2.f},p+vec2{x*size,y*size},c);};
    for(std::size_t i=0;i<frame.count;++i){const auto& r=frame.records[i];const float age=frame.time-r.time;vec2 a,b;
        if(r.kind==0 && age<1.5f && (s.tracer_targets&(1u<<r.group)) && (s.tracer_styles[r.group]&2u) && project(r.start,a) && project(r.end,b))d->add_line(a,b,color{color{s.tracer_colors[r.group]},1.f-age/1.5f});
        if(r.kind==0 && s.impacts && r.group==0 && age<4.f){
            std::array<vec2,8> points;bool visible=true;for(unsigned j=0;j<8;++j)visible=project(r.end+Vector{(j&1)?2.f:-2.f,(j&2)?2.f:-2.f,(j&4)?2.f:-2.f},points[j]) && visible;
            if(visible)for(unsigned j=0;j<8;++j)for(unsigned bit:{1u,2u,4u})if(!(j&bit))d->add_line(points[j],points[j|bit],color{s.impact_color});
        }
        if(r.kind==1)for(unsigned mode=1;mode<4;++mode){const float duration=std::clamp(s.hit_duration[mode],0.f,10.f);if(!(s.hitmarkers&(1u<<mode)) || duration<=0.f || age>=duration)continue;
            const color c{color{s.hit_colors[mode]},1.f-age/duration};if(mode==1){if(project(r.end,a))cross(a,c,7.f);}else if(mode==2)cross(display*.5f,c,8.f);else{const vec2 p=display*.5f;d->add_line(p+vec2{-7,0},p+vec2{0,7},c);d->add_line(p+vec2{0,7},p+vec2{7,-5},c);}
        }
    }
}
inline void Render(){
    using namespace evo::ren;using namespace evo::gui;
    RenderEvents();
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
