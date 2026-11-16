#pragma once
#include "menu.h"
#include "../../../sdk/visual_runtime_state.hpp"
namespace PlayerEspOverlay {
using namespace evo::gui;
using namespace evo::ren;
inline void Render() {
    using namespace VisualRuntime;
    static Players frame;CopyPlayers(frame);if(!frame.count)return;
    const auto config=Get();auto& d=draw.layers[l_back];d->font=draw.fonts[GUI_HASH("gui_main")];
    const auto display=draw.display/draw.scale;
    for(int i=0;i<frame.count;++i) {
        const auto& p=frame.items[i];const auto& s=config.esp[p.subject];if(!s.show[Enabled])continue;
        const auto& b=p.box;const rect box{vec2{b.left*display.x,b.top*display.y},vec2{b.right*display.x,b.bottom*display.y}};
        auto tint=[&](int id){return color{s.colors[id]};};
        auto position=[&](int id,vec2 initial){const auto offset=s.layout[id];return initial+vec2{offset.x*box.width(),offset.y*box.height()};};
        auto text=[&](int id,const std::string& label,vec2 initial,color c) {
            const auto point=position(id,initial);d->add_text(point+vec2{1.f,1.f},label,color{0,0,0,220});d->add_text(point,label,c);
        };
        auto bar=[&](int id,vec2 initial,vec2 size,float ratio,color c,color end,bool gradient=false) {
            ratio=std::clamp(ratio,0.f,1.f);const auto point=position(id,initial);d->add_rect_filled({point,point+size},color{0,0,0,210});
            const bool vertical=size.y>size.x;
            const rect fill=vertical ? rect{point+vec2{1.f,1.f+(size.y-2.f)*(1.f-ratio)},point+size-vec2{1.f,1.f}} : rect{point+vec2{1.f,1.f},point+vec2{1.f+(size.x-2.f)*ratio,size.y-1.f}};
            if(gradient)d->add_rect_filled_multicolor(fill,{end,end,c,c});else d->add_rect_filled(fill,c);
        };
        if(s.show[Box]){d->add_rect({box.tl()-vec2{1.f,1.f},box.br()+vec2{1.f,1.f}},color{0,0,0,200});d->add_rect(box,tint(BoxColor));}
        if(s.show[Name])text(VisualPreview::Name,p.name,{box.center().x-d->font->get_text_size(p.name).x*.5f,box.tl().y-14.f},tint(NameColor));
        if(s.show[Health])bar(VisualPreview::Health,box.tl()-vec2{7.f,0.f},{5.f,box.height()},float(p.health)/100.f,tint(HealthColor),tint(HealthEnd),s.colors[HealthColor]!=s.colors[HealthEnd]);
        if(s.show[Armor])bar(VisualPreview::Armor,box.bl()+vec2{0.f,3.f},{box.width(),5.f},float(p.armor)/100.f,tint(ArmorColor),tint(ArmorColor));
        if(s.show[Ammo] && p.max_ammo>0 && p.ammo>=0)bar(VisualPreview::Ammo,box.bl()+vec2{0.f,10.f},{box.width(),5.f},float(p.ammo)/float(p.max_ammo),tint(AmmoColor),tint(AmmoColor));
        if(s.show[Weapon])text(VisualPreview::Weapon,p.weapon,{box.center().x-d->font->get_text_size(p.weapon).x*.5f,box.br().y+20.f},tint(WeaponColor));
        if(s.show[Icon] && p.weapon[0]) {
            const auto pos=position(VisualPreview::Icon,{box.center().x-17.f,box.br().y+36.f});const auto c=tint(IconColor);
            d->add_rect_filled({pos+vec2{6.f,2.f},pos+vec2{26.f,6.f}},c);d->add_line(pos+vec2{26.f,3.f},pos+vec2{34.f,3.f},c,2.f);
            d->add_line(pos+vec2{7.f,4.f},pos+vec2{0.f,8.f},c,3.f);d->add_line(pos+vec2{16.f,6.f},pos+vec2{19.f,11.f},c,3.f);
        }
        if(s.show[Skeleton])for(int j=0;j<p.segments;++j)d->add_line({p.skeleton[j].a.x*display.x,p.skeleton[j].a.y*display.y},{p.skeleton[j].b.x*display.x,p.skeleton[j].b.y*display.y},tint(SkeletonColor));
        static const char* names[]={"SCOPED","BOMB","LC","FD","CHARGE","ARMOR","HIT","MONEY","KIT","ZEUS","IMMUNE"};
        std::string flags;for(int f=0;f<11;++f)if((p.flags&s.flags)&(1ull<<f)){if(!flags.empty())flags+='\n';flags+=names[f];}
        if(!flags.empty())text(VisualPreview::Flags,flags,box.tr()+vec2{4.f,0.f},color::white());
    }
}
}
