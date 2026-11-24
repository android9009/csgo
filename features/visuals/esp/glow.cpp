#include "../../../sdk/visual_runtime_state.hpp"
#include "../../../sdk/glow.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../context.hpp"
#include "../../../utils/log.hpp"
#include <cstring>
namespace VisualRuntime {
namespace {
struct Backup {int index{};void* entity{};float r{},g{},b{},a{};bool occluded{},visible{},bloom{};};
std::array<Backup,4096> backups;
int saved{};IGlowObjectManager* manager{};CGlowObjectDefinition* storage{};
std::atomic<DWORD> game_thread{0};
std::atomic<bool> cleaned{false};
void RestoreEntry(const Backup& b){
    if(!Interfaces::EntityList || !manager)return;
    auto* item=manager->GetGlowObject(b.index);
    if(!item || item->m_nNextFreeSlot!=-2 || item->m_pEntity!=b.entity || !static_cast<CBaseEntity*>(item->m_pEntity)->GetClientClass())return;
    if(!reinterpret_cast<const Vector*>(static_cast<const unsigned char*>(item->m_pEntity)+offsets::netvars::m_vecOrigin)->IsValid())return;
    item->r=b.r;item->g=b.g;item->b=b.b;item->a=b.a;
    item->m_bRenderWhenOccluded=b.occluded;item->m_bRenderWhenUnoccluded=b.visible;item->m_bFullBloom=b.bloom;
}
void RestoreImpl(){
    auto* current=Interfaces::GlowManager;
    if(current && current==manager && current->Objects()==storage)for(int i=0;i<saved;++i)RestoreEntry(backups[i]);
    saved=0;manager=nullptr;storage=nullptr;
}
void Restore(){__try{RestoreImpl();}__except(EXCEPTION_EXECUTE_HANDLER){saved=0;manager=nullptr;storage=nullptr;}}
void ApplyEntry(int i,const Effects& config){
        auto& item=storage[i];if(item.m_nNextFreeSlot!=-2 || !item.m_pEntity)return;
        auto* entity=static_cast<CBaseEntity*>(item.m_pEntity);
        auto* cls=entity->GetClientClass();if(!cls || !cls->m_pNetworkName || !reinterpret_cast<const Vector*>(reinterpret_cast<const unsigned char*>(entity)+offsets::netvars::m_vecOrigin)->IsValid())return;
        Highlight highlight;
        if(std::strcmp(cls->m_pNetworkName,"CCSPlayer")==0) {
            if(*reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(entity)+offsets::netvars::m_iHealth)<=0)return;
            const int subject=SubjectFor(*reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(ctx.local)+offsets::netvars::m_iTeamNum),*reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(entity)+offsets::netvars::m_iTeamNum),entity==ctx.local);
            highlight=config.player_glow[subject];
        } else return;
        if(!highlight.enabled)return;
        if(static_cast<std::size_t>(saved)>=backups.size())return;
        // Recheck ownership immediately before touching the engine entry.
        if(item.m_nNextFreeSlot!=-2 || item.m_pEntity!=entity || Interfaces::GlowManager!=manager || !Interfaces::EntityList)return;
        backups[saved++]={i,item.m_pEntity,item.r,item.g,item.b,item.a,item.m_bRenderWhenOccluded,item.m_bRenderWhenUnoccluded,item.m_bFullBloom};
        const auto rgba=highlight.color;item.r=float((rgba>>16)&255)/255.f;item.g=float((rgba>>8)&255)/255.f;
        item.b=float(rgba&255)/255.f;item.a=float((rgba>>24)&255)/255.f;
        item.m_bRenderWhenOccluded=true;item.m_bRenderWhenUnoccluded=true;item.m_bFullBloom=false;
}
void UpdateImpl(const Effects& config) {
    Restore();
    if(glow_closing.load()){cleaned.store(true);return;}
    if(!ctx.in_game || !ctx.local || !Interfaces::GlowManager || !Interfaces::EntityList)return;
    bool any=false;for(int i=0;i<3;++i)any=any || config.player_glow[i].enabled;
    if(!any)return;
    manager=Interfaces::GlowManager;storage=manager->Objects();
    const int count=manager->GetGlowObjectCount();
    if(!storage || count<0 || count>4096){Restore();return;}
    for(int i=0;i<count;++i)ApplyEntry(i,config);

}
}
void UpdateGlow() {
    game_thread.store(GetCurrentThreadId());
    const auto config=GetEffects();
    __try {UpdateImpl(config);}
    __except(EXCEPTION_EXECUTE_HANDLER) {
        __try {Restore();}__except(EXCEPTION_EXECUTE_HANDLER){saved=0;}
        static bool logged=false;if(!logged){logged=true;LOG_WARN("glow: ABI/entity read rejected; no invalid entry retained");}
    }
}
void RestoreGlow(){Restore();}
void ShutdownGlow(){
    glow_closing.store(true);
    if(game_thread.load()==GetCurrentThreadId()){UpdateGlow();return;}
    if(!game_thread.load())return;
    const auto deadline=GetTickCount64()+250;
    while(!cleaned.load() && GetTickCount64()<deadline)Sleep(1);
    if(!cleaned.load())LOG_WARN("glow: game-thread cleanup unavailable; refusing off-thread engine writes");
}
}
