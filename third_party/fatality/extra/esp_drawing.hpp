#pragma once
#include "menu.h"
#include "../../../sdk/visual_runtime_state.hpp"
#include "../../../sdk/weapon_glyph.hpp"
namespace EspDrawing {
using namespace evo::gui;using namespace evo::ren;
struct Data {
    int health{87},armor{100},ammo{21},max_ammo{30};
    const char* name{"Player"};const char* weapon{"ak47"};
    std::uint64_t flags{0x7ff};
};
inline std::array<rect,EspDock::Count> Render(std::shared_ptr<layer>& d,const rect& box,const VisualRuntime::Esp& style,const Data& data,
    const std::array<VisualPreview::Segment,64>& skeleton={},int segments=0,const vec2& skeleton_scale={1.f,1.f},const vec2& skeleton_origin={}) {
    using namespace VisualRuntime;
    std::array<EspDock::Item,EspDock::Count> items{};std::array<std::string,EspDock::Count> labels{};
    const auto text_font=draw.fonts[GUI_HASH("gui_main")],icon_font=draw.fonts[GUI_HASH("weapon_icons")];
    labels[0]=data.name;labels[4]=data.weapon;labels[5]=WeaponGlyph::Get(data.weapon);
    static const char* flag_names[]={"SCOPED","BOMB","LC","FD","CHARGE","HK","HIT","$16000","KIT","ZEUS","IMMUNE"};
    for(int f=0;f<11;++f)labels[6+f]=flag_names[f];
    items[0].visible=style.show[Name];items[4].visible=style.show[Weapon];items[5].visible=style.show[Icon] && !labels[5].empty();
    for(int id=1;id<=3;++id)items[id]={style.show[id==1 ? Health : id==2 ? Armor : Ammo],true,4.f,4.f};
    for(int f=0;f<11;++f)items[6+f].visible=(style.flags&data.flags&(1ull<<f))!=0;
    for(int i=0;i<EspDock::Count;++i)if(!items[i].bar){const auto size=(i==5 ? icon_font : text_font)->get_text_size(labels[i]);items[i].width=size.x;items[i].height=size.y;}
    const auto arranged=EspDock::Arrange({box.tl().x,box.tl().y,box.width(),box.height()},items,style.layout);
    std::array<rect,EspDock::Count> hit{};
    auto color_for=[&](int id){return color{style.colors[id]};};
    if(style.show[Box]){d->add_rect({box.tl()-vec2{1.f,1.f},box.br()+vec2{1.f,1.f}},color{0,0,0,210});d->add_rect(box,color_for(BoxColor));}
    for(int id=0;id<EspDock::Count;++id){if(!items[id].visible)continue;const auto& a=arranged[id];const rect r{a.x,a.y,a.x+a.w,a.y+a.h};hit[id]=r;
        if(items[id].bar){
            const int tint=id==1 ? HealthColor : id==2 ? ArmorColor : AmmoColor;
            const float ratio=std::clamp(id==1 ? static_cast<float>(data.health)/100.f : id==2 ? static_cast<float>(data.armor)/100.f : data.max_ammo>0 ? static_cast<float>(data.ammo)/data.max_ammo : 0.f,0.f,1.f);
            const bool vertical=style.layout[id].side==EspDock::Left || style.layout[id].side==EspDock::Right;
            d->add_rect_filled(r,color{0,0,0,220});
            const auto fill=vertical ? rect{r.tl()+vec2{1.f,1.f+(r.height()-2.f)*(1.f-ratio)},r.br()-vec2{1.f,1.f}} : rect{r.tl()+vec2{1.f,1.f},r.tl()+vec2{1.f+(r.width()-2.f)*ratio,r.height()-1.f}};
            const auto start=color_for(tint),end=color{style.gradient_end[id-1]};
            if(style.gradient[id-1])d->add_rect_filled_multicolor(fill,vertical ? std::array<color,4>{end,end,start,start} : std::array<color,4>{start,end,end,start});
            else d->add_rect_filled(fill,start);
        }else{
            d->font=id==5 ? icon_font : text_font;
            const auto c=id==0 ? color_for(NameColor) : id==4 ? color_for(WeaponColor) : id==5 ? color_for(IconColor) : color::white();
            d->add_text(r.tl()+vec2{1.f,1.f},labels[id],color{0,0,0,210});d->add_text(r.tl(),labels[id],c);
        }
    }
    if(style.show[Skeleton])for(int i=0;i<segments && i<64;++i)d->add_line(skeleton_origin+vec2{skeleton[i].a.x*skeleton_scale.x,skeleton[i].a.y*skeleton_scale.y},skeleton_origin+vec2{skeleton[i].b.x*skeleton_scale.x,skeleton[i].b.y*skeleton_scale.y},color_for(SkeletonColor));
    d->font=text_font;return hit;
}
}
