#include "sdk/visual_preview_state.hpp"
#include <cassert>
#include <limits>
int main(){
    using namespace VisualPreview;
    auto p=ClampPosition({-2.f,100.f},{20.f,20.f},{80.f,80.f});assert(p.x==0.f && p.y==60.f);
    p=ClampPosition({20.f,20.f},{100.f,100.f},{80.f,80.f});assert(p.x==0.f && p.y==0.f);
    SetDock(Name,EspDock::Bottom,2);assert(GetLayout()[Name].side==EspDock::Bottom);
    SetDock(Flags+3,EspDock::Left,0);assert(GetLayout()[Flags+3].side==EspDock::Left);
    ResetDock(Name);assert(GetLayout()[Name].side==EspDock::Top);
    Material a,b;assert(a==b);b.primary=0xffff0000;assert(!(a==b));SetMaterial(b);assert(GetMaterial()==b);
}
