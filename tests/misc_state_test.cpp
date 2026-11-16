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
}
