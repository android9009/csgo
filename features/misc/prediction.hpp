#pragma once
#include "state.hpp"
// Read-only Source-style movement forecast. Never calls ProcessMovement on the
// live player: every candidate owns its state, and shares only a trace budget.
namespace MiscPrediction {
struct Rules {
    MiscRuntime::Physics ground;
    float gravity{800.f},air_accelerate{12.f},jump_impulse{301.993f},duck_speed{6.f};
};
struct State {Vector origin{},velocity{};bool grounded{};float duck{};};
struct Trace {float fraction{1.f};Vector end{},normal{};bool solid{},all_solid{};};
struct Contact {bool floor{},wall{},landed{},left_ground{},invalid{};Vector normal{};};
struct Budget {int remaining{512};bool exhausted{};};
inline bool Valid(const State& s,const Rules& r){
    return s.origin.IsValid() && s.velocity.IsValid() && std::isfinite(s.duck) &&
        std::isfinite(r.gravity) && r.gravity>0.f && r.gravity<=4000.f &&
        std::isfinite(r.ground.interval) && r.ground.interval>=.001f && r.ground.interval<=.1f &&
        std::isfinite(r.air_accelerate) && r.air_accelerate>=0.f && r.air_accelerate<=10000.f &&
        std::isfinite(r.ground.friction) && r.ground.friction>=0.f &&
        std::isfinite(r.ground.accelerate) && r.ground.accelerate>=0.f &&
        std::isfinite(r.ground.max_speed) && r.ground.max_speed>0.f &&
        std::isfinite(r.ground.stop_speed) && r.ground.stop_speed>=0.f &&
        std::isfinite(r.ground.surface_friction) && r.ground.surface_friction>=0.f &&
        std::isfinite(r.jump_impulse) && r.jump_impulse>0.f &&
        std::isfinite(r.duck_speed) && r.duck_speed>0.f;
}
inline Vector Clip(const Vector& velocity,const Vector& normal){
    Vector result=velocity-normal*velocity.Dot(normal);
    const float residual=result.Dot(normal);
    if(residual<0.f)result-=normal*(std::min)(residual,-.03125f);
    return result;
}
template<class TraceFn>
Trace Sweep(const State& s,const Vector& start,const Vector& end,TraceFn& trace,Budget& budget){
    if(budget.remaining<=0){budget.exhausted=true;Trace t;t.solid=t.all_solid=true;t.end=start;return t;}
    --budget.remaining;
    auto t=trace(start,end,Vector{-16,-16,0},Vector{16,16,72.f-18.f*std::clamp(s.duck,0.f,1.f)});
    if(!std::isfinite(t.fraction) || t.fraction<0.f || t.fraction>1.f || !t.end.IsValid() || !t.normal.IsValid())t.solid=t.all_solid=true;
    return t;
}
template<class TraceFn>
Contact Tick(State& s,const CUserCmd& cmd,const Rules& r,TraceFn& trace,Budget& budget){
    Contact hit;
    if(!Valid(s,r) || !std::isfinite(cmd.forwardmove) || !std::isfinite(cmd.sidemove) || !std::isfinite(cmd.viewangles.y)){hit.invalid=true;return hit;}
    const bool was_grounded=s.grounded;
    const float dt=r.ground.interval,half=r.gravity*dt*.5f;
    const float target=(cmd.buttons&4) ? 1.f : 0.f;
    const float duck=std::clamp(s.duck+std::clamp(target-s.duck,-r.duck_speed*dt,r.duck_speed*dt),0.f,1.f);
    if(duck<s.duck){State standing=s;standing.duck=duck;const auto clear=Sweep(standing,s.origin,s.origin,trace,budget);if(!clear.solid && !clear.all_solid)s.duck=duck;}
    else s.duck=duck;
    if(s.grounded){
        s.velocity.z=0.f;
        const float speed=s.velocity.Length2D();
        if(speed>.01f){const float after=(std::max)(0.f,speed-(std::max)(speed,r.ground.stop_speed)*r.ground.friction*r.ground.surface_friction*dt);s.velocity.x*=after/speed;s.velocity.y*=after/speed;}
        if(cmd.buttons&2){s.grounded=false;s.velocity.z=r.jump_impulse;}
    }
    if(!s.grounded)s.velocity.z-=half;
    const float yaw=cmd.viewangles.y*.01745329252f,c=std::cos(yaw),sn=std::sin(yaw);
    Vector wish{cmd.forwardmove*c+cmd.sidemove*sn,cmd.forwardmove*sn-cmd.sidemove*c,0};
    float wish_speed=wish.NormalizeInPlace();
    wish_speed=(std::min)(wish_speed,r.ground.max_speed*(s.duck>.5f ? .34f : (cmd.buttons&(1<<17)) ? .52f : 1.f));
    const float add=(s.grounded ? wish_speed : (std::min)(wish_speed,30.f))-s.velocity.Dot(wish);
    if(add>0.f)s.velocity+=wish*(std::min)(add,(s.grounded ? r.ground.accelerate : r.air_accelerate)*wish_speed*dt*r.ground.surface_friction);
    const Vector primal=s.velocity;Vector anchor=s.velocity;
    std::array<Vector,5> planes{};int count=0;float remaining=dt;
    for(int bump=0;bump<4 && s.velocity.LengthSqr()>.000001f;++bump){
        const auto t=Sweep(s,s.origin,s.origin+s.velocity*remaining,trace,budget);
        if(t.solid || t.all_solid){hit.invalid=true;return hit;}
        if(t.fraction>0.f){s.origin=t.end;anchor=s.velocity;count=0;}
        if(t.fraction==1.f)break;
        if(t.normal.z>=.7f){hit.floor=true;hit.normal=t.normal;}
        else {hit.wall=true;hit.normal=t.normal;}
        remaining*=1.f-t.fraction;
        if(count>=5){s.velocity={};break;}
        planes[count++]=t.normal;
        int i=0;
        for(;i<count;++i){
            const Vector clipped=Clip(anchor,planes[i]);bool clear=true;
            for(int j=0;j<count;++j)if(i!=j && clipped.Dot(planes[j])<0.f){clear=false;break;}
            if(clear){s.velocity=clipped;break;}
        }
        if(i==count){
            if(count!=2){s.velocity={};break;}
            const Vector crease=planes[0].Cross(planes[1]).Normalized();s.velocity=crease*crease.Dot(s.velocity);
        }
        if(s.velocity.Dot(primal)<=0.f){s.velocity={};break;}
    }
    s.grounded=false;
    if(s.velocity.z<=140.f){
        const auto support=Sweep(s,s.origin,s.origin-Vector{0,0,2},trace,budget);
        if(support.solid || support.all_solid){hit.invalid=true;return hit;}
        s.grounded=support.fraction<1.f && support.normal.z>=.7f;
    }
    if(s.grounded)s.velocity.z=0.f;else s.velocity.z-=half;
    hit.landed=!was_grounded && s.grounded;hit.left_ground=was_grounded && !s.grounded;
    if(budget.exhausted || !Valid(s,r))hit.invalid=true;
    return hit;
}
struct Choice {bool found{},duck{};float forward{},side{};int tick{},kind{},traces{};};
template<class TraceFn>
Choice Search(const State& initial,const CUserCmd& original,const Rules& rules,bool edge,bool pixel,TraceFn& trace,int trace_limit=512){
    Choice best;Budget budget;budget.remaining=std::clamp(trace_limit,0,512);const int initial_budget=budget.remaining;
    if(initial.grounded || initial.velocity.z>=-40.f || !Valid(initial,rules))return best;
    int best_score=100000;
    for(int candidate=0;candidate<8;++candidate){
        State s=initial;CUserCmd cmd=original;
        if(candidate&1)cmd.buttons|=4;else cmd.buttons&=~4;
        switch(candidate/2){case 1:cmd.forwardmove=cmd.sidemove=0.f;break;case 2:cmd.forwardmove=0;cmd.sidemove=450;break;case 3:cmd.forwardmove=0;cmd.sidemove=-450;break;default:break;}
        int sustained=0;
        for(int tick=1;tick<=8;++tick){
            const State before=s;const auto contact=Tick(s,cmd,rules,trace,budget);
            if(contact.invalid || s.grounded)break;
            const float half=rules.gravity*rules.ground.interval*.5f;
            // Velocity alone is insufficient: require a real floor contact,
            // no supporting ground, and a falling -> clipped transition.
            const bool caught=contact.floor && !s.grounded && std::abs(s.velocity.z+half)<.1f;
            sustained=caught ? sustained+1 : 0;
            const int kind=pixel && sustained>=3 ? 2 : edge && caught && before.velocity.z< -40.f ? 1 : 0;
            if(kind){
                const int score=(kind==2 ? 0 : 1000)+tick*10+candidate;
                if(score<best_score){best_score=score;best={true,(cmd.buttons&4)!=0,cmd.forwardmove,cmd.sidemove,tick,kind,0};}
                if(kind==2 || !pixel)break;
            }
        }
        if(budget.exhausted)return {}; // Never select a partially evaluated search.
    }
    best.traces=initial_budget-budget.remaining;return best;
}
}
