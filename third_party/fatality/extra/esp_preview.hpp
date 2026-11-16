#pragma once
#include "menu.h"
#include "../cfg.h"
#include "../../../sdk/visual_preview_state.hpp"
#include <array>

namespace EspPreview {
using namespace evo::gui;
using namespace evo::ren;
// Deterministic sample data: editing ESP must also work outside a match.
// Only screen-space layout is draggable; the model box stays attached to it.
class Overlay {
public:
    void render(std::shared_ptr<layer>& d,const rect& viewport) {
        hits.fill({});present.fill(false);
        if(!cfg.visuals.esp.enabled.get())return;
        const auto geometry=VisualPreview::GetGeometry();
        if(!geometry.valid)return;
        const vec2 extent{viewport.width(),viewport.height()};
        const auto& b=geometry.box;
        const rect box{viewport.tl()+vec2{b.left*extent.x,b.top*extent.y},viewport.tl()+vec2{b.right*extent.x,b.bottom*extent.y}};
        layout_extent={box.width(),box.height()};
        const auto clip=d->g.clip_rect;d->override_clip_rect(viewport);
        d->font=draw.fonts[GUI_HASH("gui_main")];
        const auto offsets=VisualPreview::GetLayout();
        auto position=[&](int id,vec2 origin,vec2 size) {
            const auto p=VisualPreview::ClampPosition({origin.x-viewport.tl().x+offsets[id].x*box.width(),origin.y-viewport.tl().y+offsets[id].y*box.height()},
                {size.x,size.y},{extent.x,extent.y});
            const auto top=viewport.tl()+vec2{p.x,p.y};
            hits[id]={top,top+size};present[id]=true;
            return top;
        };
        auto text=[&](int id,const std::string& value,vec2 origin,color c) {
            const auto size=d->font->get_text_size(value);
            const auto p=position(id,origin,size);
            d->add_text(p+vec2{1.f,1.f},value,color{0,0,0,210});d->add_text(p,value,c);
        };
        auto bar=[&](int id,vec2 origin,vec2 size,float fraction,color c,color end,bool gradient=false) {
            const auto p=position(id,origin,size);
            const rect r{p,p+size};d->add_rect_filled(r,color{0,0,0,210});
            const bool vertical=size.y>size.x;
            const rect fill=vertical ? rect{p+vec2{1.f,1.f+(size.y-2.f)*(1.f-fraction)},p+size-vec2{1.f,1.f}} :
                rect{p+vec2{1.f,1.f},p+vec2{1.f+(size.x-2.f)*fraction,size.y-1.f}};
            if(gradient)d->add_rect_filled_multicolor(fill,{end,end,c,c});else d->add_rect_filled(fill,c);
        };
        auto& e=cfg.visuals.esp;
        if(e.box.get()) {
            d->add_rect(rect{box.tl()-vec2{1.f,1.f},box.br()+vec2{1.f,1.f}},color{0,0,0,210});
            d->add_rect(box,e.box_color.get());
        }
        if(e.name.get())text(VisualPreview::Name,"Player",{box.center().x-18.f,box.tl().y-16.f},e.name_color.get());
        if(e.health.get())bar(VisualPreview::Health,box.tl()-vec2{8.f,0.f},{5.f,box.height()},.87f,e.health_color_1.get(),e.health_color_2.get(),e.health_color_1.get()!=e.health_color_2.get());
        if(e.armor.get())bar(VisualPreview::Armor,box.bl()+vec2{0.f,4.f},{box.width(),5.f},1.f,e.armor_color.get(),e.armor_color.get());
        if(e.ammo.get())bar(VisualPreview::Ammo,box.bl()+vec2{0.f,12.f},{box.width(),5.f},.7f,e.ammo_color.get(),e.ammo_color.get());
        if(e.weapon.get())text(VisualPreview::Weapon,"AK-47",{box.center().x-18.f,box.br().y+22.f},e.weapon_color.get());
        if(e.weapon_icon.get()) {
            // Vector silhouette; no unsupported icon-font codepoints.
            const auto p=position(VisualPreview::Icon,{box.center().x-17.f,box.br().y+39.f},{34.f,12.f});
            const auto c=e.weapon_icon_color.get();
            d->add_rect_filled({p+vec2{6.f,2.f},p+vec2{26.f,6.f}},c);
            d->add_line(p+vec2{26.f,3.f},p+vec2{34.f,3.f},c,2.f);
            d->add_line(p+vec2{7.f,4.f},p+vec2{0.f,8.f},c,3.f);
            d->add_line(p+vec2{16.f,6.f},p+vec2{19.f,11.f},c,3.f);
        }
        std::string flags;
        static const char* names[]={"SCOPED","BOMB","LC","FD","CHARGE","HK","HIT","$16000","KIT","ZEUS","IMMUNE"};
        for(int i=0;i<cfg_t::esp_flag_max;++i)if(e.flags->test(i)){if(!flags.empty())flags+='\n';flags+=names[i];}
        if(!flags.empty())text(VisualPreview::Flags,flags,box.tr()+vec2{5.f,0.f},color::white());
        // A static standing-pose guide. It is not a live hitbox/bone cache.
        if(e.skeleton.get() || e.history_skeleton.get()) {
            const auto c=e.skeleton.get()?e.skeleton_color.get():e.history_skeleton_color.get();
            const auto p=[&](float x,float y){return box.tl()+vec2{x*box.width(),y*box.height()};};
            for(const auto& line:std::array<std::array<float,4>,9>{{{.5f,.12f,.5f,.52f},{.28f,.23f,.72f,.23f},{.28f,.23f,.18f,.48f},{.18f,.48f,.15f,.6f},{.72f,.23f,.82f,.48f},{.82f,.48f,.85f,.6f},{.5f,.52f,.34f,.74f},{.34f,.74f,.32f,.96f},{.5f,.52f,.68f,.96f}}})
                d->add_line(p(line[0],line[1]),p(line[2],line[3]),c);
        }
        if(e.sound_esp.get())d->add_circle(box.bl()+vec2{box.width()*.5f,0.f},box.width()*.55f,e.sound_esp_color.get(),48);
        for(int i=0;i<VisualPreview::ElementCount;++i)if(present[i] && (dragging==i || hits[i].contains(input.cursor())))d->add_rect(hits[i],colors.accent.mod_a(.65f));
        d->add_text(viewport.bl()-vec2{0.f,13.f},"Sample ESP - drag / RMB reset",colors.text_dark);
        d->g.clip_rect=clip;
    }
    bool down(char key) {
        for(int i=VisualPreview::ElementCount-1;i>=0;--i)if(present[i] && hits[i].contains(input.cursor())) {
            if(key==m_right){VisualPreview::SetOffset(i,{});return true;}
            if(key==m_left){dragging=i;return true;}
        }
        return false;
    }
    bool move(const vec2& delta,const vec2&) {
        if(dragging<0)return false;
        if(layout_extent.x<=0.f || layout_extent.y<=0.f)return true;
        auto p=VisualPreview::GetLayout()[dragging];p.x+=delta.x/layout_extent.x;p.y+=delta.y/layout_extent.y;
        VisualPreview::SetOffset(dragging,p);return true;
    }
    void up(){dragging=-1;}
private:
    std::array<rect,VisualPreview::ElementCount> hits{};
    std::array<bool,VisualPreview::ElementCount> present{};
    int dragging{-1};
    vec2 layout_extent{};
};
}
