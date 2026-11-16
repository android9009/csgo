#include "../../sdk/world_esp_state.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/entity/player.hpp"
#include "../../sdk/entity/weapon.hpp"
#include "../../context.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include "../../offsets/offsets.hpp"
#include <windows.h>
#include <cstring>
#include <cstdio>

namespace WorldEsp {
namespace {
Frame cached;
DWORD next_scan{};
Options previous;
int ReadInt(void* entity,std::ptrdiff_t offset) {
    return offset>0 && offset<0x10000 ? *reinterpret_cast<int*>(static_cast<unsigned char*>(entity)+offset) : 0;
}
float ReadFloat(void* entity,std::ptrdiff_t offset) {
    return offset>0 && offset<0x10000 ? *reinterpret_cast<float*>(static_cast<unsigned char*>(entity)+offset) : 0.f;
}
bool ReadBool(void* entity,std::ptrdiff_t offset) {
    return offset>0 && offset<0x10000 && *reinterpret_cast<bool*>(static_cast<unsigned char*>(entity)+offset);
}
void CaptureImpl() {
    const auto options=GetOptions();
    if(!options.Any() || !ctx.in_game || !Interfaces::EntityList || !Interfaces::Globals) {
        if(cached.valid){cached={};Publish(cached);}next_scan=0;return;
    }
    auto& net=PropManager::Get();
    static const auto owner=net.GetOffset("DT_BaseEntity","m_hOwnerEntity");
    static const auto ticking=net.GetOffset("DT_PlantedC4","m_bBombTicking");
    static const auto blow=net.GetOffset("DT_PlantedC4","m_flC4Blow");
    static const auto defuse_end=net.GetOffset("DT_PlantedC4","m_flDefuseCountDown");
    static const auto defuser=net.GetOffset("DT_PlantedC4","m_hBombDefuser");
    const auto now=GetTickCount();
    if(now>=next_scan || options.enabled!=previous.enabled) {
        cached.count=0;next_scan=now+100;previous=options;
        const int highest=(std::min)(Interfaces::EntityList->GetHighestEntityIndex(),4096);
        for(int i=1;i<=highest && cached.count<cached.items.size();++i) {
            auto* entity=static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(i));
            if(!entity || entity->IsDormant())continue;
            auto* cls=entity->GetClientClass();
            if(!cls || !cls->m_pNetworkName)continue;
            const auto category=Classify(cls->m_pNetworkName);
            if(category==None || !options.enabled[category])continue;
            const bool planted=std::strcmp(cls->m_pNetworkName,"CPlantedC4")==0;
            if(planted && !ReadBool(entity,ticking))continue;
            // Never label weapons / grenades carried by a player.
            const auto handle=static_cast<std::uint32_t>(ReadInt(entity,owner));
            if(!planted && (!owner || (handle && handle!=0xffffffffu && Interfaces::EntityList->GetClientEntityFromHandle(handle))))continue;
            const auto position=entity->m_vecOrigin();
            if(!position.IsValid() || position.IsZero())continue;
            Item item;item.category=category;item.x=position.x;item.y=position.y;item.z=position.z;
            item.planted=planted;
            const char* label=cls->m_pNetworkName;
            if(category==C4)label="C4";
            else if(std::strncmp(label,"CWeapon",7)==0)label+=7;
            else if(label[0]=='C')++label;
            std::snprintf(item.label,sizeof(item.label),"%s",label);
            if(planted) {
                item.explode_at=ReadFloat(entity,blow);item.defuse_at=ReadFloat(entity,defuse_end);
                const auto h=static_cast<std::uint32_t>(ReadInt(entity,defuser));
                item.defusing=h && h!=0xffffffffu && item.defuse_at>Interfaces::Globals->curtime;
                if(ctx.local)item.estimated_damage=BombDamage(position.Dist(ctx.local->m_vecOrigin()),ctx.local->m_ArmorValue());
            }
            cached.items[cached.count++]=item;
        }
    }
    // A single view-matrix read per rendered frame; no matrix/entity access in GUI.
    const auto base=reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    const auto matrix_offset=offsets::Get("dwViewMatrix");
    cached.valid=base && matrix_offset;
    if(cached.valid)std::memcpy(cached.matrix.data(),reinterpret_cast<void*>(base+matrix_offset),sizeof(float)*16);
    cached.time=Interfaces::Globals->curtime;Publish(cached);
}
}
void Capture() {
    __try { CaptureImpl(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        cached={};Publish(cached);next_scan=0;
        static bool logged=false;
        if(!logged){logged=true;LOG_WARN("world ESP: invalid entity read; cleared snapshot");}
    }
}
}
