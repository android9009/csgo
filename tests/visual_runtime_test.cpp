#include "sdk/visual_runtime_state.hpp"
#include <cassert>
int main(){
    using namespace VisualRuntime;
    assert(SubjectFor(2,3,false)==Enemy);assert(SubjectFor(3,2,false)==Enemy);
    assert(SubjectFor(2,2,false)==Team);assert(SubjectFor(3,3,false)==Team);
    assert(SubjectFor(2,2,true)==Local);assert(SubjectFor(3,3,true)==Local);
    Settings config;config.esp[Enemy].show[Enabled]=true;config.esp[Enemy].colors[BoxColor]=0xffff0000;
    config.esp[Local].layout[VisualPreview::Health]={.25f,.5f};Publish(config);
    auto copy=Get();assert(copy.esp[Enemy].show[Enabled]);assert(!copy.esp[Team].show[Enabled]);
    assert(copy.esp[Local].layout[VisualPreview::Health].x==.25f);
    assert(copy.esp[Team].layout[VisualPreview::Health].x==0.f);
    assert(!any_material.load());config.objects[0].enabled=true;config.objects[0].kind=2;Publish(config);assert(any_material.load());
    config.objects[0].enabled=false;config.players[Local].kind=1;Publish(config);assert(!any_material.load());
    config.chams_enabled=true;Publish(config);assert(any_material.load());
}
