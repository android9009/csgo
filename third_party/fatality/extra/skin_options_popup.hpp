#pragma once
#include "menu.h"
#include "../../../sdk/skin_schema.hpp"
#include <array>
#include <functional>

namespace SkinOptions {
using namespace evo::gui;
using namespace evo::ren;
struct Choice {
    std::string paint, custom_name;
    int seed{}, stattrak_count{};
    float wear{};
    bool stattrak{}, souvenir{};
    int stattrak_mode{};
    std::array<SkinSchema::Decal,5> stickers{};
};
// Only the inventory image slot is custom. Values, editing, keyboard focus,
// dragging, wheel input and dropdown behavior use the original GUI controls.
class Slot final : public control {
public:
    Slot(control_id id,const vec2& p,std::function<void()> click,
         std::function<void(std::shared_ptr<layer>&,const rect&)> image)
        :control(id,p,{34.f,34.f}),click(std::move(click)),image(std::move(image)) {}
    void render() override {
        control::render();if(!is_visible)return;
        auto& d=draw.layers[ctx->content_layer];const auto r=area_abs();
        d->add_circle(r.center(),16.f,is_mouse_on_me?colors.accent:colors.outline,32,1.f,1.f);
        if(image)image(d,r.shrink(4.f));
    }
    void on_mouse_down(char key) override {if(key==m_left && click)click();}
private:
    std::function<void()> click;
    std::function<void(std::shared_ptr<layer>&,const rect&)> image;
};
class Panel final : public popup {
    struct Values {
        Choice choice;
        value_param<int> seed,count;
        value_param<float> wear;
        value_param<bool> souvenir;
        value_param<bits> mode;
        explicit Values(const Choice& c):choice(c),seed(c.seed),count(c.stattrak_count),wear(c.wear),souvenir(c.souvenir),mode(bits{std::uint64_t{1}<<c.stattrak_mode}){}
    };
public:
    Panel(control_id id,const vec2& anchor,const Choice& choice,bool agent,bool gloves,int slots,
          std::function<void(const Choice&)> changed,std::function<void(int)> pick,
          std::function<void(int,std::shared_ptr<layer>&,const rect&)> image)
        :popup(id,anchor,agent?vec2{92.f,190.f}:gloves?vec2{286.f,116.f}:vec2{304.f,slots?316.f:268.f},true),
         values(std::make_shared<Values>(choice)),agent(agent),gloves(gloves),slots(slots),changed(std::move(changed)),pick(std::move(pick)),image(std::move(image)){}
    void on_first_render_call() override {
        popup::on_first_render_call();
        std::weak_ptr<Panel> self=as<Panel>();
        const auto keep=values;
        auto title=std::make_shared<label>(control_id{id+1},agent?"Patches":"Options",colors.text,false,vec2{10.f,10.f});add(title);
        auto close_button=std::make_shared<button>(control_id{id+2},"X",nullptr,vec2{size.x-26.f,4.f},vec2{22.f,22.f});
        close_button->callback=[self]{if(auto p=self.lock())p->close();};add(close_button);
        auto update=[self,keep]{if(auto p=self.lock())p->publish();};
        if(!agent) {
            auto seed=std::make_shared<slider<int>>(control_id{id+3},0,1000,values->seed,"%d");
            seed->callback=[update](int,int){update();};row("Seed",seed,42.f);
            auto wear=std::make_shared<slider<float>>(control_id{id+4},0.f,1.f,values->wear,"%.3f",.001f);
            wear->callback=[update](float,float){update();};row("Wear",wear,78.f);
            if(!gloves) {
                auto souvenir=std::make_shared<checkbox>(control_id{id+5},values->souvenir);
                souvenir->callback=[update](bool){update();};row("Souvenir",souvenir,114.f);
                auto name=std::make_shared<text_input>(control_id{id+6},vec2{},vec2{154.f,24.f},std::nullopt,std::nullopt,31);
                name->placeholder="Name tag";name->set_value(values->choice.custom_name);
                name->callback=[self,keep](std::string& text){keep->choice.custom_name=text;if(auto p=self.lock())p->publish();};row("Custom name",name,150.f);
                mode=std::make_shared<combo_box>(control_id{id+7},values->mode);
                const char* names[]={"Default","Static","None"};
                for(int i=0;i<3;++i)mode->add(std::make_shared<selectable>(control_id{id+20+static_cast<std::uint64_t>(i)},names[i],i));
                mode->callback=[update](bits&){update();};row("StatTrak",mode,186.f);
                count=std::make_shared<slider<int>>(control_id{id+8},0,99999,values->count,"%d");
                count->callback=[update](int,int){update();};row("Value",count,222.f);
            }
        }
        if(slots) {
            if(!agent)add(std::make_shared<label>(control_id{id+9},"Stickers",colors.text,false,vec2{12.f,278.f}));
            for(int i=0;i<slots;++i) {
                const vec2 location=agent?vec2{29.f,38.f+46.f*static_cast<float>(i)}:vec2{112.f+37.f*static_cast<float>(i),266.f};
                auto slot=std::make_shared<Slot>(control_id{id+40+static_cast<std::uint64_t>(i)},location,
                    [self,i]{if(auto p=self.lock()){if(p->pick)p->pick(i);p->close();}},
                    [self,i](std::shared_ptr<layer>& d,const rect& r){if(auto p=self.lock())if(p->image)p->image(i,d,r);});
                add(slot);
            }
        }
        process_queues();pos=calc_perfect_position(pos);
        ctx->active_popups.emplace_back(as<popup>());ctx->top_sort=sort;
    }
    void render() override {
        if(!is_visible)return;
        if(mode)mode->is_taking_input=!values->souvenir.get();
        if(count)count->is_taking_input=!values->souvenir.get() && values->mode.get().get(1);
        begin_render();auto& d=draw.layers[ctx->content_layer];
        d->add_shadow_rect(area_abs(),8.f,true);
        d->add_rect_filled_rounded(area_abs(),colors.bg_block,3.f);
        d->add_rect_rounded(area_abs(),colors.outline,3.f);
        popup::render();
        if(!agent && !gloves) {
            begin_render();auto& top=draw.layers[ctx->content_layer];
            if(values->souvenir.get())top->add_rect_filled(rect{area_abs().tl()+vec2{8.f,180.f},area_abs().tl()+vec2{size.x-8.f,246.f}},color{0,0,0,135});
            else if(!values->mode.get().get(1))top->add_rect_filled(rect{area_abs().tl()+vec2{8.f,216.f},area_abs().tl()+vec2{size.x-8.f,246.f}},color{0,0,0,135});
            end_render();
        }
    }
private:
    void row(const char* text,const std::shared_ptr<control>& c,float y) {
        add(std::make_shared<label>(control_id{c->id+100},text,colors.text,false,vec2{12.f,y+3.f}));
        c->pos={128.f,y};if(c->type!=ctrl_checkbox)c->size.x=size.x-142.f;c->margin={};add(c);
    }
    void publish() {
        auto& c=values->choice;c.seed=values->seed.get();c.wear=values->wear.get();c.souvenir=values->souvenir.get();
        c.stattrak_count=values->count.get();c.stattrak_mode=values->mode.get().first_set_bit().value_or(0);c.stattrak=c.stattrak_mode==1;
        if(changed)changed(c);
    }
    std::shared_ptr<Values> values;
    bool agent{},gloves{};int slots{};
    std::function<void(const Choice&)> changed;
    std::function<void(int)> pick;
    std::function<void(int,std::shared_ptr<layer>&,const rect&)> image;
    std::shared_ptr<combo_box> mode;
    std::shared_ptr<slider<int>> count;
};
}
