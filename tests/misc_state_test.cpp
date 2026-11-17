#include "../features/misc/state.hpp"
#include <cassert>
#include <limits>
#include <thread>
int main(){
    MiscRuntime::Settings movement_off;assert(!MiscRuntime::MovementEnabled(movement_off));movement_off.edge_jump=true;assert(MiscRuntime::MovementEnabled(movement_off));

    using namespace MiscRuntime;
    Settings s;s.bhop=true;s.instant_duck=true;
    CUserCmd c;c.buttons=2;
    Move(c,s,false,2,0,0,0);assert(!(c.buttons&2));assert(c.buttons&(1<<22));
    c.buttons=2;Move(c,s,true,2,0,0,0);assert(c.buttons&2);
    c.buttons=2;Move(c,s,false,2,0,0,0,true);assert(c.buttons&2);
    for(int type:{0,8,9,10}){c.buttons=2;Move(c,s,false,type,0,0,0);assert(c.buttons==2);}
    c.buttons=2;Move(c,s,false,2,2,0,0);assert(c.buttons==2);
    c.buttons=2;Move(c,s,false,2,0,std::numeric_limits<float>::quiet_NaN(),0);assert(c.buttons==2);
    s.quick_stop=true;c={};Move(c,s,true,2,0,100,0);assert(c.forwardmove<0 && c.sidemove==0);
    c={};c.buttons=1<<3;Move(c,s,true,2,0,100,0);assert(c.forwardmove==0);
    s.strafe=1;c={};c.buttons=2;Move(c,s,false,2,0,250,0);assert(std::isfinite(c.forwardmove) && std::abs(c.sidemove)>400);
    c={};Move(c,s,false,2,0,250,0);assert(c.forwardmove==0 && c.sidemove==0);
    s.strafe=9;Move(c,s,false,2,0,250,0);assert(std::abs(c.sidemove)>400);
    s={};assert(BuyCommands(s,3).empty());s.primary=1;s.defuser=true;
    assert(BuyCommands(s,3)=="buy m4a1;buy defuser;");assert(BuyCommands(s,2)=="buy ak47;");assert(BuyCommands(s,1).empty());
    s.primary=2;assert(BuyCommands(s,3)=="buy aug;buy defuser;");
    s={};s.grenades=true;assert(BuyCommands(s,2).find("molotov")!=std::string::npos);assert(BuyCommands(s,3).find("incgrenade")!=std::string::npos);
    s.primary=6;Publish(s);assert(Get().primary==6);
    Physics p;c={};CounterMove(c,200,0,p);assert(c.forwardmove< -449.f && c.sidemove==0.f);
    c={};c.viewangles.y=90;CounterMove(c,200,0,p);assert(c.sidemove< -449.f && std::abs(c.forwardmove)<.001f);
    c={};CounterMove(c,10,0,p);assert(c.forwardmove< -1.f && c.forwardmove> -250.f);
    c={};EdgeJump(c,true,true,false);assert(c.buttons&2);
    c={};EdgeJump(c,true,false,false);assert(c.buttons==0);EdgeJump(c,false,true,false);assert(c.buttons==0);
    c={};c.buttons=4;FallButtons(c,10.f,-600.f,800.f,1.f/64);assert((c.buttons&2) && !(c.buttons&4));
    c={};c.buttons=4;FallButtons(c,10.f,-100.f,800.f,1.f/64);assert(c.buttons==4);
    c={};FallButtons(c,10.f,-600.f,0.f,1.f/64);assert(c.buttons==0);
    c={};FallButtons(c,std::numeric_limits<float>::quiet_NaN(),-600.f,800.f,1.f/64);assert(c.buttons==0);
    PeekState peek;c={};peek.Update(c,true,true,true,{0,0,0},{0,0,0},false,p);assert(peek.valid && !peek.returning);
    c.buttons=1<<3;c.forwardmove=100;peek.Update(c,true,true,true,{50,0,0},{0,0,0},false,p);assert(peek.moved);
    c={};peek.Update(c,true,true,true,{50,0,0},{0,0,0},false,p);assert(peek.returning && c.forwardmove<0);
    c={};peek.Update(c,false,true,true,{50,0,0},{0,0,0},false,p);assert(!peek.valid);
    c={};peek.Update(c,true,false,false,{0,0,20},{0,0,0},false,p);assert(!peek.valid);
    peek.Update(c,true,false,true,{0,0,0},{0,0,0},false,p);
    peek.Update(c,true,false,true,{50,0,0},{0,0,0},true,p);assert(peek.returning && c.forwardmove<0);
    for(unsigned frame=0;frame<60;++frame){const auto tag=ClanFrame(frame);assert(tag.size()==15 && tag.find("gamesense")==std::string::npos);}
    assert(ClanFrame(11).find("Pastaware")!=std::string::npos);
    LongJumpState lj;c={};c.buttons=2;lj.Update(c,true,true,0);c={};lj.Update(c,true,false,200);assert((c.buttons&4) && !(c.buttons&2));
    lj.Update(c,false,false,100);assert(!lj.active);
    SequenceHistory history;SequenceHistory::Entry entry;
    history.Record(10,1,1.0);history.Record(11,0,1.1);history.Record(12,1,1.2);
    assert(history.Select(1.25,.1,entry) && entry.sequence==11 && entry.reliable==0);
    assert(!history.Select(1.25,.5,entry));history.Record(1,0,2);assert(history.count==1);
    history.Record(1,0,2.1);assert(history.count==1);
    s={};s.pistol=5;assert(BuyCommands(s,2)=="buy cz75a;");
    s.pistol=6;assert(BuyCommands(s,3)=="buy revolver;");
    s.logs=13;Publish(s);assert(Get().logs==13);
    PlayerState(7,true);assert(Alive() && ActiveWeapon()==7);
    PingApplied(true);assert(PingActive());PingApplied(false);
    std::atomic<bool> publishing{true};
    std::thread writer([&]{for(unsigned n=0;n<10000;++n){Settings next;next.logs=n;next.hit_details=n^0x1234;Publish(next);}publishing.store(false);});
    while(publishing.load()){const auto next=Get();assert((next.logs==13 && next.hit_details==0) || next.hit_details==(next.logs^0x1234));}
    writer.join();
    // Preserve a shot-triggered return across an airborne command.
    peek.Reset();c={};peek.Update(c,true,false,true,{0,0,0},{},false,p);
    peek.Update(c,true,false,false,{50,0,20},{},true,p);assert(peek.returning);
    c={};peek.Update(c,true,false,true,{50,0,0},{},false,p);assert(c.forwardmove<0);
    // Brake before crossing the anchor, not only inside the 3-unit radius.
    c={};peek.Update(c,true,false,true,{8,0,0},{-200,0,0},false,p);assert(c.forwardmove>0);
    peek.Update(c,true,false,false,{600,0,20},{},false,p);assert(!peek.valid);
    peek.Update(c,true,false,true,{0,0,0},{},false,p);
    peek.Update(c,true,false,false,{0,0,100},{},false,p);assert(!peek.valid);
    s={};s.strafe=6;c={};c.buttons=2|16;c.mousedx=10;
    Move(c,s,false,2,0,200,0);assert(c.sidemove< -449.f && std::abs(c.forwardmove)<.001f);
    s.strafe=6;c={};c.buttons=2|512;c.mousedx=10;
    Move(c,s,false,2,0,200,0);assert(c.forwardmove>449.f && std::abs(c.sidemove)<.001f);
    PeekMarker marker;marker.valid=true;marker.origin={10,20,30};PublishPeekMarker(marker);
    assert(GetPeekMarker().valid && GetPeekMarker().origin.y==20);PublishPeekMarker({});assert(!GetPeekMarker().valid);
}
