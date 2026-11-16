#include "sdk/esp_docking.hpp"
#include "sdk/weapon_glyph.hpp"
#include <cassert>
#include <cstring>
int main(){
    auto layout=EspDock::Defaults();
    std::array<EspDock::Item,EspDock::Count> items{};
    items[1]={true,true,4,4};items[2]={true,true,4,4};items[3]={true,true,4,4};
    for(int i=0;i<11;++i)items[6+i]={true,false,45,12};
    const EspDock::Rect box{80,60,90,240};auto r=EspDock::Arrange(box,items,layout);
    assert(r[1].h==240 && r[1].w==4);assert(r[2].w==90 && r[2].h==4);assert(r[3].y>=r[2].y+r[2].h);
    EspDock::Move(layout,1,EspDock::Right,0);r=EspDock::Arrange(box,items,layout);
    assert(r[6].x>=r[1].x+r[1].w);assert(r[7].y>=r[6].y+r[6].h);
    EspDock::Move(layout,1,EspDock::Top,0);r=EspDock::Arrange(box,items,layout);assert(r[1].w==box.w && r[1].h==4);
    EspDock::Move(layout,6,EspDock::Bottom,0);r=EspDock::Arrange(box,items,layout);
    assert(r[6].w==45 && r[6].h==12);assert(layout[7].side==EspDock::Right);
    assert(EspDock::Nearest(10,180,box)==EspDock::Left);assert(EspDock::Nearest(120,310,box)==EspDock::Bottom);
    assert(!std::strcmp(WeaponGlyph::Get("weapon_ak47"),"W"));assert(!std::strcmp(WeaponGlyph::Get("SmokeGrenadeProjectile"),"k"));
    assert(!std::strcmp(WeaponGlyph::Get("DEagle"),"A"));
    assert(!std::strcmp(WeaponGlyph::Get("M4A1"),"S"));
    for(int i=0;i<1000;++i)EspDock::Move(layout,6,EspDock::Right,0);
    for(auto item:layout)assert(item.order<EspDock::Count);
}
