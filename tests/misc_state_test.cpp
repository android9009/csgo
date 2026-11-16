#include "../features/misc/state.hpp"
#include <cassert>
#include <limits>
int main(){
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
    c={};CounterMove(c,10,0,p);assert(std::abs(c.forwardmove)<=.0011f);
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
}
