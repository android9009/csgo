#pragma once
#include "menu.h"
#include "../cfg.h"
#include "../../../features/misc/state.hpp"
namespace MiscBridge {
inline void Configure(){
    auto& m=cfg.misc;MiscRuntime::Settings s;
    s.peek=m.peek_assist.get();s.retreat_on_release=m.peek_assist_retreat_on_release.get();s.peek_distance=m.peek_assist_distance.get();s.no_fall=m.no_fall_damage.get();s.edge_jump=m.edge_jump.get();s.autoaccept=m.autoaccept.get();
    s.bhop=m.bhop.get();s.instant_duck=m.instant_unduck.get();s.quick_stop=cfg.legit.quick_stop.get();
    s.strafe=static_cast<unsigned>(static_cast<std::uint64_t>(m.autostrafe.get()));s.smoothing=m.autostrafe_smoothing.get();s.avoid_walls=m.avoid_walls.get();s.radar=m.radar.get();
    s.primary=static_cast<int>(m.buybot_primary.get());s.pistol=static_cast<int>(m.buybot_pistol.get());s.armor=static_cast<int>(m.buybot_kevlar_helmet.get());
    s.defuser=m.buybot_defuser.get();s.taser=m.buybot_taser.get();s.grenades=m.buybot_grenades.get();
    s.logs=static_cast<unsigned>(static_cast<std::uint64_t>(m.log_types.get()));s.hit_details=static_cast<unsigned>(static_cast<std::uint64_t>(m.hit_log_details.get()));s.buy_targets=static_cast<unsigned>(static_cast<std::uint64_t>(m.buy_log_targets.get()));s.console_logs=m.console_logs.get();
    s.hit_enabled=m.hit_sound_enabled.get();s.kill_enabled=m.kill_sound_enabled.get();s.hit_sound=m.hitsound_select.get().first_set_bit().value_or(0);s.kill_sound=m.kill_sound_select.get().first_set_bit().value_or(0);
    for(int i=0;i<5;++i)s.colors[i]=m.log_colors[i].get().rgba();MiscRuntime::Publish(s);
}
inline void Render(){
    using namespace evo::ren;using namespace evo::gui;
    const auto lines=MiscRuntime::Messages();const float now=static_cast<float>(GetTickCount64())*.001f;
    auto& d=draw.layers[l_top];d->font=draw.fonts[GUI_HASH("gui_main")];float y=12.f;
    for(const auto& message:lines){
        const float age=now-message.time;if(!message.text[0] || age<0.f || age>=5.f)continue;
        const auto tint=color{message.color}.mod_a(std::clamp(5.f-age,0.f,1.f));
        d->add_text({13.f,y+1.f},message.text,color{0,0,0,200}.mod_a(std::clamp(5.f-age,0.f,1.f)));
        d->add_text({12.f,y},message.text,tint);y+=16.f;
    }
}
}
