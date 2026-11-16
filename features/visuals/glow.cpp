#include "../../sdk/visual_runtime_state.hpp"
#include "../../sdk/world_esp_state.hpp"
#include "../../sdk/glow.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../context.hpp"
#include "../../utils/log.hpp"
#include <cstring>
namespace VisualRuntime {
namespace {
struct Backup {int index{};void* entity{};float r{},g{},b{},a{};bool occluded{},visible{},bloom{};};
std::array<Backup,4096> backups;
int saved{};IGlowObjectManager* manager{};CGlowObjectDefinition* storage{};
std::atomic<DWORD> game_thread{0};
std::atomic<bool> cleaned{false};
void Restore() {
    auto* current=Interfaces::GlowManager;
    if(current && current==manager && current->Objects()==storage)for(int i=0;i<saved;++i) {
        const auto& b=backups[i];auto* item=current->GetGlowObject(b.index);
        if(!item || item->m_nNextFreeSlot!=-2 || item->m_pEntity!=b.entity)continue;
        item->r=b.r;item->g=b.g;item->b=b.b;item->a=b.a;
        item->m_bRenderWhenOccluded=b.occluded;item->m_bRenderWhenUnoccluded=b.visible;item->m_bFullBloom=b.bloom;
    }
    saved=0;manager=nullptr;storage=nullptr;
}
void UpdateImpl() {
    Restore();
    if(glow_closing.load()){cleaned.store(true);return;}
    if(!ctx.in_game || !ctx.local || !Interfaces::GlowManager)return;
    const auto config=GetEffects();
    bool any=false;for(int i=0;i<3;++i)any=any || config.player_glow[i].enabled || config.object_glow[i].enabled;
    if(!any)return;
    manager=Interfaces::GlowManager;storage=manager->Objects();
    const int count=manager->GetGlowObjectCount();
    static const auto owner_offset=PropManager::Get().GetOffset("DT_BaseEntity","m_hOwnerEntity");
    for(int i=0;i<count;++i) {
        auto& item=storage[i];if(item.m_nNextFreeSlot!=-2 || !item.m_pEntity)continue;
        auto* entity=static_cast<CBaseEntity*>(item.m_pEntity);if(entity->IsDormant())continue;
        auto* cls=entity->GetClientClass();if(!cls || !cls->m_pNetworkName)continue;
        Highlight highlight;
        if(std::strcmp(cls->m_pNetworkName,"CCSPlayer")==0) {
            if(!entity->IsAlive())continue;
            const int subject=SubjectFor(ctx.local->m_iTeamNum(),entity->m_iTeamNum(),entity==ctx.local);
            highlight=config.player_glow[subject];
        } else {
            const auto category=WorldEsp::Classify(cls->m_pNetworkName);if(category==WorldEsp::None)continue;
            if(!WorldEsp::Projectile(cls->m_pNetworkName) && std::strcmp(cls->m_pNetworkName,"CPlantedC4")!=0) {
                if(owner_offset<=0 || owner_offset>=0x10000)continue;
                const auto h=*reinterpret_cast<std::uint32_t*>(reinterpret_cast<unsigned char*>(entity)+owner_offset);
                if(h && h!=0xffffffffu && Interfaces::EntityList->GetClientEntityFromHandle(h))continue;
            }
            highlight=config.object_glow[category];
        }
        if(!highlight.enabled)continue;
        backups[saved++]={i,item.m_pEntity,item.r,item.g,item.b,item.a,item.m_bRenderWhenOccluded,item.m_bRenderWhenUnoccluded,item.m_bFullBloom};
        const auto rgba=highlight.color;item.r=float((rgba>>16)&255)/255.f;item.g=float((rgba>>8)&255)/255.f;
        item.b=float(rgba&255)/255.f;item.a=float((rgba>>24)&255)/255.f;
        item.m_bRenderWhenOccluded=true;item.m_bRenderWhenUnoccluded=true;item.m_bFullBloom=false;
    }
}
}
void UpdateGlow() {
    game_thread.store(GetCurrentThreadId());
    __try {UpdateImpl();}
    __except(EXCEPTION_EXECUTE_HANDLER) {
        __try {Restore();}__except(EXCEPTION_EXECUTE_HANDLER){saved=0;}
        static bool logged=false;if(!logged){logged=true;LOG_WARN("glow: ABI/entity read rejected; no invalid entry retained");}
    }
}
void ShutdownGlow(){
    glow_closing.store(true);
    if(game_thread.load()==GetCurrentThreadId()){UpdateGlow();return;}
    if(!game_thread.load())return;
    const auto deadline=GetTickCount64()+250;
    while(!cleaned.load() && GetTickCount64()<deadline)Sleep(1);
    if(!cleaned.load())LOG_WARN("glow: game-thread cleanup unavailable; refusing off-thread engine writes");
}
}
