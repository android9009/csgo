#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <string>
#include "../../sdk/structs.hpp"
namespace MiscRuntime {
struct Settings {
    bool bhop{},instant_duck{},quick_stop{},avoid_walls{},radar{},console_logs{},console_filter{},reveal_ranks{};
    bool peek{},retreat_on_release{},no_fall{},edge_jump{},autoaccept{},edge_bug{},pixel_surf{},edge_stop{};float peek_distance{-1.f};
    bool clantag{},ragdoll_gravity{},long_jump{},ladder_jump{},enemy_chat{},ping_spike{};float ping_amount{};float ragdoll_amount{100.f};
    unsigned indicators{},indicator_active{};int active_weapon{};bool alive{};
    unsigned strafe{},logs{},hit_details{},buy_targets{};float smoothing{};
    int primary{},pistol{},armor{},hit_sound{},kill_sound{};
    bool defuser{},taser{},grenades{},hit_enabled{},kill_enabled{};
    std::array<std::uint32_t,5> colors{};
};
inline bool MovementEnabled(const Settings& s){return s.bhop || s.instant_duck || s.quick_stop || s.avoid_walls || s.peek || s.no_fall || s.edge_jump || s.edge_bug || s.pixel_surf || s.edge_stop || s.long_jump || s.ladder_jump || s.strafe!=0;}
struct Message {char text[192]{};std::uint32_t color{};double time{};};
// Nonblocking snapshot exchange; never hold a lock while calling game/UI code.
// A contended reader keeps its own last complete copy. A contended publisher
// retries naturally on the next menu frame, rather than stalling a game thread.
inline std::mutex settings_mutex;
inline Settings settings;
inline std::mutex message_mutex;
inline std::array<Message,8> messages{};
inline void Publish(const Settings& s){
    std::unique_lock<std::mutex> lock(settings_mutex,std::try_to_lock);
    if(lock.owns_lock())settings=s;
}
inline Settings Get(){
    static thread_local Settings snapshot;
    std::unique_lock<std::mutex> lock(settings_mutex,std::try_to_lock);
    if(lock.owns_lock())snapshot=settings;
    return snapshot;
}
inline std::array<Message,8> Messages(){
    static thread_local std::array<Message,8> snapshot{};
    std::unique_lock<std::mutex> lock(message_mutex,std::try_to_lock);
    if(lock.owns_lock())snapshot=messages;
    return snapshot;
}
inline std::atomic<int> live_weapon{};
inline std::atomic<bool> live_alive{},ping_applied{};
inline void PingApplied(bool active){ping_applied.store(active,std::memory_order_relaxed);}
inline bool PingActive(){return ping_applied.load(std::memory_order_relaxed);}
inline int ActiveWeapon(){return live_weapon.load(std::memory_order_relaxed);}
inline bool Alive(){return live_alive.load(std::memory_order_relaxed);}
inline void PlayerState(int weapon,bool alive){live_weapon.store(weapon,std::memory_order_relaxed);live_alive.store(alive,std::memory_order_relaxed);}
inline std::string ClanFrame(unsigned tick){
    constexpr char text[]="             Pastaware                ";
    constexpr std::array<unsigned,30> frames{0,1,2,3,4,5,6,7,8,9,10,11,11,11,11,11,11,11,11,12,13,14,15,16,17,18,19,20,21,22};
    return std::string(text+frames[tick%frames.size()],15);
}
struct LongJumpState {
    bool was_grounded{},armed{},active{};
    void Reset(){*this={};}
    void Update(CUserCmd& cmd,bool enabled,bool ground,float vertical){
        if(!enabled){Reset();return;}
        if(ground){active=false;armed=(cmd.buttons&2)!=0;}
        else if(was_grounded && armed && vertical>0.f){active=true;armed=false;}
        if(active && !ground){cmd.buttons|=4;cmd.buttons&=~2;}
        was_grounded=ground;
    }
};
struct SequenceHistory {
    struct Entry {int sequence{},reliable{};double time{};};
    std::array<Entry,256> entries{};unsigned count{},next{};
    void Reset(){count=next=0;}
    void Record(int sequence,int reliable,double time){
        if(!std::isfinite(time))return;
        if(count){const auto& last=entries[(next+255)%256];if(time<last.time || static_cast<std::int32_t>(static_cast<std::uint32_t>(sequence)-static_cast<std::uint32_t>(last.sequence))<0)Reset();else if(sequence==last.sequence)return;}
        entries[next]={sequence,reliable,time};next=(next+1)%256;count=(std::min)(count+1,256u);
    }
    bool Select(double now,double delay,Entry& out) const {
        if(!std::isfinite(now) || !std::isfinite(delay) || delay<=0)return false;
        for(unsigned n=0;n<count;++n){const auto& e=entries[(next+255-n)%256];if(now-e.time>=delay){out=e;return true;}}
        return false;
    }
};
inline bool Walkable(int type,int water){return type==2 && water<2;}
// Adapted from the reference's friction_speed/write_counter_movement stages.
struct Physics {float interval{1.f/64.f},friction{5.2f},stop_speed{80.f},accelerate{5.5f},surface_friction{1.f},max_speed{250.f};};
inline void CounterMove(CUserCmd& cmd,float vx,float vy,const Physics& p){
    cmd.buttons&=~0x61a;cmd.forwardmove=cmd.sidemove=0.f;
    const float speed=std::hypot(vx,vy);if(!std::isfinite(speed) || speed<.1f)return;
    const float after=(std::max)(0.f,speed-(std::max)(speed,p.stop_speed)*p.friction*p.surface_friction*p.interval);
    if(after<.1f)return;
    float basis=(std::min)(250.f,p.max_speed);
    if(cmd.buttons&4)basis*=.34f;else if(cmd.buttons&(1<<17))basis*=.52f;
    const float acceleration=p.interval*p.accelerate*p.surface_friction*basis;
    // Solve the remaining speed against this tick's acceleration instead
    // of emitting 0.001 movement, which left low-speed slides uncorrected.
    const float factor=p.interval*p.accelerate*p.surface_friction;
    const float strength=acceleration>after && factor>0.f ? (std::min)(450.f,after/factor) : 450.f;
    const float yaw=cmd.viewangles.y*.01745329252f,c=std::cos(yaw),sn=std::sin(yaw);
    cmd.forwardmove=-(vx*c+vy*sn)/speed*strength;
    cmd.sidemove=(vy*c-vx*sn)/speed*strength;
}
// Skeet no_fall_damage::phase2: crouch while forecast enters the 9..11u
// landing window; release duck + jump in the actual dangerous landing window.
inline void FallButtons(CUserCmd& cmd,float distance,float vertical,float gravity,float interval){
    if(!std::isfinite(distance) || !std::isfinite(vertical) || !std::isfinite(gravity) || !std::isfinite(interval) || gravity<=0.f || interval<.001f || interval>.1f || distance<0.f || distance>=1000.f)return;
    if(vertical<0.f && distance<11.f){if(vertical< -580.f && distance>9.f)cmd.buttons=(cmd.buttons&~4)|2;return;}
    const float half=gravity*interval*.5f;
    for(int ticks=0;distance>11.f && ticks<4096;++ticks){vertical-=half;distance+=interval*vertical;vertical-=half;}
    if(distance<=11.f && vertical<= -580.f && distance>=9.f)cmd.buttons=(cmd.buttons|4)&~2;
}
inline void EdgeJump(CUserCmd& cmd,bool enabled,bool was_grounded,bool now_grounded){
    if(enabled && !(cmd.buttons&2) && was_grounded && !now_grounded)cmd.buttons|=2;
}
struct PeekState {
    bool valid{},returning{},moved{};Vector origin{};int command{};
    void Reset(){*this={};}
    void Update(CUserCmd& cmd,bool enabled,bool release,bool ground,const Vector& position,const Vector& velocity,bool shot,const Physics& physics,float limit=-1.f){
        if(!enabled || !position.IsValid() || !velocity.IsValid() || !std::isfinite(cmd.viewangles.y)){Reset();return;}
        if(!valid){if(!ground)return;origin=position;valid=true;}
        const float dx=origin.x-position.x,dy=origin.y-position.y,distance=std::hypot(dx,dy);
        // Discard stale anchors after teleports/floor changes, even in the air.
        if(distance>512.f || std::abs(origin.z-position.z)>64.f){Reset();return;}
        // A weapon_fire event may arrive while airborne. Latch it before the
        // ground-only steering gate instead of losing it for the whole peek.
        if(shot)returning=true;
        if(!ground)return;
        const bool input=(cmd.buttons&0x618)!=0 || std::hypot(cmd.forwardmove,cmd.sidemove)>.1f;
        if(input)moved=true;
        if(shot || (release && moved && !input))returning=true;
        if(!returning){
            if(std::isfinite(limit) && limit>=0.f && distance>limit && distance>.01f){origin.x=position.x+dx*limit/distance;origin.y=position.y+dy*limit/distance;}
            return;
        }
        const float closing=distance>.01f ? (velocity.x*dx+velocity.y*dy)/distance : 0.f;
        const float speed=velocity.Length2D();
        const float deceleration=(std::max)(speed,physics.stop_speed)*physics.friction*physics.surface_friction
            +physics.accelerate*physics.max_speed*physics.surface_friction;
        const float stopping=deceleration>0.f ? closing*closing/(2.f*deceleration) : 0.f;
        if(closing>5.f && distance<=stopping+closing*physics.interval+3.f){CounterMove(cmd,velocity.x,velocity.y,physics);return;}
        if(distance<3.f){CounterMove(cmd,velocity.x,velocity.y,physics);if(velocity.Length2D()<5.f){returning=false;moved=false;}return;}
        const float yaw=cmd.viewangles.y*.01745329252f,c=std::cos(yaw),sn=std::sin(yaw);
        // Stop-position compensation prevents repeatedly overshooting the anchor.
        const float x=dx-velocity.x*physics.interval*2.f,y=dy-velocity.y*physics.interval*2.f;
        const float length=std::hypot(x,y);if(length<.01f){CounterMove(cmd,velocity.x,velocity.y,physics);return;}
        const float strength=(std::min)(450.f,(std::max)(30.f,distance*20.f));
        cmd.buttons&=~0x618;cmd.forwardmove=(x*c+y*sn)/length*strength;cmd.sidemove=(x*sn-y*c)/length*strength;
    }
};
struct PeekMarker {Vector origin{};bool valid{},returning{};};
inline std::mutex peek_marker_mutex;
inline PeekMarker peek_marker;
inline void PublishPeekMarker(const PeekMarker& frame){
    std::unique_lock<std::mutex> lock(peek_marker_mutex,std::try_to_lock);
    if(lock.owns_lock())peek_marker=frame;
}
inline PeekMarker GetPeekMarker(){
    static thread_local PeekMarker frame;
    std::unique_lock<std::mutex> lock(peek_marker_mutex,std::try_to_lock);
    if(lock.owns_lock())frame=peek_marker;
    return frame;
}
inline void Move(CUserCmd& cmd,const Settings& s,bool ground,int type,int water,float vx,float vy,bool server_bhop=false,const Physics& physics={}){
    constexpr int jump=1<<1,speed_key=1<<17,bullrush=1<<22;
    if(!Walkable(type,water) || !std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(cmd.viewangles.y))return;
    const bool wants_jump=(cmd.buttons&jump)!=0;
    if(s.instant_duck)cmd.buttons|=bullrush;
    if(s.bhop && !server_bhop && !ground)cmd.buttons&=~jump;
    constexpr float radians=.01745329252f;
    const float speed=std::hypot(vx,vy);
    if(ground){
        if(s.quick_stop && !wants_jump && !(cmd.buttons&0x61a) && cmd.forwardmove*cmd.forwardmove+cmd.sidemove*cmd.sidemove<.01f && speed>2.f)CounterMove(cmd,vx,vy,physics);
        return;
    }
    if(!(s.strafe&3u) || (cmd.buttons&speed_key) || (!wants_jump && !(s.strafe&8u)))return;
    float original_forward=cmd.forwardmove,original_side=cmd.sidemove;
    if(!std::isfinite(original_forward) || !std::isfinite(original_side))return;
    // Buttons remain meaningful when another feature has zeroed analog move.
    if((s.strafe&2u) && std::abs(original_forward)<.01f && std::abs(original_side)<.01f){
        original_forward=((cmd.buttons&8) ? 450.f : 0.f)-((cmd.buttons&16) ? 450.f : 0.f);
        original_side=((cmd.buttons&1024) ? 450.f : 0.f)-((cmd.buttons&512) ? 450.f : 0.f);
    }
    if((s.strafe&1u) && !(s.strafe&2u) && ((cmd.buttons&16) || original_side!=0.f))return;
    float desired=cmd.viewangles.y;
    if((s.strafe&2u) && (std::abs(original_forward)>1.f || std::abs(original_side)>1.f))desired+=std::atan2(-original_side,original_forward)/radians;
    else if(!(s.strafe&1u))return;
    if((s.strafe&4u) && std::abs(cmd.mousedx)>1){
        const float side=cmd.mousedx>0 ? 450.f : -450.f,rotation=(desired-cmd.viewangles.y)*radians;
        cmd.forwardmove=std::sin(rotation)*side;cmd.sidemove=std::cos(rotation)*side;return;
    }
    const float velocity_yaw=speed>1.f ? std::atan2(vy,vx)/radians : desired;
    const float delta=std::remainder(desired-velocity_yaw,360.f);
    const float ideal=(std::min)(90.f,845.5f/(std::max)(speed,1.f));
    const float correction=(100.f-std::clamp(s.smoothing,0.f,100.f))*.02f*(ideal+ideal);
    float side{},heading=desired;
    if(delta>correction && speed>80.f){heading=velocity_yaw+correction;side=-450.f;}
    else if(-correction>delta && speed>80.f){heading=velocity_yaw-correction;side=450.f;}
    else if(cmd.command_number&1){heading-=ideal;side=-450.f;}
    else {heading+=ideal;side=450.f;}
    // The reference writes a movement-angle view. Rotate that view back into
    // this command's unchanged camera basis instead of silently changing aim.
    const float rotation=(heading-cmd.viewangles.y)*radians;
    cmd.forwardmove=std::sin(rotation)*side;cmd.sidemove=std::cos(rotation)*side;

}
inline std::string BuyCommands(const Settings& s,int team){
    if(team!=2 && team!=3)return {};
    std::string result;auto buy=[&](const char* name){result+="buy ";result+=name;result+=';';};
    switch(s.primary){case 1:buy(team==3 ? "m4a1" : "ak47");break;case 2:buy(team==3 ? "aug" : "sg556");break;case 3:buy("awp");break;case 4:buy("ssg08");break;case 5:buy(team==3 ? "scar20" : "g3sg1");break;case 6:buy("negev");break;default:break;}
    switch(s.pistol){case 1:buy("deagle");break;case 2:buy("elite");break;case 3:buy(team==3 ? "fiveseven" : "tec9");break;case 6:buy("revolver");break;case 4:buy("p250");break;case 5:buy("cz75a");break;default:break;}
    if(s.armor==1)buy("vest");else if(s.armor==2)buy("vesthelm");
    if(s.defuser && team==3)buy("defuser");
    if(s.taser)buy("taser");
    if(s.grenades){buy("hegrenade");buy("smokegrenade");buy("flashbang");buy(team==3 ? "incgrenade" : "molotov");}
    return result;
}
}
