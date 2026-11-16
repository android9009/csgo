#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include "../../sdk/structs.hpp"
namespace MiscRuntime {
struct Settings {
    bool bhop{},instant_duck{},quick_stop{},avoid_walls{},radar{},console_logs{};
    unsigned strafe{},logs{},hit_details{},buy_targets{};float smoothing{};
    int primary{},pistol{},armor{},hit_sound{},kill_sound{};
    bool defuser{},taser{},grenades{},hit_enabled{},kill_enabled{};
    std::array<std::uint32_t,5> colors{};
};
struct Message {char text[192]{};std::uint32_t color{};float time{};};
inline std::mutex mutex;
inline Settings settings;
inline std::array<Message,8> messages{};
inline void Publish(const Settings& s){std::lock_guard<std::mutex> lock(mutex);settings=s;}
inline Settings Get(){std::lock_guard<std::mutex> lock(mutex);return settings;}
inline std::array<Message,8> Messages(){std::lock_guard<std::mutex> lock(mutex);return messages;}
inline bool Walkable(int type,int water){return type==2 && water<2;}
inline void Move(CUserCmd& cmd,const Settings& s,bool ground,int type,int water,float vx,float vy,bool server_bhop=false){
    constexpr int jump=1<<1,speed_key=1<<17,bullrush=1<<22;
    if(!Walkable(type,water) || !std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(cmd.viewangles.y))return;
    const bool wants_jump=(cmd.buttons&jump)!=0;
    if(s.instant_duck)cmd.buttons|=bullrush;
    if(s.bhop && !server_bhop && !ground)cmd.buttons&=~jump;
    constexpr float radians=.01745329252f;
    const float yaw=cmd.viewangles.y*radians,c=std::cos(yaw),sn=std::sin(yaw),speed=std::hypot(vx,vy);
    if(ground){
        if(s.quick_stop && !wants_jump && !(cmd.buttons&((1<<3)|(1<<4)|(1<<9)|(1<<10))) && std::abs(cmd.forwardmove)<1.f && std::abs(cmd.sidemove)<1.f && speed>1.f){
            cmd.forwardmove=std::clamp(-(vx*c+vy*sn),-450.f,450.f);
            cmd.sidemove=std::clamp(vy*c-vx*sn,-450.f,450.f);
        }
        return;
    }
    if(!(s.strafe&3u) || (cmd.buttons&speed_key) || (!wants_jump && !(s.strafe&8u)))return;
    const float original_forward=cmd.forwardmove,original_side=cmd.sidemove;
    float desired=cmd.viewangles.y;
    if((s.strafe&2u) && (std::abs(original_forward)>1.f || std::abs(original_side)>1.f))desired+=std::atan2(-original_side,original_forward)/radians;
    else if(!(s.strafe&1u))return;
    if((s.strafe&4u) && std::abs(cmd.mousedx)>1){cmd.forwardmove=0.f;cmd.sidemove=cmd.mousedx>0 ? 450.f : -450.f;return;}
    const float velocity_yaw=speed>1.f ? std::atan2(vy,vx)/radians : desired;
    const float delta=std::remainder(desired-velocity_yaw,360.f);
    const float ideal=std::atan2(15.f,(std::max)(speed,1.f))/radians;
    const float turn=std::abs(delta)>ideal ? std::copysign(ideal,delta) : (cmd.command_number&1 ? ideal : -ideal);
    const float wish_angle=std::acos(std::clamp(15.f/(std::max)(speed,15.f),0.f,1.f))/radians;
    const float heading=(velocity_yaw+std::copysign(wish_angle,turn)-cmd.viewangles.y)*radians;
    const float blend=1.f-.85f*std::clamp(s.smoothing/100.f,0.f,1.f);
    cmd.forwardmove=std::clamp(original_forward*(1.f-blend)+std::cos(heading)*450.f*blend,-450.f,450.f);
    cmd.sidemove=std::clamp(original_side*(1.f-blend)-std::sin(heading)*450.f*blend,-450.f,450.f);
}
inline std::string BuyCommands(const Settings& s,int team){
    if(team!=2 && team!=3)return {};
    std::string result;auto buy=[&](const char* name){result+="buy ";result+=name;result+=';';};
    switch(s.primary){case 1:buy(team==3 ? "m4a1" : "ak47");break;case 2:buy(team==3 ? "aug" : "sg556");break;case 3:buy("awp");break;case 4:buy("ssg08");break;case 5:buy(team==3 ? "scar20" : "g3sg1");break;case 6:buy("negev");break;default:break;}
    switch(s.pistol){case 1:buy("deagle");break;case 2:buy("elite");break;case 3:buy(team==3 ? "fiveseven" : "tec9");break;case 4:buy("p250");break;case 5:buy("fiveseven");break;default:break;}
    if(s.armor==1)buy("vest");else if(s.armor==2)buy("vesthelm");
    if(s.defuser && team==3)buy("defuser");
    if(s.taser)buy("taser");
    if(s.grenades){buy("hegrenade");buy("smokegrenade");buy("flashbang");buy(team==3 ? "incgrenade" : "molotov");}
    return result;
}
}
