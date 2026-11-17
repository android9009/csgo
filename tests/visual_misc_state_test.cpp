#include "../features/visuals/misc_state.hpp"
#include <cassert>
int main(){
    using namespace VisualMisc;
    assert(ZoomFov(40,100,0)==40 && ZoomFov(40,100,100)==100 && ZoomFov(40,100,50)==70);
    assert(Smooth(0,100,.016f)>0 && Smooth(0,100,.016f)<100);
    assert(Smooth(100,0,.016f)>0 && Smooth(100,0,.016f)<100);
    assert(Smooth(0,100,0)==0);
    auto v=ViewmodelOffset({0,0,0},{1,2,3});assert(v.x==2 && v.y==-1 && v.z==3);
    v=Forward({0,90,0});assert(std::abs(v.x)<.0001f && v.y>.999f);
    Settings s;s.third=true;s.fov=120;Publish(s);assert(Get().third && Get().fov==120);
    LocalFrame f;f.alive=true;f.spread=.05f;PublishLocal(f);assert(GetLocal().spread==.05f);
}
