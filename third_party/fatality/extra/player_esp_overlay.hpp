#pragma once
#include "esp_drawing.hpp"
namespace PlayerEspOverlay {
using namespace evo::gui;using namespace evo::ren;
inline void Render(){
    using namespace VisualRuntime;
    static Players frame;CopyPlayers(frame);if(!frame.count)return;
    const auto config=Get();auto& d=draw.layers[l_back];const auto display=draw.display/draw.scale;
    for(int i=0;i<frame.count;++i){
        const auto& p=frame.items[i];const auto& style=config.esp[p.subject];if(!style.show[Enabled])continue;
        const auto& b=p.box;const rect box{vec2{b.left*display.x,b.top*display.y},vec2{b.right*display.x,b.bottom*display.y}};
        EspDrawing::Data data;data.health=p.health;data.armor=p.armor;data.ammo=p.ammo;data.max_ammo=p.max_ammo;data.name=p.name;data.weapon=p.weapon;data.flags=p.flags;
        std::array<VisualPreview::Segment,64> segments{};
        for(int j=0;j<p.segments && j<64;++j)segments[j]={p.skeleton[j].a,p.skeleton[j].b};
        EspDrawing::Render(d,box,style,data,segments,p.segments,display);
    }
}
}
