#pragma once
#include "../tabs/tabs.hpp"
#include "../config.hpp"
#include "../../sdk/world_esp_state.hpp"
#include <cstdio>
#include "../../utils/log.hpp"
#include <windows.h>
#include "../../sdk/weapon_glyph.hpp"
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
inline std::shared_ptr<font_base> text_font,icon_font;
inline std::shared_ptr<layer> back_layer;
inline void Initialize(){
    if(!text_font){const auto it=draw.fonts.find(GUI_HASH("gui_main"));if(it!=draw.fonts.end())text_font=it->second;}
    if(!icon_font){const auto it=draw.fonts.find(GUI_HASH("weapon_icons"));if(it!=draw.fonts.end())icon_font=it->second;}
    if(!back_layer)back_layer=draw.layers.at(l_back);
}
inline int LogFault(EXCEPTION_POINTERS* info){if(info && info->ExceptionRecord)LOG_ERROR("world esp: overlay exception code=0x%08lX address=%p",info->ExceptionRecord->ExceptionCode,info->ExceptionRecord->ExceptionAddress);return EXCEPTION_EXECUTE_HANDLER;}
inline void DrawItems(const WorldEsp::Options& options,const WorldEsp::Frame& frame,layer* d,const vec2& screen){
    d->font=text_font;
    auto text=[&](vec2 p,const std::string& value,color c) {
        if(!std::isfinite(p.x) || !std::isfinite(p.y))return;
        p.x=std::clamp(p.x,0.f,screen.x);p.y=std::clamp(p.y,0.f,screen.y);
        d->add_text(vec2{(std::min)(p.x+1.f,screen.x),(std::min)(p.y+1.f,screen.y)},value,color{0,0,0,220},text_params::with_h(align_center));
        d->add_text(p,value,c,text_params::with_h(align_center));
    };
    for(size_t i=0;i<(std::min)(frame.count,frame.items.size());++i) {
        const auto& item=frame.items[i];if(item.category<WorldEsp::Weapon || item.category>=WorldEsp::None || !options.enabled[item.category])continue;
        float x{},y{};if(!WorldEsp::Project(frame,item,x,y))continue;
        const vec2 p{std::clamp(x*screen.x,0.f,screen.x),std::clamp(y*screen.y,0.f,screen.y)};const auto c=color{options.colors[item.category]};
        float row=0.f;
        if(options.icons[item.category]) {
            const auto glyph=WeaponGlyph::Get(item.label);
            if(glyph[0] && icon_font){
                d->font=icon_font;text(p,glyph,c);
                d->font=text_font;
            }
            row=18.f;
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
inline bool DrawProtected(const WorldEsp::Options& options,const WorldEsp::Frame& frame,layer* d,const vec2& screen){__try{DrawItems(options,frame,d,screen);return true;}__except(LogFault(GetExceptionInformation())){return false;}}
inline void Render(){
    const auto options=WorldEsp::GetOptions();if(!options.Any())return;
    WorldEsp::Frame frame;WorldEsp::CopyFrame(frame);
    std::uint32_t clock{};float time{};
    if(!WorldEsp::RenderClock(clock,time) || !WorldEsp::Fresh(frame,clock,time,GetTickCount64()) || !WorldEsp::ValidMatrix(frame.matrix))return;
    Initialize();if(!back_layer || !text_font || !std::isfinite(draw.scale) || draw.scale<=0.f)return;
    const auto screen=draw.display/draw.scale;if(!std::isfinite(screen.x) || !std::isfinite(screen.y) || screen.x<=0.f || screen.y<=0.f)return;
    const auto previous_font=back_layer->font;
    const bool ok=DrawProtected(options,frame,back_layer.get(),screen);
    back_layer->font=previous_font; // outside SEH, including a draw fault
    static std::uint64_t next_log{};const auto now=GetTickCount64();
    if(now>=next_log){LOG_INFO("world esp: overlay: draw items=%u ok=%d",static_cast<unsigned>(frame.count),ok);next_log=now+1000;}
}

}
