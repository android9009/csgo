#include "../../sdk/visual_runtime_state.hpp"
#include "../../sdk/esp_projection.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../context.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include <cstdio>
#include <cstring>
#include <windows.h>
namespace VisualRuntime {
namespace {
void CaptureImpl() {
    static Players output;output.count=0;
    const auto config=Get();bool any=false;for(const auto& style:config.esp)any=any || style.show[Enabled];
    if(!any || !ctx.in_game || !ctx.local || !Interfaces::Engine || !Interfaces::EntityList) {ClearPlayers();return;}
    std::array<float,16> matrix{};
    const auto base=reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    const auto offset=offsets::Get("dwViewMatrix");
    if(!base || !offset){ClearPlayers();return;}
    std::memcpy(matrix.data(),reinterpret_cast<void*>(base+offset),sizeof(float)*16);
    auto project=[&](const Vector& p,VisualPreview::Point& result) {
        const auto clip=EspProjection::Transform(matrix,{p.x,p.y,p.z});
        if(clip.w<=.01f)return false;
        result.x=.5f+clip.x/(2.f*clip.w);result.y=.5f-clip.y/(2.f*clip.w);
        return std::isfinite(result.x) && std::isfinite(result.y);
    };
    const int max_clients=(std::min)(64,Interfaces::Engine->GetMaxClients());
    const int local_team=ctx.local->m_iTeamNum();
    static const auto duck_offset=PropManager::Get().GetOffset("DT_CSPlayer","m_flDuckAmount");
    static const auto immune_offset=PropManager::Get().GetOffset("DT_CSPlayer","m_bGunGameImmunity");
    struct NameCache {void* entity{};DWORD next{};char name[128]{};};
    static std::array<NameCache,65> names;
    for(int index=1;index<=max_clients;++index) {
        auto* entity=static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(index));
        if(!entity || entity->IsDormant() || !entity->IsAlive())continue;
        const int team=entity->m_iTeamNum();if(team!=2 && team!=3)continue;
        const int subject=SubjectFor(local_team,team,entity==ctx.local);
        const auto& style=config.esp[subject];if(!style.show[Enabled])continue;
        Player p;p.subject=subject;p.health=entity->m_iHealth();p.armor=entity->m_ArmorValue();
        const auto origin=entity->m_vecOrigin();if(!origin.IsValid())continue;
        float duck=0.f;
        if(duck_offset>0 && duck_offset<0x10000)duck=*reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(entity)+duck_offset);
        if(!std::isfinite(duck))duck=0.f;duck=(std::max)(0.f,(std::min)(1.f,duck));
        std::array<EspProjection::Point,8> hull{};
        for(int corner=0;corner<8;++corner){
            const auto point=origin+Vector{corner&1 ? 16.f : -16.f,corner&2 ? 16.f : -16.f,corner&4 ? 72.f-duck*18.f : 0.f};
            hull[corner]={point.x,point.y,point.z};
        }
        EspProjection::Box bounds;
        if(!EspProjection::Bounds(matrix,hull,bounds))continue;
        p.box={bounds.left,bounds.top,bounds.right,bounds.bottom};
        auto& name=names[index];
        if(name.entity!=entity || GetTickCount()>=name.next) {
            name.entity=entity;name.next=GetTickCount()+1000;
            alignas(8) unsigned char info[512]{};
            if(Interfaces::Engine->GetPlayerInfo(index,info)) {
                std::memcpy(name.name,info+16,sizeof(name.name)-1);name.name[127]=0;
                for(char& c:name.name)if(static_cast<unsigned char>(c)<32 && c)c=' ';
            } else std::snprintf(name.name,sizeof(name.name),"Player %d",index);
        }
        std::memcpy(p.name,name.name,sizeof(p.name));
        if(auto* weapon=entity->GetActiveWeapon()) {
            p.ammo=weapon->m_iClip1();
            if(auto* data=weapon->GetWeaponData()) {
                p.max_ammo=static_cast<int>(data->max_clip1);
                const char* label=data->weapon_name;if(label && std::strncmp(label,"weapon_",7)==0)label+=7;
                if(label)std::snprintf(p.weapon,sizeof(p.weapon),"%s",label);
            }
            if(weapon->m_iItemDefinitionIndex()==31)p.flags|=1ull<<9;
            if(weapon->m_iItemDefinitionIndex()==49)p.flags|=1ull<<1;
        }
        if(entity->m_bIsScoped())p.flags|=1ull<<0;
        if(p.armor>0)p.flags|=1ull<<5;
        if(entity->m_bHasDefuser())p.flags|=1ull<<8;
        if(immune_offset>0 && immune_offset<0x10000 && *reinterpret_cast<bool*>(reinterpret_cast<unsigned char*>(entity)+immune_offset))p.flags|=1ull<<10;
        if(style.show[Skeleton]) {
            auto* hdr=entity->GetStudioHdr();auto* bones=entity->GetBoneMatrix();
            if(hdr && bones && hdr->bones>0 && hdr->bones<=256)for(int b=0;b<hdr->bones && p.segments<64;++b) {
                auto* bone=hdr->bone(b);if(!bone || !(bone->flags&0x100) || bone->parent<0 || bone->parent>=hdr->bones)continue;
                Segment segment;
                if(project(bones[b].GetOrigin(),segment.a) && project(bones[bone->parent].GetOrigin(),segment.b))p.skeleton[p.segments++]=segment;
            }
        }
        output.items[output.count++]=p;
    }
    PublishPlayers(output);
}
}
void CapturePlayers() {
    __try {CaptureImpl();}
    __except(EXCEPTION_EXECUTE_HANDLER) {
        ClearPlayers();static bool logged=false;if(!logged){logged=true;LOG_WARN("player ESP: rejected entity/bone read; cleared frame");}
    }
}
}
