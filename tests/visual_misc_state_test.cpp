#include "../features/visuals/misc_state.hpp"
#include <cassert>
int main(){
    using namespace VisualMisc;
    assert(ZoomFov(40,100,0)==40 && ZoomFov(40,100,100)==100 && ZoomFov(40,100,50)==70);
    assert(Smooth(0,100,.016f)>0 && Smooth(0,100,.016f)<100);
    assert(Smooth(100,0,.016f)>0 && Smooth(100,0,.016f)<100);
    assert(Smooth(0,100,0)==0);
    assert(std::abs(Smooth(Smooth(0,100,.016f),100,.016f)-Smooth(0,100,.032f))<.0001f);
    assert(Smooth(0,100,-1.f)==0);
    assert(ZoomFov(40,100,-100)==40 && ZoomFov(40,100,200)==100);
    auto v=ViewmodelOffset({0,0,0},{1,2,3});assert(v.x==2 && v.y==-1 && v.z==3);
    v=Forward({0,90,0});assert(std::abs(v.x)<.0001f && v.y>.999f);
    Settings s;s.third=true;s.fov=120;Publish(s);assert(Get().third && Get().fov==120);
    ClearEvents();
    for(int i=0;i<300;++i)AddEvent({{},{},static_cast<float>(i),0,0});
    EventFrame events;assert(CopyEvents(events) && events.count==256 && events.records[0].time==44.f);
    TickEvents(300.f);assert(CopyEvents(events) && events.count==10);
    TickEvents(1.f);assert(CopyEvents(events) && events.count==0);
    AddEvent({{},{},1.f,1,0});ClearEvents();assert(CopyEvents(events) && events.count==0);
    s.hitmarkers=1u<<3;s.hit_duration[3]=10.f;s.weather=1u<<3;s.weather_colors[3]=0xff123456;Publish(s);
    assert(Get().hit_duration[3]==10.f && Get().weather_colors[3]==0xff123456);
    Trajectory path;path.count=2;path.points[1]={1,2,3};path.bounces[1]=true;path.detonated=true;PublishTrajectory(path);
    Trajectory path_copy;assert(CopyTrajectory(path_copy) && path_copy.count==2 && path_copy.bounces[1] && path_copy.detonated);
    PublishTrajectory({});assert(CopyTrajectory(path_copy) && path_copy.count==0);
    ArrowFrame arrows;arrows.points[63].alpha=.5f;PublishArrows(arrows);ArrowFrame arrow_copy;assert(CopyArrows(arrow_copy) && arrow_copy.points[63].alpha==.5f);
    WarningFrame warnings;warnings.count=1;warnings.records[0].remaining=1.2f;PublishWarnings(warnings);WarningFrame warning_copy;assert(CopyWarnings(warning_copy) && warning_copy.count==1 && warning_copy.records[0].remaining==1.2f);
    PublishArrows({});PublishWarnings({});
    assert(CopyArrows(arrow_copy) && arrow_copy.points[63].alpha==0.f);
    assert(CopyWarnings(warning_copy) && warning_copy.count==0);
    LocalFrame f;f.alive=true;f.spread=.05f;PublishLocal(f);assert(GetLocal().spread==.05f);
}
