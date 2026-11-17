#include "runtime.hpp"
#include "prediction.hpp"
#include "../../context.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/entity/player.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include "../../utils/detourhook.hpp"
#include <atomic>
#include "../../sdk/third_party/fatality/resources/misc_sounds.h"
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
PeekState peek_state;
LongJumpState long_jump_state;
std::atomic<bool> peek_shot{};
void* movement_player{};int last_command{};
float ConVarFloat(const char* name,float fallback){
    // Per-thread cache avoids racing the network hook against CreateMove.
    struct Cached {const char* name{};void* var{};};
    static thread_local std::array<Cached,16> cache{};
    static thread_local void* owner{};
    if(!Interfaces::ConVar)return fallback;
    if(owner!=Interfaces::ConVar){cache={};owner=Interfaces::ConVar;}
    void* var=nullptr;
    for(auto& entry:cache){
        if(entry.name && std::strcmp(entry.name,name))continue;
        if(!entry.name){entry.name=name;entry.var=MEM::GetVFunc<void*(__thiscall*)(void*,const char*)>(Interfaces::ConVar,14)(Interfaces::ConVar,name);}
        var=entry.var;break;
    }
    if(!var)return fallback;
    const float result=MEM::GetVFunc<float(__thiscall*)(void*)>(var,12)(var);
    return std::isfinite(result) ? result : fallback;
}
Physics CapturePhysics(){
    Physics p;if(Interfaces::Globals)p.interval=Interfaces::Globals->interval_per_tick;
    p.friction=ConVarFloat("sv_friction",5.2f);p.stop_speed=ConVarFloat("sv_stopspeed",80.f);p.accelerate=ConVarFloat("sv_accelerate",5.5f);
    return p;
}
class WorldFilter : public ITraceFilter {
public: bool ShouldHitEntity(void*,int) override{return false;}int GetTraceType() const override{return 0;}
};
CGameTrace Hull(const Vector& start,const Vector& end,const Vector& mins,const Vector& maxs,unsigned mask=0x200400Bu,bool world_only=false){
    Ray_t ray;ray.Init(start,end,mins,maxs);
    // Ray_t's existing SDK helper stores the center with the wrong sign.
    // Correct only this local ray, without changing any World consumers.
    ray.m_StartOffset.x=-ray.m_StartOffset.x;ray.m_StartOffset.y=-ray.m_StartOffset.y;ray.m_StartOffset.z=-ray.m_StartOffset.z;
    CTraceFilterSimple filter(ctx.local);WorldFilter world;CGameTrace trace;
    Interfaces::EngineTrace->TraceRay(ray,mask,world_only ? static_cast<ITraceFilter*>(&world) : &filter,&trace);return trace;
}
VMT_Hook accept_event_hook,accept_destroy_hook;
void* accept_receiver{};
using ReadyFn=char(__stdcall*)(const char*);
ReadyFn ready{};
using AcceptEventFn=std::uint32_t(__thiscall*)(void*,const char*,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using AcceptDestroyFn=std::uint32_t(__thiscall*)(void*,std::uintptr_t);
AcceptEventFn accept_original{};AcceptDestroyFn accept_destroy_original{};
ULONGLONG accepted_at{};
bool AcceptName(const char* name){
    if(!name)return false;
    std::uint32_t hash=0x811c9dc5;std::size_t i=0;
    for(;i<256 && name[i];++i)hash=(hash^static_cast<unsigned char>(name[i]))*0x1000193;
    return i<256 && ((hash^0x92ca5df1u)==0xe8bb3240u || !std::strcmp(name,"UIPanorama.popup_accept_match_beep"));
}
void TryAccept(void* receiver,const char* name){
    if(receiver!=accept_receiver || !AcceptName(name) || !ready || !Get().autoaccept || !Interfaces::Engine || Interfaces::Engine->IsInGame())return;
    const auto now=GetTickCount64();if(accepted_at && now-accepted_at<5000)return;
    accepted_at=now;const char result=ready("accept");LOG_INFO("misc autoaccept: ready callback returned %d",static_cast<int>(result));
}
void SafeAccept(void* receiver,const char* name){__try{TryAccept(receiver,name);}__except(EXCEPTION_EXECUTE_HANDLER){}}
std::uint32_t __fastcall AcceptEvent(void* receiver,void*,const char* name,std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d,std::uintptr_t e,std::uintptr_t f){
    SafeAccept(receiver,name);return accept_original ? accept_original(receiver,name,a,b,c,d,e,f) : 0;
}
std::uint32_t __fastcall AcceptDestroy(void* receiver,void*,std::uintptr_t argument){
    const auto original=accept_destroy_original;
    if(receiver==accept_receiver){accept_event_hook.detach();accept_destroy_hook.detach();accept_receiver=nullptr;}
    return original ? original(receiver,argument) : 0;
}
void InstallAcceptImpl(){
    // Match the full callback body and its deferred string, not a foreign RVA.
    const auto address=MEM::FindPattern("client.dll","55 8B EC 83 E4 F8 8B 4D 08 BA ? ? ? ? E8 ? ? ? ? 85 C0 75 12 8D 50 02 33 C9 E8 ? ? ? ? B0 01 8B E5 5D C2 04 00 83 3D ? ? ? ? 00 74 0C 51 E8 ? ? ? ? 8B E5 5D C2 04 00 32 C0 8B E5 5D C2 04 00");
    if(!address){LOG_WARN("misc autoaccept: verified ready callback not found");return;}
    std::uint32_t text{};std::memcpy(&text,reinterpret_cast<void*>(address+10),4);
    if(std::strcmp(reinterpret_cast<const char*>(text),"deferred"))return;
    void* panorama=Interfaces::CaptureRaw("panorama.dll","PanoramaUIEngine001");if(!panorama)return;
    void* engine=MEM::GetVFunc<void*(__thiscall*)(void*)>(panorama,11)(panorama);if(!engine)return;
    void* receiver=MEM::GetVFunc<void*(__thiscall*)(void*)>(engine,25)(engine);if(!receiver)return;
    accept_original=MEM::GetVFunc<AcceptEventFn>(receiver,1);accept_destroy_original=MEM::GetVFunc<AcceptDestroyFn>(receiver,0);
    if(!accept_original || !accept_destroy_original)return;
    ready=reinterpret_cast<ReadyFn>(address);accept_receiver=receiver;
    if(!accept_event_hook.attach(receiver,1,reinterpret_cast<void*>(&AcceptEvent)) || !accept_destroy_hook.attach(receiver,0,reinterpret_cast<void*>(&AcceptDestroy))){accept_event_hook.detach();accept_destroy_hook.detach();accept_receiver=nullptr;ready=nullptr;return;}
    LOG_INFO("misc autoaccept: Panorama event receiver installed, ready RVA=%p",reinterpret_cast<void*>(address-reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"))));
}
void InstallAccept(){__try{InstallAcceptImpl();}__except(EXCEPTION_EXECUTE_HANDLER){accept_event_hook.detach();accept_destroy_hook.detach();accept_receiver=nullptr;ready=nullptr;LOG_WARN("misc autoaccept: receiver validation failed");}}

struct Event {
    const char* Name(){return MEM::GetVFunc<const char*(__thiscall*)(void*)>(this,1)(this);}
    int Int(const char* key){return MEM::GetVFunc<int(__thiscall*)(void*,const char*,int)>(this,6)(this,key,0);}
    const char* String(const char* key){return MEM::GetVFunc<const char*(__thiscall*)(void*,const char*,const char*)>(this,9)(this,key,"");}
};
int User(int id){return MEM::GetVFunc<int(__thiscall*)(void*,int)>(Interfaces::Engine,9)(Interfaces::Engine,id);}
void Command(const char* command){if(Interfaces::Engine && command && *command)MEM::GetVFunc<void(__thiscall*)(void*,const char*)>(Interfaces::Engine,7)(Interfaces::Engine,command);}
void Sound(int selection){
    using namespace FatalityMiscSounds;const unsigned char* wave=nullptr;
    switch(selection){case 1:wave=bell;break;case 2:wave=cod;break;case 3:wave=fatality;break;case 4:
        if(Interfaces::Surface)MEM::GetVFunc<void(__thiscall*)(void*,const char*)>(Interfaces::Surface,82)(Interfaces::Surface,"buttons/arena_switch_press_02.wav");return;
        case 5:wave=bepis;break;case 6:wave=phonk;break;case 7:wave=sparkle;break;case 8:wave=cock;break;default:return;}
    PlaySoundA(reinterpret_cast<LPCSTR>(wave),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);
}
void EventLog(const Settings& options,unsigned category,const char* text){
    if(!(options.logs&(1u<<category)))return;
    Message message;std::snprintf(message.text,sizeof message.text,"%s",text);
    message.color=options.colors[category];message.time=static_cast<double>(GetTickCount64())*.001;
    {std::lock_guard<std::mutex> lock(message_mutex);
        for(std::size_t i=messages.size()-1;i>0;--i){messages[i]=messages[i-1];}
        messages[0]=message;}
    if(options.console_logs){
        struct ConsoleColor {unsigned char r,g,b,a;};
        using Print=void(__cdecl*)(const ConsoleColor&,const char*,...);
        static auto print=reinterpret_cast<Print>(GetProcAddress(GetModuleHandleA("tier0.dll"),"?ConColorMsg@@YAXABVColor@@PBDZZ"));
        const auto c=message.color;
        const ConsoleColor color{static_cast<unsigned char>(c>>16),static_cast<unsigned char>(c>>8),static_cast<unsigned char>(c),static_cast<unsigned char>(c>>24)};
        if(print)print(color,"[Pastaware] %s\n",message.text);
        else LOG_INFO("misc event: %s",message.text);
    }
}
void OnEvent(Event* event){
    if(!event || !Interfaces::Engine || !ctx.in_game)return;
    const char* name=event->Name();if(!name)return;
    const auto options=Get();const int local=Interfaces::Engine->GetLocalPlayer();
    if(!std::strcmp(name,"weapon_fire")){if(User(event->Int("userid"))==local)peek_shot.store(true);return;}
    if(!std::strcmp(name,"round_start")){pending_buy=true;pending_since=GetTickCount64();return;}
    if(!std::strcmp(name,"player_spawn")){if(User(event->Int("userid"))==local){pending_buy=true;pending_since=GetTickCount64();}return;}
    if(!std::strcmp(name,"game_newmap") || !std::strcmp(name,"round_end")){pending_buy=false;return;}
    if(!std::strcmp(name,"player_hurt")){
        const int attacker=User(event->Int("attacker")),victim=User(event->Int("userid"));
        if(attacker==local && victim!=local){
            if(options.hit_enabled)Sound(options.hit_sound);
            char text[192];std::snprintf(text,sizeof text,"Hit player #%d for %d damage (%d HP remaining, hitgroup %d)",victim,event->Int("dmg_health"),event->Int("health"),event->Int("hitgroup"));
            EventLog(options,0,text);
            const char* weapon=event->String("weapon");
            if(!std::strcmp(weapon,"hegrenade") || !std::strcmp(weapon,"inferno"))EventLog(options,4,text);
        }else if(victim==local){
            char text[192];std::snprintf(text,sizeof text,"Hurt by player #%d for %d damage (%d HP remaining)",attacker,event->Int("dmg_health"),event->Int("health"));EventLog(options,3,text);
        }
    }else if(!std::strcmp(name,"item_purchase")){
        const int buyer=User(event->Int("userid"));if(buyer<=0)return;
        const int team=event->Int("team");
        const bool selected=(options.buy_targets&8u) || (buyer==local ? (options.buy_targets&4u)!=0 : ctx.local && (options.buy_targets&(team==ctx.local->m_iTeamNum() ? 1u : 2u))!=0);
        if(selected){char text[192];std::snprintf(text,sizeof text,"Player #%d bought %.96s",buyer,event->String("weapon"));EventLog(options,2,text);}
    }else if(!std::strcmp(name,"player_death")){
        if(User(event->Int("attacker"))==local && User(event->Int("userid"))!=local && options.kill_enabled)Sound(options.kill_sound);
    }
}
class Listener {
public:
    virtual ~Listener()=default;
    virtual void FireGameEvent(Event* event){__try{OnEvent(event);}__except(EXCEPTION_EXECUTE_HANDLER){static bool logged=false;if(!logged){logged=true;LOG_WARN("misc: rejected event data");}}}
    virtual int GetEventDebugID(){return 42;}
} listener;
void ClearMessages(){std::lock_guard<std::mutex> lock(message_mutex);messages={};}
void RestoreRadar(){
    for(int i=1;i<=64;++i){auto& record=radar[i];
        if(record.written && spotted>0 && ctx.in_game && Interfaces::EntityList && Interfaces::EntityList->GetClientEntity(i)==record.entity)
            *reinterpret_cast<bool*>(static_cast<unsigned char*>(record.entity)+spotted)=record.original;
        record={};
    }
}
VMT_Hook datagram_hook;void* ping_channel{};SequenceHistory sequence_history;std::mutex sequence_mutex;
using DatagramFn=int(__thiscall*)(void*,void*);DatagramFn datagram_original{};
int SendWithSequence(void* channel,void* data,const SequenceHistory::Entry& entry){
    auto* sequence=reinterpret_cast<int*>(static_cast<unsigned char*>(channel)+0x1c);
    auto* reliable=reinterpret_cast<int*>(static_cast<unsigned char*>(channel)+0x28);
    const int old_sequence=*sequence,old_reliable=*reliable;int result{};
    __try{*sequence=entry.sequence;*reliable=entry.reliable;result=datagram_original(channel,data);}
    __finally{*sequence=old_sequence;*reliable=old_reliable;}
    return result;
}
int __fastcall SendDatagram(void* channel,void*,void* data){
    const auto original=datagram_original;if(!original)return 0;
    const auto options=Get();if(channel!=ping_channel || data || !options.ping_spike || !Interfaces::Engine || !Interfaces::Engine->IsInGame()){PingApplied(false);return original(channel,data);}
    const auto loopback=MEM::GetVFunc<bool(__thiscall*)(void*)>(channel,6);if(loopback(channel)){PingApplied(false);return original(channel,data);}
    const auto latency=MEM::GetVFunc<float(__thiscall*)(void*,int)>(channel,9);
    const float existing=latency(channel,0)+latency(channel,1),max_unlag=ConVarFloat("sv_maxunlag",.2f);
    const float delay=std::clamp(options.ping_amount*.001f,0.f,(std::max)(0.f,max_unlag-existing));
    SequenceHistory::Entry selected;bool found{};
    {std::unique_lock<std::mutex> lock(sequence_mutex,std::try_to_lock);if(lock.owns_lock())found=sequence_history.Select(static_cast<double>(GetTickCount64())*.001,delay,selected);}
    PingApplied(found);
    return found ? SendWithSequence(channel,data,selected) : original(channel,data);
}
bool PingEngineSupported(){
    static int supported=-1;if(supported!=-1)return supported==1;
    supported=0;auto* base=static_cast<unsigned char*>(MEM::GetModuleBase("engine.dll"));if(!base)return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>0x1000)return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    supported=nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386 && nt->FileHeader.TimeDateStamp==0x64ee7e86u && nt->OptionalHeader.SizeOfImage==0x39c0000u ? 1 : 0;
    if(!supported)LOG_WARN("misc ping: unsupported engine build; channel hook disabled");
    return supported==1;
}
void UpdatePing(){
    if(!Get().ping_spike || !ctx.in_game)PingApplied(false);
    if(!PingEngineSupported())return;
    void* channel=ctx.in_game && Interfaces::Engine ? MEM::GetVFunc<void*(__thiscall*)(void*)>(Interfaces::Engine,78)(Interfaces::Engine) : nullptr;
    if(channel!=ping_channel){datagram_hook.detach();{std::lock_guard<std::mutex> lock(sequence_mutex);sequence_history.Reset();}ping_channel=channel;
        if(channel){datagram_original=MEM::GetVFunc<DatagramFn>(channel,46);if(!datagram_hook.attach(channel,46,reinterpret_cast<void*>(&SendDatagram)))ping_channel=nullptr;}
    }
    if(!channel || channel!=ping_channel)return;
    const int sequence=*reinterpret_cast<int*>(static_cast<unsigned char*>(channel)+0x1c),reliable=*reinterpret_cast<int*>(static_cast<unsigned char*>(channel)+0x28);
    std::lock_guard<std::mutex> lock(sequence_mutex);sequence_history.Record(sequence,reliable,static_cast<double>(GetTickCount64())*.001);
}
using SetClanTagFn=void(__fastcall*)(const char*,const char*);
SetClanTagFn set_clan_tag{};
bool clan_changed{};unsigned clan_frame=~0u;char original_clan[16]{};
void ResolveClanTag(){
    const auto at=MEM::FindPattern("engine.dll","53 56 57 8B DA 8B F9 FF 15 ? ? ? ? 6A 2C 8B C8 8B 30 FF 56 08 85 C0 74 13 51 6A 00 68 ? ? ? ? 8B C8 E8 ? ? ? ? 8B F0 EB 02 33 F6 6A 01 68");
    if(!at){LOG_WARN("misc clantag: engine fingerprint not found");return;}
    std::uint32_t event{},tag{},name{};std::memcpy(&event,reinterpret_cast<void*>(at+30),4);std::memcpy(&tag,reinterpret_cast<void*>(at+50),4);std::memcpy(&name,reinterpret_cast<void*>(at+76),4);
    if(std::strcmp(reinterpret_cast<char*>(event),"ClanTagChanged") || std::strcmp(reinterpret_cast<char*>(tag),"tag") || std::strcmp(reinterpret_cast<char*>(name),"name")){LOG_WARN("misc clantag: message identity mismatch");return;}
    set_clan_tag=reinterpret_cast<SetClanTagFn>(at);
}
bool CaptureClan(){
    original_clan[0]=0;const auto offset=PropManager::Get().GetOffset("DT_CSPlayerResource","m_szClan");
    if(offset<=0 || offset>=0x10000)return false;
    for(int i=1;i<2048;++i){auto* entity=static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(i));if(!entity)continue;
        auto* cls=entity->GetClientClass();if(!cls || !cls->m_pNetworkName || std::strcmp(cls->m_pNetworkName,"CCSPlayerResource"))continue;
        std::memcpy(original_clan,reinterpret_cast<unsigned char*>(entity)+offset+Interfaces::Engine->GetLocalPlayer()*16,15);original_clan[15]=0;return true;
    }
    return false;
}
void UpdateClan(bool enabled){
    if(!set_clan_tag)return;
    if(!ctx.in_game){clan_changed=false;clan_frame=~0u;return;}
    if(!enabled){if(clan_changed)set_clan_tag(original_clan,original_clan);clan_changed=false;clan_frame=~0u;return;}
    if(!clan_changed){static ULONGLONG next_attempt{};const auto now=GetTickCount64();if(now<next_attempt)return;next_attempt=now+1000;if(!CaptureClan())return;}
    const auto frame=static_cast<unsigned>(GetTickCount64()/300)%30;
    if(frame==clan_frame)return;
    const auto text=ClanFrame(frame);set_clan_tag(text.c_str(),text.c_str());clan_changed=true;clan_frame=frame;
}
void* ragdoll_cvar{};float ragdoll_original{},ragdoll_applied{};bool ragdoll_changed{};
void UpdateRagdoll(const Settings& options){
    if(!ragdoll_cvar && Interfaces::ConVar)ragdoll_cvar=MEM::GetVFunc<void*(__thiscall*)(void*,const char*)>(Interfaces::ConVar,14)(Interfaces::ConVar,"cl_ragdoll_gravity");
    if(!ragdoll_cvar)return;
    const auto get=MEM::GetVFunc<float(__thiscall*)(void*)>(ragdoll_cvar,12);const auto set=MEM::GetVFunc<void(__thiscall*)(void*,float)>(ragdoll_cvar,15);
    if(!options.ragdoll_gravity){if(ragdoll_changed && std::abs(get(ragdoll_cvar)-ragdoll_applied)<.01f)set(ragdoll_cvar,ragdoll_original);ragdoll_changed=false;return;}
    if(!ragdoll_changed)ragdoll_original=get(ragdoll_cvar);
    const float value=ragdoll_original*std::clamp(options.ragdoll_amount/100.f,.01f,10.f);
    if(!ragdoll_changed || std::abs(value-ragdoll_applied)>.01f){set(ragdoll_cvar,value);ragdoll_applied=value;ragdoll_changed=true;}
}
void InitializeExtras(){__try{ResolveClanTag();}__except(EXCEPTION_EXECUTE_HANDLER){set_clan_tag=nullptr;LOG_WARN("misc clantag: invalid fingerprint operands");}}
void ShutdownExtras(){datagram_hook.detach();ping_channel=nullptr;__try{UpdateClan(false);Settings disabled;UpdateRagdoll(disabled);}__except(EXCEPTION_EXECUTE_HANDLER){}}
void FrameImpl(){
    const auto options=Get();
    UpdateClan(options.clantag);UpdateRagdoll(options);UpdatePing();
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
void PredictMovement(CUserCmd& cmd,const Settings& options,const Physics& physics,bool ground,const Vector& position,const Vector& velocity){
    if(!(options.edge_jump || options.edge_stop || options.edge_bug || options.pixel_surf) || !Interfaces::EngineTrace)return;
    const auto duck_offset=PropManager::Get().GetOffset("DT_CSPlayer","m_flDuckAmount");
    if(duck_offset<=0 || duck_offset>=0x10000)return;
    const float duck=*reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(ctx.local)+duck_offset);
    MiscPrediction::State initial{position,velocity,ground,duck};MiscPrediction::Rules rules;
    rules.ground=physics;rules.gravity=ConVarFloat("sv_gravity",800.f);rules.air_accelerate=ConVarFloat("sv_airaccelerate",12.f);rules.jump_impulse=ConVarFloat("sv_jump_impulse",301.993f);
    auto trace=[](const Vector& start,const Vector& end,const Vector& mins,const Vector& maxs){
        const auto native=Hull(start,end,mins,maxs);
        return MiscPrediction::Trace{native.fraction,native.endpos,native.plane.normal,native.startsolid,native.allsolid};
    };
    if(ground){
        auto future=initial;MiscPrediction::Budget budget;
        const auto result=MiscPrediction::Tick(future,cmd,rules,trace,budget);
        if(result.invalid)return;
        if(result.left_ground && !(cmd.buttons&2)){
            if(options.edge_jump)MiscRuntime::EdgeJump(cmd,true,true,false);
            else if(options.edge_stop)CounterMove(cmd,velocity.x,velocity.y,physics);
        }
        return;
    }
    // Do not search routes while far from contact. One proximity sweep plus
    // a strict shared budget replaces the old unconditional 512 sweeps.
    if(!(options.edge_bug || options.pixel_surf) || velocity.z>=-40.f)return;
    const float reach=std::clamp(-velocity.z*physics.interval*4.f+4.f,8.f,64.f);
    const auto nearby=Hull(position,position-Vector{0.f,0.f,reach},Vector{-16.f,-16.f,0.f},Vector{16.f,16.f,duck>.5f ? 54.f : 72.f});
    if(nearby.fraction>=1.f || nearby.startsolid || nearby.allsolid)return;
    const auto choice=MiscPrediction::Search(initial,cmd,rules,options.edge_bug,options.pixel_surf,trace,64);
    if(!choice.found)return;
    // Receding-horizon plan: only this command is changed. Every following
    // command starts from a fresh live snapshot; never replay stale trajectories.
    cmd.forwardmove=choice.forward;cmd.sidemove=choice.side;
    if(choice.duck)cmd.buttons|=4;else cmd.buttons&=~4;
    static ULONGLONG reported{};const auto now=GetTickCount64();
    if(now-reported>2000){reported=now;LOG_INFO("misc forecast: selected %s route tick=%d traces=%d (experimental, not engine prediction)",choice.kind==2 ? "pixel-surf" : "edge-bug",choice.tick,choice.traces);}
}
void MoveImpl(CUserCmd* cmd){
    if(!cmd || !ctx.in_game || !ctx.local || !ctx.local->IsAlive()){peek_state.Reset();long_jump_state.Reset();PlayerState(0,false);movement_player=nullptr;peek_shot.store(false);return;}
    if(ctx.local!=movement_player || cmd->command_number<=last_command || cmd->command_number-last_command>128){peek_state.Reset();long_jump_state.Reset();peek_shot.store(false);}
    movement_player=ctx.local;last_command=cmd->command_number;
    const auto options=Get();PlayerState(ctx.weapon ? ctx.weapon->m_iItemDefinitionIndex() : 0,true);const auto velocity=ctx.local->m_vecVelocity();
    // Datamap entries in the supplied client.dll: m_MoveType=0x25C,
    // m_nWaterLevel=0x25E. Both are byte fields, not 32-bit NETVAR reads.
    const auto* bytes=reinterpret_cast<const unsigned char*>(ctx.local);
    const int type=bytes[0x25c],water=bytes[0x25e];const bool ground=(ctx.local->m_fFlags()&1)!=0;
    bool server_bhop=false;
    if(Interfaces::ConVar){
        static void* variable=MEM::GetVFunc<void*(__thiscall*)(void*,const char*)>(Interfaces::ConVar,14)(Interfaces::ConVar,"sv_autobunnyhopping");
        if(variable)server_bhop=MEM::GetVFunc<int(__thiscall*)(void*)>(variable,13)(variable)!=0;
    }
    auto physics=CapturePhysics();
    if(type==9 && options.ladder_jump && water<2 && velocity.z>0.f && cmd->forwardmove>0.f && Interfaces::EngineTrace){
        const auto position=ctx.local->m_vecOrigin();const float yaw=cmd->viewangles.y*.01745329252f;
        const Vector toward{std::cos(yaw)*32.f,std::sin(yaw)*32.f,0};
        Ray_t lower,upper;lower.Init(position+Vector{0,0,32},position+Vector{0,0,32}+toward);
        upper.Init(position+Vector{0,0,80},position+Vector{0,0,80}+toward);
        CTraceFilterSimple filter(ctx.local);CGameTrace low,high;
        Interfaces::EngineTrace->TraceRay(lower,0x20000000u,&filter,&low);Interfaces::EngineTrace->TraceRay(upper,0x20000000u,&filter,&high);
        if(low.fraction<1.f && high.fraction==1.f && !high.startsolid && !high.allsolid)cmd->buttons|=2;
    }
    if(!Walkable(type,water)){peek_state.Reset();long_jump_state.Reset();peek_shot.store(false);return;}
    Move(*cmd,options,ground,type,water,velocity.x,velocity.y,server_bhop,physics);
    const auto position=ctx.local->m_vecOrigin();
    const bool shot=peek_shot.exchange(false);
    peek_state.Update(*cmd,options.peek,options.retreat_on_release,ground,position,velocity,shot,physics,options.peek_distance);
    if(Interfaces::EngineTrace){
        if(peek_state.returning && ground){
            const float yaw=cmd->viewangles.y*.01745329252f,c=std::cos(yaw),sn=std::sin(yaw);
            const Vector direction{cmd->forwardmove*c+cmd->sidemove*sn,cmd->forwardmove*sn-cmd->sidemove*c,0};
            const float length=direction.Length2D();
            if(length>.1f){const auto trace=Hull(position+Vector{0,0,2},position+Vector{0,0,2}+direction*((std::min)(24.f,length*physics.interval)/length),{-16,-16,0},{16,16,54});
                if(trace.startsolid || trace.allsolid || trace.fraction<.15f){cmd->forwardmove=cmd->sidemove=0.f;}
                else if(trace.fraction<1.f){
                    const float dot=direction.x*trace.plane.normal.x+direction.y*trace.plane.normal.y;
                    const float x=direction.x-(std::min)(dot,0.f)*trace.plane.normal.x,y=direction.y-(std::min)(dot,0.f)*trace.plane.normal.y;
                    cmd->forwardmove=std::clamp(x*c+y*sn,-450.f,450.f);cmd->sidemove=std::clamp(x*sn-y*c,-450.f,450.f);
                }
            }
        }
        if(options.no_fall && !ground){
            Vector start=position;if(!(ctx.local->m_fFlags()&2))start.z+=9.f;
            const auto floor=Hull(start,start-Vector{0,0,1000},{-16,-16,0},{16,16,54},1u,true);
            if(floor.fraction<1.f && !floor.startsolid && !floor.allsolid && floor.plane.normal.z>=.7f)
                FallButtons(*cmd,floor.fraction*1000.f,velocity.z,ConVarFloat("sv_gravity",800.f),physics.interval);
        }
    }
    if(options.avoid_walls && options.strafe && !ground && Walkable(type,water) && Interfaces::EngineTrace && velocity.Length2D()>20.f){
        const auto start=ctx.local->m_vecOrigin()+Vector{0,0,2};
        const auto trace=Hull(start,start+velocity*.1f,{-16,-16,0},{16,16,54});
        if(trace.fraction<1.f && !trace.startsolid && trace.plane.normal.z<.7f){
            const float yaw=cmd->viewangles.y*.01745329252f,c=std::cos(yaw),sn=std::sin(yaw);
            float x=cmd->forwardmove*c+cmd->sidemove*sn,y=cmd->forwardmove*sn-cmd->sidemove*c;
            const float dot=x*trace.plane.normal.x+y*trace.plane.normal.y;
            if(dot<0.f){x-=dot*trace.plane.normal.x;y-=dot*trace.plane.normal.y;cmd->forwardmove=std::clamp(x*c+y*sn,-450.f,450.f);cmd->sidemove=std::clamp(x*sn-y*c,-450.f,450.f);}
        }
    }
    PredictMovement(*cmd,options,physics,ground,position,velocity);
    if(!options.edge_bug && !options.pixel_surf && !options.no_fall)long_jump_state.Update(*cmd,options.long_jump,ground,velocity.z);else long_jump_state.Reset();
}
}
void Initialize(){
    InitializeExtras();
    if(!accept_receiver)InstallAccept();
    if(registered || !Interfaces::GameEvent)return;
    auto add=MEM::GetVFunc<bool(__thiscall*)(void*,void*,const char*,bool)>(Interfaces::GameEvent,3);
    bool ok=true;for(const char* event:{"round_start","round_end","game_newmap","player_spawn","weapon_fire","player_hurt","player_death","item_purchase"})ok=add(Interfaces::GameEvent,&listener,event,false) && ok;
    if(!ok){MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::GameEvent,5)(Interfaces::GameEvent,&listener);LOG_WARN("misc: event listener unavailable");return;}
    registered=true;LOG_INFO("misc: event listener registered");
}
void Shutdown(){
    ShutdownExtras();
    accept_event_hook.detach();accept_destroy_hook.detach();accept_receiver=nullptr;ready=nullptr;
    peek_state.Reset();peek_shot.store(false);
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
