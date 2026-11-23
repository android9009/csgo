#pragma once
#include "../tabs/tabs.hpp"
#include "../../sdk/skin_schema.hpp"
#include "../../sdk/third_party/gui/src/gui/controls/color_picker.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

namespace SkinOptions {
using namespace evo::gui;
using namespace evo::ren;
// Paint element colors travel as packed 0x00RRGGBB so the menu and the
// preview hook share one representation.
inline std::uint32_t PackColor(color c) {
    auto q = [](float v) { return std::uint32_t(std::clamp(static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)), 0, 255)); };
    return (q(c.value.r) << 16) | (q(c.value.g) << 8) | q(c.value.b);
}
inline color UnpackColor(std::uint32_t v) {
    return color{((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, 1.f};
}
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
        value_param<bool> stattrak;
        explicit Values(const Choice& c):choice(c),seed(c.seed),count(c.stattrak_count),wear(c.wear),souvenir(c.souvenir),stattrak(c.stattrak_mode==1){}
    };
public:
    Panel(control_id id,const vec2& anchor,const Choice& choice,bool agent,bool gloves,int slots,
          std::function<void(const Choice&)> changed,std::function<void(int)> pick,
          std::function<void(int,std::shared_ptr<layer>&,const rect&)> image,
          int color_count=0,const std::array<std::string,8>& color_labels={},
          const std::array<std::uint32_t,8>& color_values={},std::function<void(int,std::uint32_t)> set_color={})
        :popup(id,anchor,agent?vec2{92.f,190.f}:gloves?vec2{286.f,116.f}
             :vec2{304.f,(slots?316.f:268.f)+(color_count>0?44.f+28.f*static_cast<float>(color_count):0.f)},true),
         values(std::make_shared<Values>(choice)),agent(agent),gloves(gloves),slots(slots),changed(std::move(changed)),pick(std::move(pick)),image(std::move(image)),
         color_count(color_count>0?(std::min)(color_count,8):0),color_labels(color_labels),color_values(color_values),set_color(std::move(set_color)){}
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
                souvenir->reset_internal();
                souvenir->callback=[update](bool){update();};row("Souvenir",souvenir,114.f);
                auto name=std::make_shared<text_input>(control_id{id+6},vec2{},vec2{154.f,24.f},std::nullopt,std::nullopt,31);
                name->placeholder="Name tag";name->set_value(values->choice.custom_name);
                name->callback=[self,keep](std::string& text){keep->choice.custom_name=text;if(auto p=self.lock())p->publish();};row("Custom name",name,150.f);
                // Plain on/off switch; the kill counter below sets the static
                // value while it is on. (Old combo: Default/Static/None.)
                stattrak_check=std::make_shared<checkbox>(control_id{id+7},values->stattrak);
                stattrak_check->reset_internal();
                stattrak_check->callback=[update](bool){update();};row("StatTrak",stattrak_check,186.f);
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
        // Paint kit element colors (only for paintable skins): one swatch per
        // recolorable model part, below the stickers.
        if(color_count>0) {
            const float top=(slots?316.f:268.f)+12.f;
            add(std::make_shared<label>(control_id{id+50},"Paint colors",colors.text,false,vec2{12.f,top}));
            for(int i=0;i<color_count;++i) {
                const float y=top+30.f+28.f*static_cast<float>(i);
                auto row=std::make_shared<control_container>(control_id{id+60+static_cast<std::uint64_t>(i)},vec2{12.f,y},vec2{size.x-24.f,24.f});
                auto text=color_labels[i].empty()?("Element "+std::to_string(i+1)):color_labels[i];
                if(text.size()>18) text=text.substr(0,18)+"...";
                row->add(std::make_shared<label>(control_id{id+70+static_cast<std::uint64_t>(i)},text,colors.text_dark,false,vec2{0.f,4.f}));
                auto keep_color=std::make_shared<value_param<color>>(UnpackColor(color_values[i]));
                auto picker=std::make_shared<color_picker>(control_id{id+80+static_cast<std::uint64_t>(i)},*keep_color,false);
                picker->pos={116.f,3.f};picker->size={48.f,16.f};picker->margin={};
                picker->callback=[self,i](color c){
                    if(auto p=self.lock()){const auto packed=PackColor(c);p->color_values[i]=packed;if(p->set_color)p->set_color(i,packed);}
                };
                row->add(picker);add(row);
            }
        }
        // This popup builds children inside its own first-render callback,
        // AFTER context::render already raised child initialization. Initialize
        // the newly queued subtree now, before label::render dereferences fnt.
        process_queues();raise_first_render_call();pos=calc_perfect_position(pos);
        ctx->active_popups.emplace_back(as<popup>());ctx->top_sort=sort;
    }
    void render() override {
        if(!is_visible)return;
        if(stattrak_check)stattrak_check->is_taking_input=!values->souvenir.get();
        if(count)count->is_taking_input=!values->souvenir.get() && values->stattrak.get();
        begin_render();auto& d=draw.layers[ctx->content_layer];
        d->add_shadow_rect(area_abs(),8.f,true);
        d->add_rect_filled_rounded(area_abs(),colors.bg_block,3.f);
        d->add_rect_rounded(area_abs(),colors.outline,3.f);
        popup::render();
        if(!agent && !gloves) {
            begin_render();auto& top=draw.layers[ctx->content_layer];
            if(values->souvenir.get())top->add_rect_filled(rect{area_abs().tl()+vec2{8.f,180.f},area_abs().tl()+vec2{size.x-8.f,246.f}},color{0,0,0,135});
            else if(!values->stattrak.get())top->add_rect_filled(rect{area_abs().tl()+vec2{8.f,216.f},area_abs().tl()+vec2{size.x-8.f,246.f}},color{0,0,0,135});
            end_render();
        }
    }
