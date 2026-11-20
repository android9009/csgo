#include "sdk/visual_runtime_state.hpp"
#include <cassert>
#include "sdk/esp_projection.hpp"
int main(){
    // Partial off-screen / camera-plane intersections must not drop all ESP.
    std::array<float,16> projection{};projection[0]=1.f;projection[5]=1.f;projection[15]=1.f;
    std::array<EspProjection::Point,8> hull{};
    for(int i=0;i<8;++i)hull[i]={i&1 ? 2.f : -.2f,i&2 ? 2.f : -.2f,i&4 ? 1.f : -1.f};
    EspProjection::Box bounds;
    assert(EspProjection::Bounds(projection,hull,bounds));
    assert(bounds.right==1.f && bounds.top==0.f);
    projection[15]=0.f;projection[14]=1.f;
    assert(EspProjection::Bounds(projection,hull,bounds));
    for(auto& point:hull)point.z=-1.f;
    assert(!EspProjection::Bounds(projection,hull,bounds));
    projection[15]=1.f;projection[14]=0.f;
    for(auto& point:hull)point.x=3.f;
    assert(!EspProjection::Bounds(projection,hull,bounds));
    using namespace VisualRuntime;
    assert(SubjectFor(2,3,false)==Enemy);assert(SubjectFor(3,2,false)==Enemy);
    assert(SubjectFor(2,2,false)==Team);assert(SubjectFor(3,3,false)==Team);
    assert(SubjectFor(2,2,true)==Local);assert(SubjectFor(3,3,true)==Local);
    static_assert(sizeof(Players)>64*1024,"Keep large player snapshots off engine stacks");
    static Players source,destination;
    source.count=1;source.items[0].health=87;PublishPlayers(source);CopyPlayers(destination);
    assert(destination.count==1 && destination.items[0].health==87);
    ClearPlayers();CopyPlayers(destination);assert(destination.count==0);
    Settings config;config.esp[Enemy].show[Enabled]=true;config.esp[Enemy].colors[BoxColor]=0xffff0000;
    config.esp[Local].layout[VisualPreview::Health]={EspDock::Right,2};Publish(config);
    auto copy=Get();assert(copy.esp[Enemy].show[Enabled]);assert(!copy.esp[Team].show[Enabled]);
    assert(copy.esp[Local].layout[VisualPreview::Health].side==EspDock::Right);
    assert(copy.esp[Team].layout[VisualPreview::Health].side==EspDock::Left);
    assert(!any_material.load());config.objects[0].enabled=true;config.objects[0].kind=2;Publish(config);assert(any_material.load());
    config.objects[0].enabled=false;config.players[Local].kind=2;Publish(config);assert(!any_material.load());
    config.chams_enabled=true;Publish(config);assert(any_material.load());
    config.players[Local].kind=0;config.shot.kind=7;config.shot.enabled=true;Publish(config);
    assert(any_material.load() && shot_requested.load());
    config.chams_enabled=false;Publish(config);assert(!shot_requested.load());
    // Visuals -> Misc: the scope/spread style travels with the same publish, and the game
    // thread state the overlay draws from is a separate, copyable value (never a live entity).
    Settings style;assert(style.screen.scope_mode==0 && !style.screen.spread);
    style.screen.scope_mode=2;style.screen.scope_invert=true;style.screen.spread=true;
    style.screen.spread_color1=0x11223344u;Publish(style);
    auto echoed=GetEffects();
    assert(echoed.screen.scope_mode==2 && echoed.screen.scope_invert && echoed.screen.spread);
    assert(echoed.screen.spread_color1==0x11223344u);
    ScreenState state;state.scoped=true;state.inaccuracy=.25f;state.fov=40.f;state.height=1080;
    PublishScreen(state);
    auto copy_state=GetScreen();
    assert(copy_state.scoped && copy_state.inaccuracy>.24f && copy_state.fov==40.f && copy_state.height==1080);
    PublishScreen(ScreenState{});
    assert(!GetScreen().scoped && GetScreen().inaccuracy==0.f);
}
