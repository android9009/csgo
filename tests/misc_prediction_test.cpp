#include "../features/misc/prediction.hpp"
#include <cassert>
#include <limits>
using namespace MiscPrediction;
struct Empty {
    Trace operator()(const Vector&,const Vector& end,const Vector&,const Vector&){Trace t;t.end=end;return t;}
};
struct Floor {
    Trace operator()(const Vector& start,const Vector& end,const Vector&,const Vector&){
        Trace t;t.end=end;
        if(end.z<0.f && start.z>=0.f){t.fraction=start.z/(start.z-end.z);t.end=start+(end-start)*t.fraction;t.normal={0,0,1};}
        return t;
    }
};
struct LipContacts {
    Trace operator()(const Vector& start,const Vector& end,const Vector&,const Vector&){
        Trace t;t.end=end;
        // Deterministic contact fixture: falling sweeps touch a lip, then
        // leave it horizontally; the separate support probe finds no ground.
        if(end.x>start.x && end.z<start.z){t.fraction=.5f;t.end=start+(end-start)*.5f;t.normal={0,0,1};}
        return t;
    }
};
int main(){
    Rules rules;CUserCmd cmd;Empty empty;Floor floor;
    State original{{0,0,100},{100,0,-100},false,0},s=original;Budget b;
    auto result=Tick(s,cmd,rules,empty,b);assert(!result.invalid && s.origin.z<100 && s.velocity.z< -100);
    assert(original.origin.z==100 && original.velocity.z== -100); // snapshot isolation
    s={{0,0,0},{0,0,0},true,0};b={};result=Tick(s,cmd,rules,floor,b);assert(!result.invalid && s.grounded);
    s={{0,0,0},{100,0,0},true,0};b={};result=Tick(s,cmd,rules,empty,b);assert(result.left_ground && !s.grounded);
    s={{0,0,0},{0,0,0},true,0};b={};cmd.buttons=2;result=Tick(s,cmd,rules,floor,b);assert(!result.invalid && s.velocity.z>250.f && !s.grounded);
    cmd={};s={{0,0,.5f},{0,0,-200},false,0};b={};result=Tick(s,cmd,rules,floor,b);assert(result.floor && result.landed && s.grounded);
    auto choice=Search(original,cmd,rules,true,true,empty);assert(!choice.found && choice.traces<=512);
    s=original;b.remaining=0;result=Tick(s,cmd,rules,empty,b);assert(result.invalid && b.exhausted);
    s=original;s.velocity.z=std::numeric_limits<float>::quiet_NaN();b={};assert(Tick(s,cmd,rules,empty,b).invalid);
    s=original;Rules invalid=rules;invalid.ground.interval=0;assert(Tick(s,cmd,invalid,empty,b).invalid);
    const Vector clipped=Clip({100,0,-200},{0,0,1});assert(clipped.x==100 && clipped.z==0);
    // A velocity resembling half-gravity without a collision must never trigger.
    s={{0,0,100},{100,0,-rules.gravity*rules.ground.interval*.5f},false,0};assert(!Search(s,cmd,rules,true,true,empty).found);
    LipContacts lips;
    choice=Search(original,cmd,rules,true,false,lips);assert(choice.found && choice.kind==1 && choice.tick==1);
    choice=Search(original,cmd,rules,false,true,lips);assert(choice.found && choice.kind==2 && choice.tick>=3);
    State grounded{{0,0,0},{100,0,0},true,0};assert(!Search(grounded,cmd,rules,true,true,lips).found);
}