private:
    void row(const char* text,const std::shared_ptr<control>& c,float y) {
        auto row=std::make_shared<control_container>(control_id{c->id+200},vec2{12.f,y},vec2{size.x-24.f,26.f});
        row->add(std::make_shared<label>(control_id{c->id+100},text,colors.text,false,vec2{0.f,3.f}));
        c->pos={116.f,0.f};if(c->type!=ctrl_checkbox)c->size.x=size.x-142.f;c->margin={};row->add(c);add(row);
    }
    void publish() {
        auto& c=values->choice;c.seed=values->seed.get();c.wear=values->wear.get();c.souvenir=values->souvenir.get();
        // Checkbox: on = static StatTrak with the counter below, off = no StatTrak.
        c.stattrak_count=values->count.get();c.stattrak_mode=values->stattrak.get()?1:2;c.stattrak=c.stattrak_mode==1;
        if(changed)changed(c);
    }
    std::shared_ptr<Values> values;
    bool agent{},gloves{};int slots{};
    std::function<void(const Choice&)> changed;
    std::function<void(int)> pick;
    std::function<void(int,std::shared_ptr<layer>&,const rect&)> image;
    int color_count{};
    std::array<std::string,8> color_labels{};
    std::array<std::uint32_t,8> color_values{};
    std::function<void(int,std::uint32_t)> set_color;
    std::shared_ptr<checkbox> stattrak_check;
    std::shared_ptr<slider<int>> count;
};
class CatalogPanel final : public popup {
public:
    CatalogPanel(control_id id,const vec2& anchor,const std::array<bool,4>& initial,std::function<void(int,bool)> changed)
        :popup(id,anchor,{236.f,156.f},true),changed(std::move(changed)) {
        for(size_t i=0;i<4;++i)values[i]=std::make_shared<value_param<bool>>(initial[i]);
    }
    void on_first_render_call() override {
        popup::on_first_render_call();
        std::weak_ptr<CatalogPanel> self=as<CatalogPanel>();
        add(std::make_shared<label>(control_id{id+1},"Weapons",colors.text,false,vec2{10.f,10.f}));
        auto close_button=std::make_shared<button>(control_id{id+2},"X",nullptr,vec2{210.f,4.f},vec2{22.f,22.f});
        close_button->callback=[self]{if(auto p=self.lock())p->close();};add(close_button);
        const char* names[]={"Show all skins","Show all weapons","Share to inventory","Allow weapon skins"};
        for(int i=0;i<4;++i) {
            auto row=std::make_shared<control_container>(control_id{id+10+static_cast<std::uint64_t>(i)},vec2{12.f,38.f+28.f*static_cast<float>(i)},vec2{212.f,24.f});
            const auto keep=values[i];
            auto check=std::make_shared<checkbox>(control_id{id+20+static_cast<std::uint64_t>(i)},*keep);
            check->pos={194.f,0.f};check->margin={};
            // Native checkbox animation starts unchecked, independently of value.
            // Restore the visual state without firing a settings callback.
            check->reset_internal();
            check->callback=[self,keep,i](bool value){if(auto p=self.lock())if(p->changed)p->changed(i,value);};
            row->add(std::make_shared<label>(control_id{id+30+static_cast<std::uint64_t>(i)},names[i]));
            row->add(check);add(row);
        }
        process_queues();raise_first_render_call();pos=calc_perfect_position(pos);
        ctx->active_popups.emplace_back(as<popup>());ctx->top_sort=sort;
    }
    void render() override {
        if(!is_visible)return;
        begin_render();auto& d=draw.layers[ctx->content_layer];
        d->add_shadow_rect(area_abs(),8.f,true);
        d->add_rect_filled_rounded(area_abs(),colors.bg_block,3.f);
        d->add_rect_rounded(area_abs(),colors.outline,3.f);
        popup::render();
    }
private:
    std::array<std::shared_ptr<value_param<bool>>,4> values;
    std::function<void(int,bool)> changed;
};

}
