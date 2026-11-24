#include "hooks.hpp"
#include "../../context.hpp"
#include "../interfaces/interfaces.hpp"
#include "../entity/entity.hpp"
#include "../visual_runtime_state.hpp"
#include "../../features/visuals/chams/chams_renderer.hpp"
#include "../../features/visuals/chams/chams_entities.hpp"
#include "../../features/visuals/chams/shot_chams.hpp"
#include "../../features/visuals/misc/other/model_transparency.hpp"
#include "../../utils/log.hpp"
#include <cstring>
namespace HooksInternal {
namespace {
int LogChamsFault(EXCEPTION_POINTERS* info){
    static bool reported=false;
    if(!reported && info && info->ExceptionRecord){
        reported=true;
        const auto* e=info->ExceptionRecord;
        LOG_ERROR("chams pass exception code=0x%08lX instruction=%p",e->ExceptionCode,e->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
bool DrawConfigured(void* ecx,void* context,const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4_t* bones) {
    if(!VisualRuntime::any_material.load(std::memory_order_relaxed) || !ctx.in_game || !ctx.local || !Interfaces::ModelInfo || !Interfaces::EntityList || !Interfaces::ModelRender ||
       Interfaces::ModelRender->IsForcedMaterialOverride())return false;
    auto original=Hooks::GetDrawModelExecuteOriginal();if(!original)return false;
    const auto settings=VisualRuntime::GetEffects();
    const char* name=info.model ? Interfaces::ModelInfo->GetModelName(info.model) : nullptr;
    if(!name)return false;
    VisualRuntime::Material choice;
    auto* entity=info.entity_index>0 ? static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(info.entity_index)) : nullptr;
    if(!entity && info.renderable){
        auto* unknown=MEM::GetVFunc<void*(__thiscall*)(void*)>(info.renderable,0)(info.renderable);
        if(unknown)entity=MEM::GetVFunc<CBaseEntity*(__thiscall*)(void*)>(unknown,7)(unknown);
    }
    const int local_team=ctx.local->m_iTeamNum();
    if(std::strstr(name,"models/weapons/v_") && std::strstr(name,"arms")) {
        if(!settings.chams_enabled)return false;choice=settings.arms;
    } else if(std::strstr(name,"models/weapons/v_")){
        if(!settings.chams_enabled)return false;choice=settings.attachments[VisualRuntime::Local];
    } else if(entity) {
        auto* cls=entity->GetClientClass();if(!cls || !cls->m_pNetworkName)return false;
        const bool player=std::strcmp(cls->m_pNetworkName,"CCSPlayer")==0;
        const bool ragdoll=std::strcmp(cls->m_pNetworkName,"CCSRagdoll")==0;
        if(player || ragdoll) {
            if(!settings.chams_enabled)return false;
            auto* owner=ragdoll ? ChamsEntities::RagdollPlayer(entity) : entity;
            const int team=owner ? owner->m_iTeamNum() : ChamsEntities::RagdollTeam(entity);
            if(team!=2 && team!=3)return false;
            const int subject=VisualRuntime::SubjectFor(local_team,team,owner==ctx.local);
            choice=ragdoll ? settings.ragdolls[subject] : info.model!=entity->GetModel() ? settings.attachments[subject] : settings.players[subject];
        } else {
            auto* owner=ChamsObjects::Projectile(cls->m_pNetworkName) ? nullptr : ChamsEntities::AttachmentPlayer(entity);
            if(owner) {
                auto* owner_class=owner->GetClientClass();
                if(!settings.chams_enabled || !owner_class || !owner_class->m_pNetworkName || std::strcmp(owner_class->m_pNetworkName,"CCSPlayer")!=0)return false;
                const int subject=VisualRuntime::SubjectFor(local_team,owner->m_iTeamNum(),owner==ctx.local);
                choice=settings.attachments[subject];
            } else {
                const auto category=ChamsObjects::Classify(cls->m_pNetworkName);
                if(category==ChamsObjects::None)return false;
                choice=settings.objects[category];
            }
        }
    } else return false;
    if(!choice.enabled || choice.kind<=0)return false;
    return PreviewMaterial::Draw(choice,true,[&](){original(ecx,context,state,info,bones);});
}
// Prop / texture transparency: record which models are props/characters and which
// materials they use. The modulation itself happens at FRAME_RENDER_START so the
// engine queues those props as translucent when it builds the render lists (the
// same effect Fatality gets from forcing RenderableInfo_t::m_nTranslucencyType).
void NoteTransparency(const DrawModelState_t& state,const ModelRenderInfo_t& info) {
    if(!VisualRuntime::any_transparency.load(std::memory_order_relaxed) || !ctx.in_game || !state.studio_hdr)return;
    if(!Interfaces::EntityList || !Interfaces::MaterialSystem)return;
    auto* entity=info.entity_index>0 ? static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(info.entity_index)) : nullptr;
    if(!entity && info.renderable && Interfaces::ModelInfo){
        auto* unknown=MEM::GetVFunc<void*(__thiscall*)(void*)>(info.renderable,0)(info.renderable);
        if(unknown)entity=MEM::GetVFunc<CBaseEntity*(__thiscall*)(void*)>(unknown,7)(unknown);
    }
    auto* cls=entity ? entity->GetClientClass() : nullptr;
    if(!cls || !cls->m_pNetworkName)return;
    const auto target=ModelTransparency::Classify(cls->m_pNetworkName);
    if(target==ModelTransparency::Target::None)return;
    ModelTransparency::Note(state.studio_hdr,target);
}
}
void __fastcall DrawModelExecute(void* ecx,void*,void* context,const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4_t* bone_to_world) {
    bool handled=false;
    __try {ShotChams::Capture(state,info,bone_to_world);handled=DrawConfigured(ecx,context,state,info,bone_to_world);NoteTransparency(state,info);}
    __except(LogChamsFault(GetExceptionInformation())) {
        __try {PreviewMaterial::Restore();}__except(EXCEPTION_EXECUTE_HANDLER){}
        static bool logged=false;if(!logged){logged=true;LOG_WARN("chams: model pass raised exception; restored material override");}
    }
    if(!handled)if(auto original=Hooks::GetDrawModelExecuteOriginal())original(ecx,context,state,info,bone_to_world);
}
}
