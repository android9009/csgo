#pragma once
#include "../tabs/tabs.hpp"
#include "../config.hpp"
#include "../../features/misc/state.hpp"
namespace MiscBridge {
inline void Configure(){
    auto& m=cfg.misc;MiscRuntime::Settings s;
    s.ping_spike=m.ping_spike_enabled.get();s.ping_amount=cfg.aim.ping_spike_amt.get();s.clantag=m.clantag.get();s.ragdoll_gravity=m.ragdoll_thrust_enabled.get();s.ragdoll_amount=cfg.visuals.ragdoll_thrust.get();s.long_jump=m.long_jump.get();s.ladder_jump=m.ladder_jump.get();
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
    s.primary=static_cast<int>(m.buybot_primary.get());s.pistol=static_cast<int>(m.buybot_pistol.get());s.armor=static_cast<int>(m.buybot_kevlar_helmet.get());
    s.defuser=m.buybot_defuser.get();s.taser=m.buybot_taser.get();s.grenades=m.buybot_grenades.get();
    s.hit_enabled=m.hit_sound_enabled.get();s.kill_enabled=m.kill_sound_enabled.get();s.hit_sound=m.hitsound_select.get().first_set_bit().value_or(0);s.kill_sound=m.kill_sound_select.get().first_set_bit().value_or(0);
    for(int i=0;i<5;++i)s.colors[i]=m.log_colors[i].get().rgba();MiscRuntime::Publish(s);
}
inline void Render(){
    using namespace evo::ren;using namespace evo::gui;
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
