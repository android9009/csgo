#pragma once
#include "../tabs/tabs.hpp"
#include "../config.hpp"
#include "../../features/misc/state.hpp"
namespace MiscBridge {
inline void Configure(){
    auto& m=cfg.misc;MiscRuntime::Settings s;
    s.console_filter=m.console_filter.get();s.ping_spike=m.ping_spike_enabled.get();s.ping_amount=cfg.aim.ping_spike_amt.get();s.clantag=m.clantag.get();s.ragdoll_gravity=m.ragdoll_thrust_enabled.get();s.ragdoll_amount=cfg.visuals.ragdoll_thrust.get();s.long_jump=m.long_jump.get();s.ladder_jump=m.ladder_jump.get();
    s.indicators=static_cast<unsigned>(static_cast<std::uint64_t>(cfg.aa.indicators.get()));
    // The current Rage page is built with profile key 0 (port.cpp), not
    // reference Fatality's general_id=512. Do not read non-existent overrides.
    constexpr unsigned key=0;
    auto mark=[&](int index,bool active){if(active)s.indicator_active|=1u<<index;};
    mark(cfg_t::indicators_ping,MiscRuntime::PingActive());mark(cfg_t::indicators_dt,cfg.aim.doubletap.get());mark(cfg_t::indicators_st,cfg.aim.hideshot.get());
    mark(cfg_t::indicators_fs,cfg.aa.enabled.get() && cfg.aa.freestand.get());mark(cfg_t::indicators_manual,cfg.aa.manual.get() && (cfg.aa.manual_left.get() || cfg.aa.manual_right.get() || cfg.aa.manual_back.get()));
    mark(cfg_t::indicators_do,cfg.weapon_config.mindmg_override_enabled[key].get());mark(cfg_t::indicators_safe,cfg.weapon_config.safe_mode[key].get().test(2));
    mark(cfg_t::indicators_head,cfg.aim.hsmode.get());mark(cfg_t::indicators_baim,cfg.aim.baim_on_key.get() || cfg.weapon_config.body_mode[key].get().test(2));
    s.edge_bug=m.edge_bug.get();s.pixel_surf=m.pixel_surf.get();s.edge_stop=m.edge_stop.get();
    s.peek=m.peek_assist.get();s.retreat_on_release=m.peek_assist_retreat_on_release.get();s.peek_distance=m.peek_assist_distance.get();s.no_fall=m.no_fall_damage.get();s.edge_jump=m.edge_jump.get();s.autoaccept=m.autoaccept.get();
    s.bhop=m.bhop.get();s.instant_duck=m.instant_unduck.get();s.quick_stop=cfg.legit.quick_stop.get();
    s.strafe=static_cast<unsigned>(static_cast<std::uint64_t>(m.autostrafe.get()));s.smoothing=m.autostrafe_smoothing.get();s.avoid_walls=m.avoid_walls.get();s.radar=m.radar.get();
    s.logs=static_cast<unsigned>(static_cast<std::uint64_t>(m.log_types.get()));s.console_logs=m.console_logs.get();
    s.hit_details=static_cast<unsigned>(static_cast<std::uint64_t>(m.hit_log_details.get()));s.buy_targets=static_cast<unsigned>(static_cast<std::uint64_t>(m.buy_log_targets.get()));
    s.primary=static_cast<int>(m.buybot_primary.get());s.pistol=static_cast<int>(m.buybot_pistol.get());s.armor=static_cast<int>(m.buybot_kevlar_helmet.get());
    s.defuser=m.buybot_defuser.get();s.taser=m.buybot_taser.get();s.grenades=m.buybot_grenades.get();
    s.hit_enabled=m.hit_sound_enabled.get();s.kill_enabled=m.kill_sound_enabled.get();s.hit_sound=m.hitsound_select.get().first_set_bit().value_or(0);s.kill_sound=m.kill_sound_select.get().first_set_bit().value_or(0);
    for(int i=0;i<5;++i)s.colors[i]=m.log_colors[i].get().rgba();MiscRuntime::Publish(s);
}
inline void RenderPeek(){
    using namespace evo::ren;using namespace evo::gui;
    const auto frame=MiscRuntime::GetPeekMarker();
    if(!cfg.misc.peek_assist.get() || !MiscRuntime::Alive() || !frame.valid)return;
    const auto& m=frame.matrix;const auto display=draw.display/draw.scale;
    auto project=[&](const Vector& point,vec2& out){
        const float w=m[12]*point.x+m[13]*point.y+m[14]*point.z+m[15];
        if(!std::isfinite(w) || w<.01f)return false;
        out={display.x*.5f*(1.f+(m[0]*point.x+m[1]*point.y+m[2]*point.z+m[3])/w),
            display.y*.5f*(1.f-(m[4]*point.x+m[5]*point.y+m[6]*point.z+m[7])/w)};
        return std::isfinite(out.x) && std::isfinite(out.y);
    };
    auto& layer=draw.layers[l_back];
    const auto base=frame.returning ? cfg.misc.peek_assist_col2.get() : cfg.misc.peek_assist_col.get();
    const bool filled=cfg.misc.peek_assist_visual.get().test(1);
    const Vector origin=frame.origin+Vector{0.f,0.f,2.f};vec2 center;
    const bool center_visible=project(origin,center);
    for(int i=0;i<48;++i){
        const float a=static_cast<float>(i)*6.283185307f/48.f,b=static_cast<float>(i+1)*6.283185307f/48.f;
        vec2 first,second;
        if(!project(origin+Vector{std::cos(a)*16.f,std::sin(a)*16.f,0.f},first) ||
           !project(origin+Vector{std::cos(b)*16.f,std::sin(b)*16.f,0.f},second))continue;
        if(filled && center_visible)layer->add_triangle_filled(center,first,second,color{base,base.value.a*.2f});
        if(!filled){layer->add_line(first,second,color{base,base.value.a*.12f},7.f);layer->add_line(first,second,color{base,base.value.a*.3f},4.f);}
        layer->add_line(first,second,base,1.5f);
    }
}
inline void Render(){
    using namespace evo::ren;using namespace evo::gui;
    RenderPeek();
    // Event logs remain visible after death; movement indicators do not.
    auto& overlay=draw.layers[l_back];overlay->font=draw.fonts[GUI_HASH("gui_main")];
    float log_y=15.f;const double now=static_cast<double>(GetTickCount64())*.001;
    for(const auto& message:MiscRuntime::Messages()){
        if(!message.text[0] || now-message.time>5.f || now<message.time)continue;
        const auto rgba=message.color;
        overlay->add_text({15.f,log_y},message.text,color{static_cast<int>((rgba>>16)&255),static_cast<int>((rgba>>8)&255),static_cast<int>(rgba&255),static_cast<int>((rgba>>24)&255)});log_y+=20.f;
    }
    if(!MiscRuntime::Alive())return;
    const auto settings=MiscRuntime::Get();const auto active=settings.indicators&settings.indicator_active;
    auto& d=draw.layers[l_back];d->font=draw.fonts[GUI_HASH("gui_main")];
    const auto display=draw.display/draw.scale;float y=display.y-90.f;
    struct Item {int bit;const char* label;};
    const Item items[]={{cfg_t::indicators_ping,"PING"},{cfg_t::indicators_manual,"MANUAL"},{cfg_t::indicators_fs,"FS"},{cfg_t::indicators_dt,"DT"},{cfg_t::indicators_st,"OSAA"},{cfg_t::indicators_do,"MD"},{cfg_t::indicators_safe,"SAFE"},{cfg_t::indicators_head,"HS"},{cfg_t::indicators_baim,"BODY"}};
    for(const auto& item:items){if(!(active&(1u<<item.bit)))continue;
        d->add_rect_filled_multicolor({{0.f,y-2.f},{125.f,y+20.f}},{color{0,0,0,0},color{0,0,0,51},color{0,0,0,51},color{0,0,0,0}});
        d->add_text({30.f,y+1.f},item.label,color{0,0,0,220});d->add_text({29.f,y},item.label,color{255,255,255,200});y-=26.f;
    }
}
}
