#include "shot_chams.hpp"
#include "../../sdk/visual_runtime_state.hpp"
#include "material_renderer.hpp"
#include "chams_entities.hpp"
#include "../../hooks/hooks.hpp"
#include "../../context.hpp"
#include "../../sdk/entity/player.hpp"
#include "../../sdk/model_preview_render.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include <array>
#include <cstring>
#include <cmath>
namespace ShotChams {
namespace {
// CS:GO IGameEvent / IGameEventManager2 ABI, matching Fatality's SDK.
struct Event {
    const char* Name(){return MEM::GetVFunc<const char*(__thiscall*)(void*)>(this,1)(this);}
    int Int(const char* key){return MEM::GetVFunc<int(__thiscall*)(void*,const char*,int)>(this,6)(this,key,0);}
};
struct Record {
    bool valid{};int index{};void* entity{};float time{};
    DrawModelState_t state;ModelRenderInfo_t info;
    matrix3x4_t world{},lighting{};Vector light_origin{};
    bool has_lighting{},has_light_origin{};
    std::array<matrix3x4_t,256> bones{};
};
std::array<Record,65> latest;
std::array<Record,8> ghosts;
std::size_t next_ghost{};
float last_clock{};
bool registered{};
void* active_context{};
float Now(){return Interfaces::Globals ? Interfaces::Globals->curtime : 0.f;}
int User(int id){return MEM::GetVFunc<int(__thiscall*)(void*,int)>(Interfaces::Engine,9)(Interfaces::Engine,id);}
void Clear(){for(auto& r:latest)r.valid=false;for(auto& r:ghosts)r.valid=false;next_ghost=0;}
void Add(int index){
    if(index<1 || index>64)return;
    const auto& record=latest[index];const float now=Now();
    if(!record.valid || !ChamsPasses::FreshSnapshot(record.time,now))return;
    for(const auto& g:ghosts)if(g.valid && g.entity==record.entity && now-g.time<.10f)return;
    auto& ghost=ghosts[next_ghost++%ghosts.size()];ghost=record;ghost.time=now;
}
void OnEvent(Event* event){
    if(!event || !ctx.in_game || !ctx.local || !Interfaces::Engine || !Interfaces::EntityList)return;
    const auto options=VisualRuntime::GetEffects();
    if(!options.chams_enabled || !options.shot.kind){Clear();return;}
    const char* name=event->Name();if(!name)return;
    if(!std::strcmp(name,"round_start") || !std::strcmp(name,"game_newmap")){Clear();return;}
    if(!std::strcmp(name,"player_hurt")){
        if(User(event->Int("attacker"))==Interfaces::Engine->GetLocalPlayer())Add(User(event->Int("userid")));
    }else if(!std::strcmp(name,"weapon_fire") && User(event->Int("userid"))==Interfaces::Engine->GetLocalPlayer() && Interfaces::EngineTrace){
        // There is no rage shot/lag-record pipeline in this project yet. Use
        // the confirmed local fire event + current crosshair trace, with hurt
        // confirmation as a second route, never an enemy-fire timer.
        QAngle angles;Interfaces::Engine->GetViewAngles(angles);
        constexpr float rad=.01745329252f;
        const Vector direction{std::cos(angles.x*rad)*std::cos(angles.y*rad),std::cos(angles.x*rad)*std::sin(angles.y*rad),-std::sin(angles.x*rad)};
        Ray_t ray;const auto eye=ctx.local->GetEyePosition();ray.Init(eye,eye+direction*8192.f);
        CTraceFilterSimple filter(ctx.local);CGameTrace trace;
        Interfaces::EngineTrace->TraceRay(ray,0x4600400Bu,&filter,&trace);
        if(trace.m_pEnt)for(int i=1;i<=64;++i)if(latest[i].valid && latest[i].entity==trace.m_pEnt){Add(i);break;}
    }
}
class Listener {
public:
    virtual ~Listener()=default;
    virtual void FireGameEvent(Event* event){__try {OnEvent(event);}__except(EXCEPTION_EXECUTE_HANDLER){Clear();}}
    virtual int GetEventDebugID(){return 42;}
} listener;
void DrawGhosts(){
    if(!ctx.in_game || !ctx.local || !Interfaces::EntityList || !Interfaces::ModelRender || !Interfaces::MaterialSystem){Clear();return;}
    const auto options=VisualRuntime::GetEffects();const float now=Now();
    if(!options.chams_enabled || !options.shot.kind || now<last_clock){Clear();last_clock=now;return;}
    last_clock=now;
    if(std::none_of(ghosts.begin(),ghosts.end(),[](const Record& r){return r.valid;}))return;
    auto original=Hooks::GetDrawModelExecuteOriginal();if(!original || Interfaces::ModelRender->IsForcedMaterialOverride())return;
    auto* context=MEM::GetVFunc<SourceModelPreview::MatRenderContextABI*(__thiscall*)(void*)>(Interfaces::MaterialSystem,115)(Interfaces::MaterialSystem);
    if(!context)return;
    active_context=context;
    for(auto& ghost:ghosts){
        if(!ghost.valid)continue;
        const float age=now-ghost.time;
        auto* entity=static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(ghost.index));
        if(age<0.f || age>=1.f || entity!=ghost.entity || !entity || entity->GetModel()!=ghost.info.model){ghost.valid=false;continue;}
        ghost.state.model_to_world=&ghost.world;ghost.state.decals=nullptr;
        ghost.info.model_to_world=&ghost.world;
        ghost.info.lightning_offset=ghost.has_lighting ? &ghost.lighting : nullptr;
        ghost.info.lightning_origin=ghost.has_light_origin ? &ghost.light_origin : nullptr;
        auto material=options.shot;
        const float fade=ChamsPasses::GhostFade(age);
        auto alpha=[&](std::uint32_t c){return (c&0xffffffu)|(static_cast<std::uint32_t>(float((c>>24)&255)*fade)<<24);};
        material.primary=alpha(material.primary);material.hidden=alpha(material.hidden);
        PreviewMaterial::Draw(material,true,[&](){original(Interfaces::ModelRender,context,ghost.state,ghost.info,ghost.bones.data());});
    }
    context->Release();active_context=nullptr;
}
}
void Initialize(){
    if(registered || !Interfaces::GameEvent)return;
    auto add=MEM::GetVFunc<bool(__thiscall*)(void*,void*,const char*,bool)>(Interfaces::GameEvent,3);
    bool ok=true;for(const char* event:{"weapon_fire","player_hurt","round_start","game_newmap"})ok=add(Interfaces::GameEvent,&listener,event,false) && ok;
    if(!ok){MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::GameEvent,5)(Interfaces::GameEvent,&listener);LOG_WARN("on-shot chams: event listener unavailable");return;}
    registered=true;LOG_INFO("on-shot chams: local fire/hurt listener registered");
}
void Shutdown(){
    if(registered && Interfaces::GameEvent)MEM::GetVFunc<void(__thiscall*)(void*,void*)>(Interfaces::GameEvent,5)(Interfaces::GameEvent,&listener);
    registered=false;Clear();
}
void Capture(const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4_t* bones){
    if(!VisualRuntime::shot_requested.load(std::memory_order_relaxed) || (info.flags&0x60000000) || (Interfaces::ModelRender && Interfaces::ModelRender->IsForcedMaterialOverride()))return;
    const auto options=VisualRuntime::GetEffects();
    if(!options.chams_enabled || !options.shot.kind || !ctx.in_game || !ctx.local || !bones || !state.studio_hdr || info.entity_index<1 || info.entity_index>64 || !Interfaces::EntityList)return;
    auto* entity=static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(info.entity_index));
    if(!ChamsEntities::Is(entity,"CCSPlayer") || entity->m_iTeamNum()==ctx.local->m_iTeamNum() || entity->GetModel()!=info.model)return;
    const int count=state.studio_hdr->bones;if(count<1 || count>256)return;
    auto& record=latest[info.entity_index];record.valid=false;record.entity=entity;record.index=info.entity_index;record.time=Now();record.state=state;record.info=info;
    const auto* world=info.model_to_world ? info.model_to_world : state.model_to_world;if(!world)return;
    record.world=*world;record.has_lighting=info.lightning_offset!=nullptr;record.has_light_origin=info.lightning_origin!=nullptr;
    if(record.has_lighting)record.lighting=*info.lightning_offset;
    if(record.has_light_origin)record.light_origin=*info.lightning_origin;
    std::memcpy(record.bones.data(),bones,sizeof(matrix3x4_t)*static_cast<std::size_t>(count));record.valid=true;
}
void SceneEnd(){
    __try {DrawGhosts();}
    __except(EXCEPTION_EXECUTE_HANDLER){
        __try {PreviewMaterial::Restore();if(active_context)static_cast<SourceModelPreview::MatRenderContextABI*>(active_context)->Release();}__except(EXCEPTION_EXECUTE_HANDLER){}
        active_context=nullptr;Clear();static bool logged=false;if(!logged){logged=true;LOG_WARN("on-shot chams: stale snapshot rejected");}
    }
}
}
