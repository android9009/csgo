#pragma once
#include "menu.h"
#include "../cfg.h"
#include "../../../sdk/world_esp_state.hpp"
#include <cstdio>
namespace WorldEspOverlay {
using namespace evo::gui;
using namespace evo::ren;
inline void Configure() {
    auto& w=cfg.visuals.esp.world;
    WorldEsp::Options o;
    o.enabled={w.weapon_enabled.get(),w.grenade_enabled.get(),w.c4_enabled.get()};
    o.icons={w.weapon_icons.get(),w.grenade_icons.get(),w.c4_options->test(3)};
    o.colors={w.weapon_color.get().rgba(),w.grenade_color.get().rgba(),w.c4_color.get().rgba()};
    o.timer_color=w.c4_timer_color.get().rgba();o.timer=w.c4_options->test(0);o.damage=w.c4_options->test(1);o.defuse=w.c4_options->test(2);
    WorldEsp::SetOptions(o);
}
inline void Render() {
    const auto options=WorldEsp::GetOptions();if(!options.Any())return;
    const auto frame=WorldEsp::GetFrame();if(!frame.valid)return;
    auto& d=draw.layers[l_back];d->font=draw.fonts[GUI_HASH("gui_main")];
    const auto screen=draw.display/draw.scale;
    auto text=[&](vec2 p,const std::string& value,color c) {
        d->add_text(p+vec2{1.f,1.f},value,color{0,0,0,220},text_params::with_h(align_center));
        d->add_text(p,value,c,text_params::with_h(align_center));
    };
    for(size_t i=0;i<frame.count;++i) {
        const auto& item=frame.items[i];if(item.category==WorldEsp::None || !options.enabled[item.category])continue;
        float x{},y{};if(!WorldEsp::Project(frame,item,x,y))continue;
        const vec2 p{x*screen.x,y*screen.y};const auto c=color{options.colors[item.category]};
        float row=0.f;
        if(options.icons[item.category]) {
            // Lightweight vector markers remain readable without font fallbacks.
            if(item.category==WorldEsp::Weapon) {
                d->add_line(p-vec2{12.f,4.f},p+vec2{12.f,-4.f},c,3.f);
                d->add_line(p,p+vec2{3.f,6.f},c,3.f);
            } else if(item.category==WorldEsp::Grenade) {
                d->add_circle(p,5.f,c,16);d->add_line(p-vec2{2.f,6.f},p+vec2{3.f,-6.f},c,2.f);
            } else d->add_rect({p-vec2{8.f,5.f},p+vec2{8.f,5.f}},c);
            row=9.f;
        }
        if(item.category!=WorldEsp::C4 || !item.planted || options.icons[WorldEsp::C4]) {
            text(p+vec2{0.f,row},item.label,c);row+=13.f;
        }
        if(item.category!=WorldEsp::C4 || !item.planted)continue;
        char line[64]{};const auto timer_color=color{options.timer_color};
        if(options.timer){std::snprintf(line,sizeof(line),"C4 %.1fs",(std::max)(0.f,item.explode_at-frame.time));text(p+vec2{0.f,row},line,timer_color);row+=13.f;}
        if(options.damage){std::snprintf(line,sizeof(line),"~%d damage",item.estimated_damage);text(p+vec2{0.f,row},line,timer_color);row+=13.f;}
        if(options.defuse && item.defusing){std::snprintf(line,sizeof(line),"Defusing %.1fs",(std::max)(0.f,item.defuse_at-frame.time));text(p+vec2{0.f,row},line,item.defuse_at<item.explode_at ? color{80,220,120} : color{235,80,80});}
    }
}
}
