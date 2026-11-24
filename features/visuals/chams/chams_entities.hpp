#pragma once
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../sdk/entity/entity.hpp"
#include "chams_object_names.hpp"
#include <cstring>
#include <string_view>
namespace ChamsObjects {
enum Category { Weapon, Grenade, C4, None };
using ChamsObjectNames::Projectile;
inline Category Classify(std::string_view name) {
    if(name=="CPlantedC4" || name=="CC4")return C4;
    if(Projectile(name) || name=="CFlashbang")return Grenade;
    if(name.substr(0,7)=="CWeapon" || name=="CAK47" || name=="CDEagle" || name=="CKnife" || name=="CSmokeGrenade" || name=="CHEGrenade" || name=="CMolotovGrenade" || name=="CIncendiaryGrenade" || name=="CDecoyGrenade")return Weapon;
    return None;
}
}

namespace ChamsEntities {
inline const char* Class(CBaseEntity* e){auto* c=e ? e->GetClientClass() : nullptr;return c ? c->m_pNetworkName : nullptr;}
inline bool Is(CBaseEntity* e,const char* name){const auto* c=Class(e);return c && std::strcmp(c,name)==0;}
inline CBaseEntity* Handle(CBaseEntity* e,std::ptrdiff_t offset){
    if(!e || offset<=0 || offset>=0x10000)return nullptr;
    const auto h=*reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const unsigned char*>(e)+offset);
    return h && h!=0xffffffffu ? static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntityFromHandle(h)) : nullptr;
}
inline CBaseEntity* RagdollPlayer(CBaseEntity* rag){
    static const auto player=PropManager::Get().GetOffset("DT_CSRagdoll","m_hPlayer");
    if(auto* owner=Handle(rag,player);Is(owner,"CCSPlayer"))return owner;
    static const auto rag_handle=PropManager::Get().GetOffset("DT_CSPlayer","m_hRagdoll");
    if(!rag_handle)return nullptr;
    for(int i=1;i<=64;++i){
        auto* p=static_cast<CBaseEntity*>(Interfaces::EntityList->GetClientEntity(i));
        if(p && Handle(p,rag_handle)==rag && Is(p,"CCSPlayer"))return p;
    }
    return nullptr;
}
inline int RagdollTeam(CBaseEntity* rag){
    static const auto team=PropManager::Get().GetOffset("DT_CSRagdoll","m_iTeamNum");
    if(team>0 && team<0x10000)return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(rag)+team);
    return rag->m_iTeamNum();
}
inline CBaseEntity* AttachmentPlayer(CBaseEntity* entity){
    static const auto owner=PropManager::Get().GetOffset("DT_BaseEntity","m_hOwnerEntity");
    static const auto parent=PropManager::Get().GetOffset("DT_BaseWeaponWorldModel","m_hCombatWeaponParent");
    static const auto view_owner=PropManager::Get().GetOffset("DT_BaseViewModel","m_hOwner");
    static const auto move_parent=PropManager::Get().GetOffset("DT_BaseEntity","moveparent");
    CBaseEntity* visited[5]{};visited[0]=entity;
    for(int depth=0;depth<4 && entity;++depth){
        CBaseEntity* next=nullptr;
        if(Is(entity,"CBaseWeaponWorldModel"))next=Handle(entity,parent);
        if(!next && Is(entity,"CBaseViewModel"))next=Handle(entity,view_owner);
        if(!next)next=Handle(entity,owner);
        if(!next)next=Handle(entity,move_parent);
        if(!next)return nullptr;
        if(Is(next,"CCSPlayer"))return next;
        for(int i=0;i<=depth;++i)if(visited[i]==next)return nullptr;
        visited[depth+1]=next;entity=next;
    }
    return nullptr;
}
}
