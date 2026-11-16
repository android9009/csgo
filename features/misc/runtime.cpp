#include "runtime.hpp"
#include "../../context.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/entity/player.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include "../../third_party/fatality/resources/misc_sounds.h"
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstring>
namespace MiscRuntime {
namespace {
bool registered{},pending_buy{};ULONGLONG pending_since{},last_buy{};
struct RadarEntry {void* entity{};bool original{},written{};};
std::array<RadarEntry,65> radar;
std::ptrdiff_t spotted{};
struct Event {
    const char* Name(){return MEM::GetVFunc<const char*(__thiscall*)(void*)>(this,1)(this);}
    int Int(const char* key){return MEM::GetVFunc<int(__thiscall*)(void*,const char*,int)>(this,6)(this,key,0);}
    const char* String(const char* key){return MEM::GetVFunc<const char*(__thiscall*)(void*,const char*,const char*)>(this,9)(this,key,"");}
};
int User(int id){return MEM::GetVFunc<int(__thiscall*)(void*,int)>(Interfaces::Engine,9)(Interfaces::Engine,id);}
void Command(const char* command){if(Interfaces::Engine && command && *command)MEM::GetVFunc<void(__thiscall*)(void*,const char*)>(Interfaces::Engine,7)(Interfaces::Engine,command);}
void Clean(char* dst,std::size_t capacity,const char* src){
    std::size_t i=0;if(src)for(;i+1<capacity && src[i];++i){const unsigned char c=static_cast<unsigned char>(src[i]);dst[i]=c<32 || c==127 || c==';' || c=='"' || c=='\'' || c=='\\' ? '_' : static_cast<char>(c);}dst[i]=0;
}
void Name(int index,char (&name)[128]){
    alignas(8) unsigned char data[512]{};
    if(index>0 && Interfaces::Engine->GetPlayerInfo(index,data)){char raw[128]{};std::memcpy(raw,data+16,127);Clean(name,sizeof(name),raw);}
    else std::snprintf(name,sizeof(name),"player %d",index);
}
void MessageLine(const Settings& options,int kind,const char* text){
    Message message;Clean(message.text,sizeof(message.text),text);message.color=options.colors[kind];message.time=static_cast<float>(GetTickCount64())*.001f;
    {std::lock_guard<std::mutex> lock(mutex);for(std::size_t i=0;i+1<messages.size();++i)messages[i]=messages[i+1];messages.back()=message;}
    LOG_INFO("misc: %s",message.text);
    if(options.console_logs){char command[224]{};std::snprintf(command,sizeof(command),"echo [Arena] %s",message.text);Command(command);}
}
void Sound(int selection){
    using namespace FatalityMiscSounds;const unsigned char* wave=nullptr;
    switch(selection){case 1:wave=bell;break;case 2:wave=cod;break;case 3:wave=fatality;break;case 4:
        if(Interfaces::Surface)MEM::GetVFunc<void(__thiscall*)(void*,const char*)>(Interfaces::Surface,82)(Interfaces::Surface,"buttons/arena_switch_press_02.wav");return;
        case 5:wave=bepis;break;case 6:wave=phonk;break;case 7:wave=sparkle;break;case 8:wave=cock;break;default:return;}
    PlaySoundA(reinterpret_cast<LPCSTR>(wave),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);
}
void OnEvent(Event* event){
    if(!event || !Interfaces::Engine || !ctx.in_game)return;
    const char* name=event->Name();if(!name)return;
    const auto options=Get();const int local=Interfaces::Engine->GetLocalPlayer();
    if(!std::strcmp(name,"round_start")){pending_buy=true;pending_since=GetTickCount64();return;}
    if(!std::strcmp(name,"player_spawn")){if(User(event->Int("userid"))==local){pending_buy=true;pending_since=GetTickCount64();}return;}
    if(!std::strcmp(name,"game_newmap") || !std::strcmp(name,"round_end")){pending_buy=false;return;}
    if(!std::strcmp(name,"player_hurt")){
        const int attacker=User(event->Int("attacker")),victim=User(event->Int("userid"));
        if(attacker==local && victim!=local){
            if(options.hit_enabled)Sound(options.hit_sound);
            const auto* weapon=event->String("weapon");const bool grenade=weapon && (!std::strcmp(weapon,"hegrenade") || !std::strcmp(weapon,"inferno"));
            const int kind=grenade && (options.logs&16u) ? 4 : 0;
            if(options.logs&(1u<<kind)){char player[128]{},line[192]{};Name(victim,player);
                if(options.hit_details&1u)std::snprintf(line,sizeof(line),"%s %s: %d damage (%d hp)",grenade ? "Naded" : "Hit",player,event->Int("dmg_health"),event->Int("health"));
                else std::snprintf(line,sizeof(line),"%s %s",grenade ? "Naded" : "Hit",player);
                MessageLine(options,kind,line);
            }
        }else if(victim==local && (options.logs&8u)){
            char player[128]{},line[192]{};Name(attacker,player);std::snprintf(line,sizeof(line),"Hurt by %s: %d damage",player,event->Int("dmg_health"));MessageLine(options,3,line);
        }
    }else if(!std::strcmp(name,"player_death")){
        if(User(event->Int("attacker"))==local && User(event->Int("userid"))!=local && options.kill_enabled)Sound(options.kill_sound);
    }else if(!std::strcmp(name,"item_purchase") && (options.logs&4u)){
        const int index=User(event->Int("userid"));
        auto* buyer=static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(index));if(!buyer || !ctx.local)return;
        const unsigned target=index==local ? 4u : buyer->m_iTeamNum()==ctx.local->m_iTeamNum() ? 1u : 2u;
        if(!(options.buy_targets&(target|8u)))return;
        char player[128]{},weapon[64]{},line[192]{};Name(index,player);Clean(weapon,sizeof(weapon),event->String("weapon"));
        std::snprintf(line,sizeof(line),"%.110s bought %.50s",player,weapon);MessageLine(options,2,line);
    }
}
class Listener {
public:
    virtual ~Listener()=default;
    virtual void FireGameEvent(Event* event){__try{OnEvent(event);}__except(EXCEPTION_EXECUTE_HANDLER){static bool logged=false;if(!logged){logged=true;LOG_WARN("misc: rejected event data");}}}
    virtual int GetEventDebugID(){return 42;}
} listener;
void ClearMessages(){std::lock_guard<std::mutex> lock(mutex);messages={};}
void RestoreRadar(){
    for(int i=1;i<=64;++i){auto& record=radar[i];
        if(record.written && spotted>0 && ctx.in_game && Interfaces::EntityList && Interfaces::EntityList->GetClientEntity(i)==record.entity)
            *reinterpret_cast<bool*>(static_cast<unsigned char*>(record.entity)+spotted)=record.original;
        record={};
    }
}
void FrameImpl(){
    const auto options=Get();
    if(!ctx.in_game || !ctx.local || !Interfaces::EntityList){for(auto& r:radar)r={};pending_buy=false;return;}
    if(!options.radar)RestoreRadar();
    else {
        if(!spotted)spotted=PropManager::Get().GetOffset("DT_BaseEntity","m_bSpotted");
        if(spotted>0 && spotted<0x10000)for(int i=1;i<=64;++i){
            auto* entity=static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(i));auto& record=radar[i];
            if(entity!=record.entity)record={};
            if(!entity || entity==ctx.local || entity->IsDormant() || !entity->IsAlive())continue;
            auto* cls=entity->GetClientClass();if(!cls || !cls->m_pNetworkName || std::strcmp(cls->m_pNetworkName,"CCSPlayer"))continue;
            auto* flag=reinterpret_cast<bool*>(reinterpret_cast<unsigned char*>(entity)+spotted);
            if(!record.written){record.entity=entity;record.original=*flag;record.written=true;}*flag=true;
        }
    }
    if(pending_buy){
        const auto now=GetTickCount64();
        if(now-pending_since>10000 || now-last_buy<1000){pending_buy=false;return;}
        static const auto zone=PropManager::Get().GetOffset("DT_CSPlayer","m_bInBuyZone");
        if(!ctx.local->IsAlive() || zone<=0 || zone>=0x10000 || !*reinterpret_cast<bool*>(reinterpret_cast<unsigned char*>(ctx.local)+zone))return;
        const auto commands=BuyCommands(options,ctx.local->m_iTeamNum());pending_buy=false;
        if(!commands.empty()){Command(commands.c_str());last_buy=now;LOG_INFO("misc buybot: submitted configured purchases");}
    }
}
void MoveImpl(CUserCmd* cmd){
    if(!cmd || !ctx.in_game || !ctx.local || !ctx.local->IsAlive())return;
    const auto options=Get();const auto velocity=ctx.local->m_vecVelocity();
    // Datamap entries in the supplied client.dll: m_MoveType=0x25C,
    // m_nWaterLevel=0x25E. Both are byte fields, not 32-bit NETVAR reads.
    const auto* bytes=reinterpret_cast<const unsigned char*>(ctx.local);
    const int type=bytes[0x25c],water=bytes[0x25e];const bool ground=(ctx.local->m_fFlags()&1)!=0;
    bool server_bhop=false;
    if(Interfaces::ConVar){
        static void* variable=MEM::GetVFunc<void*(__thiscall*)(void*,const char*)>(Interfaces::ConVar,14)(Interfaces::ConVar,"sv_autobunnyhopping");
        if(variable)server_bhop=MEM::GetVFunc<int(__thiscall*)(void*)>(variable,13)(variable)!=0;
    }
    Move(*cmd,options,ground,type,water,velocity.x,velocity.y,server_bhop);
    if(options.avoid_walls && options.strafe && !ground && Walkable(type,water) && Interfaces::EngineTrace && velocity.Length2D()>20.f){
        const auto start=ctx.local->m_vecOrigin()+Vector{0,0,2};Ray_t ray;
        ray.Init(start,start+velocity*.1f,{-16,-16,0},{16,16,54});CTraceFilterSimple filter(ctx.local);CGameTrace trace;
        Interfaces::EngineTrace->TraceRay(ray,0x200400Bu,&filter,&trace);
        if(trace.fraction<1.f && !trace.startsolid && trace.plane.normal.z<.7f){
            const float yaw=cmd->viewangles.y*.01745329252f,c=std::cos(yaw),sn=std::sin(yaw);
            float x=cmd->forwardmove*c+cmd->sidemove*sn,y=cmd->forwardmove*sn-cmd->sidemove*c;
            const float dot=x*trace.plane.normal.x+y*trace.plane.normal.y;
            if(dot<0.f){x-=dot*trace.plane.normal.x;y-=dot*trace.plane.normal.y;cmd->forwardmove=std::clamp(x*c+y*sn,-450.f,450.f);cmd->sidemove=std::clamp(x*sn-y*c,-450.f,450.f);}
        }
    }
}
}
void Initialize(){
    if(registered || !Interfaces::GameEvent)return;
    auto add=MEM::GetVFunc<bool(__thiscall*)(void*,void*,const char*,bool)>(Interfaces::GameEvent,3);
    bool ok=true;for(const char* event:{"round_start","round_end","game_newmap","player_spawn","player_hurt","player_death","item_purchase"})ok=add(Interfaces::GameEvent,&listener,event,false) && ok;
    if(!ok){MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::GameEvent,5)(Interfaces::GameEvent,&listener);LOG_WARN("misc: event listener unavailable");return;}
    registered=true;LOG_INFO("misc: event listener registered");
}
void Shutdown(){
    if(registered && Interfaces::GameEvent)MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::GameEvent,5)(Interfaces::GameEvent,&listener);
    registered=false;pending_buy=false;PlaySoundA(nullptr,nullptr,0);
    __try{RestoreRadar();}__except(EXCEPTION_EXECUTE_HANDLER){}
    ClearMessages();
}
void CreateMove(CUserCmd* cmd){
    if(!cmd)return;const float f=cmd->forwardmove,s=cmd->sidemove;const int buttons=cmd->buttons;
    __try{MoveImpl(cmd);}__except(EXCEPTION_EXECUTE_HANDLER){cmd->forwardmove=f;cmd->sidemove=s;cmd->buttons=buttons;}
}
void Frame(){__try{FrameImpl();}__except(EXCEPTION_EXECUTE_HANDLER){static bool logged=false;if(!logged){logged=true;LOG_WARN("misc: rejected frame entity data");}}}
}
