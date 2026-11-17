#include "engine_prediction.hpp"
#include "prediction_compression.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/entity/player.hpp"
#include "../../sdk/structs.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/detourhook.hpp"
#include "../../utils/log.hpp"
#include "../../utils/offsets/offsets.hpp"
#include "../../utils/patterns/patterns.hpp"
#include <array>
#include <cstring>
#include <cmath>

// Source 1 x86 prediction. All player-internal command/button displacements
// below are decoded from RunCommand instructions, not selected by build date.
namespace EnginePrediction {
namespace Detail {
template<class T> T Read(std::uintptr_t at){T out{};std::memcpy(&out,reinterpret_cast<void*>(at),sizeof out);return out;}
template<class T> T& Field(void* p,std::ptrdiff_t offset){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
inline bool Offset(std::ptrdiff_t n){return n>0 && n<0x10000;}
inline bool Writable(void* p,std::size_t size){
    MEMORY_BASIC_INFORMATION info{};
    if(!p || !VirtualQuery(p,&info,sizeof info) || info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto protection=info.Protect&0xff;
    return (protection==PAGE_READWRITE || protection==PAGE_WRITECOPY || protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY)
        && reinterpret_cast<std::uintptr_t>(p)+size<=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
}
using RunFn=void(__thiscall*)(void*,CCSPlayer*,CUserCmd*,IMoveHelper*);
using NetworkFn=void(__thiscall*)(void*,int);
inline VMT_Hook run_hook,network_hook;
inline RunFn run_original{};inline NetworkFn network_original{};
inline int* seed{};inline void** prediction_player{};inline IMoveHelper** helper_slot{};
inline unsigned(__fastcall* md5)(unsigned){};
inline std::ptrdiff_t current_cmd{},last_cmd{},forced{},disabled{},buttons{},last_buttons{},pressed{},released{};
inline bool resolved{},failed{};
inline const char* phase="idle";inline unsigned traced_commands{};
inline void Trace(const char* value){phase=value;if(traced_commands<2)LOG_INFO("prediction: phase=%s",value); }
struct Compressed {
    CCSPlayer* player{};int tick{},command{};std::array<Vector,4> vectors{};float fall{};
};
inline std::array<Compressed,150> compressed{};
inline std::array<std::ptrdiff_t,4> compressed_offsets{};
inline std::ptrdiff_t fall_offset{};
inline void ResolveNetvars(){
    auto& props=PropManager::Get();
    compressed_offsets={props.GetOffset("DT_BasePlayer","m_aimPunchAngle"),props.GetOffset("DT_BasePlayer","m_aimPunchAngleVel"),props.GetOffset("DT_BasePlayer","m_viewPunchAngle"),props.GetOffset("DT_BasePlayer","m_vecViewOffset[0]")};
    fall_offset=props.GetOffset("DT_BasePlayer","m_flFallVelocity");
}
inline void Store(CCSPlayer* player,CUserCmd* cmd){
    if(!player || !cmd || cmd->command_number<=0 || !player->IsAlive() || !Interfaces::EntityList)return;
    if(!Interfaces::Engine || player!=Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer()))return;
    const int tick=player->m_nTickBase();if(tick<0)return;
    auto& entry=compressed[static_cast<unsigned>(cmd->command_number)%compressed.size()];
    entry={};entry.player=player;entry.tick=tick;entry.command=cmd->command_number;
    for(std::size_t i=0;i<compressed_offsets.size();++i)if(Offset(compressed_offsets[i]))entry.vectors[i]=Field<Vector>(player,compressed_offsets[i]);
    if(Offset(fall_offset))entry.fall=Field<float>(player,fall_offset);
}
inline void Correct(int acknowledged){
    if(!Interfaces::Engine || !Interfaces::EntityList || !Interfaces::Engine->IsInGame())return;
    auto* player=static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer()));
    if(!player || !player->IsAlive() || acknowledged<=0)return;
    const auto& entry=compressed[static_cast<unsigned>(acknowledged)%compressed.size()];
    if(entry.command!=acknowledged || entry.player!=player || entry.tick!=player->m_nTickBase())return;
    for(std::size_t i=0;i<compressed_offsets.size();++i){
        if(!Offset(compressed_offsets[i]))continue;
        auto& value=Field<Vector>(player,compressed_offsets[i]);const auto& saved=entry.vectors[i];
        const float tolerance=i==3 ? .065f : .03125f;
        PredictionCompression::Restore(value,saved,tolerance);
    }
    if(Offset(fall_offset)){auto& value=Field<float>(player,fall_offset);PredictionCompression::Restore(value,entry.fall,.5f);}
}
// Source SDK ClientState command counters are three consecutive int32 fields.
// The base and delta displacement are already resolved by utils/patterns.
struct CommandCounters {int outgoing,choked,ack;};
inline std::ptrdiff_t delta{},counters{};
inline bool ResolveUpdate(){
    delta=static_cast<std::ptrdiff_t>(Patterns::Get("clientstate_delta_ticks"));
    counters=static_cast<std::ptrdiff_t>(Patterns::Get("clientstate_last_outgoing_command"));
    return Offset(delta) && Offset(counters) && Patterns::Get("clientstate_choked_commands")==static_cast<std::uintptr_t>(counters)+sizeof(int);
}
inline int Acknowledged(){return Interfaces::ClientState && Offset(counters) ? Field<CommandCounters>(Interfaces::ClientState,counters).ack : 0;}
inline void __fastcall RunCommand(void* self,void*,CCSPlayer* player,CUserCmd* cmd,IMoveHelper* helper){
    if(helper)Interfaces::MoveHelper=helper;
    run_original(self,player,cmd,helper);
    __try{Store(player,cmd);}__except(EXCEPTION_EXECUTE_HANDLER){}
}
inline void __fastcall PostNetwork(void* self,void*,int commands_acknowledged){
    network_original(self,commands_acknowledged);
    __try{
        // The argument is a count, NOT the absolute command number.
        Correct(Acknowledged());
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}
inline bool Resolve(){
    if(resolved)return true;if(failed || !Interfaces::Prediction || !Interfaces::GameMovement)return false;
    current_cmd=static_cast<std::ptrdiff_t>(offsets::Get("prediction_current_command"));
    last_cmd=static_cast<std::ptrdiff_t>(offsets::Get("prediction_last_command"));
    forced=static_cast<std::ptrdiff_t>(offsets::Get("prediction_forced_buttons"));disabled=static_cast<std::ptrdiff_t>(offsets::Get("prediction_disabled_buttons"));
    buttons=static_cast<std::ptrdiff_t>(offsets::Get("prediction_buttons"));last_buttons=static_cast<std::ptrdiff_t>(offsets::Get("prediction_last_buttons"));
    pressed=static_cast<std::ptrdiff_t>(offsets::Get("prediction_pressed_buttons"));released=static_cast<std::ptrdiff_t>(offsets::Get("prediction_released_buttons"));
    seed=reinterpret_cast<int*>(offsets::Get("prediction_random_seed"));prediction_player=reinterpret_cast<void**>(offsets::Get("prediction_player"));
    helper_slot=reinterpret_cast<IMoveHelper**>(offsets::Get("prediction_move_helper"));
    md5=reinterpret_cast<decltype(md5)>(offsets::Get("prediction_md5_fastcall"));
    if(!md5 || !helper_slot || !Offset(current_cmd) || !Offset(last_cmd) || !Offset(forced) || !Offset(disabled) || !Offset(buttons) || !Offset(last_buttons) || !Offset(pressed) || !Offset(released) || !Writable(seed,4) || !Writable(prediction_player,4)){
        failed=true;LOG_ERROR("prediction: missing native bindings in utils/offsets");return false;
    }
    ResolveNetvars();if(!ResolveUpdate()){failed=true;LOG_ERROR("prediction: ClientState bindings unavailable");return false;}
    run_original=MEM::GetVFunc<RunFn>(Interfaces::Prediction,19);network_original=MEM::GetVFunc<NetworkFn>(Interfaces::Prediction,7);
    if(!run_hook.attach(Interfaces::Prediction,19,reinterpret_cast<void*>(&RunCommand)) || !network_hook.attach(Interfaces::Prediction,7,reinterpret_cast<void*>(&PostNetwork))){run_hook.detach();network_hook.detach();failed=true;return false;}
    resolved=true;LOG_INFO("prediction: native command binding ready (current command +%X)",static_cast<unsigned>(current_cmd));return true;
}
struct Backup {
    CCSPlayer* local{};float curtime{},frametime{};int flags{},tickbase{},seed{};void* player{};CUserCmd* command{};
    CUserCmd last{};std::array<int,4> buttons{};bool in_prediction{},first{},active{},host{},tracking{};
};
inline Backup backup;
alignas(16) inline std::array<std::uint8_t,512> move_data{};
}
void End(){
    using namespace Detail;
    if(!backup.active)return;
    if(backup.tracking){backup.tracking=false;__try{MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::GameMovement,4)(Interfaces::GameMovement,backup.local);}__except(EXCEPTION_EXECUTE_HANDLER){}}
    if(backup.host){backup.host=false;__try{MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::MoveHelper,1)(Interfaces::MoveHelper,nullptr);}__except(EXCEPTION_EXECUTE_HANDLER){}}
    __try{MEM::GetVFunc<void(__thiscall*)(void*)>(Interfaces::GameMovement,2)(Interfaces::GameMovement);}
    __except(EXCEPTION_EXECUTE_HANDLER){}
    Interfaces::Globals->curtime=backup.curtime;Interfaces::Globals->frametime=backup.frametime;
    backup.local->m_fFlags()=backup.flags;backup.local->m_nTickBase()=backup.tickbase;
    *seed=backup.seed;*prediction_player=backup.player;
    Field<CUserCmd*>(backup.local,current_cmd)=backup.command;Field<CUserCmd>(backup.local,last_cmd)=backup.last;
    Field<int>(backup.local,buttons)=backup.buttons[0];Field<int>(backup.local,last_buttons)=backup.buttons[1];Field<int>(backup.local,pressed)=backup.buttons[2];Field<int>(backup.local,released)=backup.buttons[3];
    Field<bool>(Interfaces::Prediction,8)=backup.in_prediction;Field<bool>(Interfaces::Prediction,0x18)=backup.first;
    backup.active=false;Trace("restored");++traced_commands;
}
bool Start(CCSPlayer* local,CUserCmd& cmd){
    using namespace Detail;
    if(backup.active || !local || !local->IsAlive() || !Interfaces::Globals || !Resolve())return false;
    if(helper_slot && *helper_slot)Interfaces::MoveHelper=*helper_slot;
    if(!Interfaces::MoveHelper)return false;
    const float interval=Interfaces::Globals->interval_per_tick;
    if(!std::isfinite(interval) || interval<=0.f || interval>.1f)return false;
    // CL_Move owns IPrediction::Update. Re-entering its command replay from
    // ClientMode::CreateMove runs the engine's prediction pipeline recursively
    // while the new command is still being assembled. Only the explicit
    // SetupMove / ProcessMovement / FinishMove transaction belongs here.
    Trace("backup");
    backup={};backup.local=local;backup.flags=local->m_fFlags();backup.tickbase=local->m_nTickBase();
    backup.curtime=Interfaces::Globals->curtime;backup.frametime=Interfaces::Globals->frametime;backup.seed=*seed;backup.player=*prediction_player;
    backup.command=Field<CUserCmd*>(local,current_cmd);backup.last=Field<CUserCmd>(local,last_cmd);
    backup.buttons={Field<int>(local,buttons),Field<int>(local,last_buttons),Field<int>(local,pressed),Field<int>(local,released)};
    backup.in_prediction=Field<bool>(Interfaces::Prediction,8);backup.first=Field<bool>(Interfaces::Prediction,0x18);backup.active=true;
    Interfaces::Globals->curtime=static_cast<float>(backup.tickbase)*interval;Interfaces::Globals->frametime=((backup.flags&64) || Field<bool>(Interfaces::Prediction,0x0a)) ? 0.f : interval;
    cmd.random_seed=static_cast<int>(md5(static_cast<unsigned>(cmd.command_number))&0x7fffffff);*seed=cmd.random_seed;*prediction_player=local;
    Field<bool>(Interfaces::Prediction,8)=true;Field<bool>(Interfaces::Prediction,0x18)=false;
    Field<CUserCmd*>(local,current_cmd)=&cmd;Field<CUserCmd>(local,last_cmd)=cmd;
    if(Offset(forced))cmd.buttons|=Field<int>(local,forced);if(Offset(disabled))cmd.buttons&=~Field<int>(local,disabled);
    const int changed=cmd.buttons^backup.buttons[0];Field<int>(local,last_buttons)=backup.buttons[0];Field<int>(local,buttons)=cmd.buttons;
    Field<int>(local,pressed)=changed&cmd.buttons;Field<int>(local,released)=changed&~cmd.buttons;
    Trace("set_host");move_data={};backup.host=true;MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::MoveHelper,1)(Interfaces::MoveHelper,local);
    Trace("start_tracking");backup.tracking=true;MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::GameMovement,3)(Interfaces::GameMovement,local);
    Trace("setup_move");MEM::GetVFunc<void(__thiscall*)(void*,void*,void*,void*,void*)>(Interfaces::Prediction,20)(Interfaces::Prediction,local,&cmd,Interfaces::MoveHelper,move_data.data());
    Trace("process_movement");MEM::GetVFunc<void(__thiscall*)(void*,void*,void*)>(Interfaces::GameMovement,1)(Interfaces::GameMovement,local,move_data.data());
    Trace("finish_move");MEM::GetVFunc<void(__thiscall*)(void*,void*,void*,void*)>(Interfaces::Prediction,21)(Interfaces::Prediction,local,&cmd,move_data.data());
    return true;
}
const char* Phase(){return Detail::phase;}
void Reset(){Detail::compressed={};}
void Shutdown(){End();Detail::run_hook.detach();Detail::network_hook.detach();Detail::resolved=false;Detail::failed=false;Reset();}
}
