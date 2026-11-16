// Local-only SOC integration. Reference: Osiris csgo-end-of-life (MIT), see
// third_party/licenses/Osiris-inventory.txt and docs/local-inventory-status.md.
#include "local_inventory.hpp"
#include "local_inventory_ids.hpp"
#include "equipment_state.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/log.hpp"
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <map>
#include <algorithm>
#include <limits>

namespace LocalInventory {
namespace {
struct Item {
    void* vtable; void* shared_vtable;
    std::uint64_t id, original;
    void* attributes;
    std::uint32_t account, position;
    std::uint16_t definition, properties;
    std::int16_t set;
    int update_frame;
    std::uint8_t flags;
};
static_assert(sizeof(void*) == 4, "Legacy CS:GO inventory requires Win32");
static_assert(offsetof(Item,id)==8 && offsetof(Item,definition)==36 && offsetof(Item,flags)==48);
struct Owner { std::uint64_t id; std::uint32_t type, padding; };
struct Request { SkinState::Choice finish; int music{}; };
struct Owned { Item* item; std::uint64_t id; Request request; };
using Key=std::pair<int,int>; // CT/T and definition, independent selections
std::map<Key,Owned> owned;
struct Equipped {std::uint64_t original{},local{};};
std::map<Key,Equipped> equipped; // actual team/slot; only our local changes

std::mutex loadout_mutex;
Loadout loadouts[2];
DWORD last_loadout{};

void* inventory{};
void* cache{};
std::uint64_t owner_id{};
IdAllocator ids;
bool attempted{}, ready{}, stopped{};
void* manager{};
Item* (__stdcall* create_item)(){};
void* (__thiscall* create_cache)(void*,int){};
void (__thiscall* attribute)(Item*,void*,void*){};
void (__thiscall* set_name)(Item*,const char*){};
void* (__cdecl* item_system)(){};
template<class T> T& At(void* p,std::size_t offset) {return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
template<class F> F Slot(void* p,std::size_t slot) {return MEM::GetVFunc<F>(p,slot);}
void ReadLoadout(void* current) {
    if(GetTickCount()-last_loadout<250) return;
    last_loadout=GetTickCount();
    Loadout next[2];
    const auto base=reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    // Inspected CCSPlayerInventory primary vtable, slot 8 = RVA 0x6cc240.
    if(current && reinterpret_cast<std::uintptr_t>(Slot<void*>(current,8))==base+0x6cc240) {
        for(int team=0;team<2;++team) {
            for(int slot=2;slot<=38;++slot) {
                auto* view=Slot<void*(__thiscall*)(void*,int,int)>(current,8)(current,team==0?3:2,slot);
                if(!view) continue;
                // RVA 0x715880 reads the definition at view+0x1ea in this client.
                const int definition=At<std::uint16_t>(view,0x1ea);
                if(definition>0 && definition<100) next[team].definitions.insert(definition);
            }
            next[team].ready=!next[team].definitions.empty();
        }
    }
    std::lock_guard<std::mutex> lock(loadout_mutex);
    if(loadouts[0].definitions!=next[0].definitions || loadouts[1].definitions!=next[1].definitions)
        LOG_INFO("local inventory: loadout CT=%zu T=%zu",next[0].definitions.size(),next[1].definitions.size());
    loadouts[0]=std::move(next[0]);loadouts[1]=std::move(next[1]);
}

std::atomic<bool> closing{false}, cleanup_complete{false};
std::atomic<DWORD> game_thread{0};
std::uintptr_t Relative(std::uintptr_t call) {
    return call ? call+5+At<std::int32_t>(reinterpret_cast<void*>(call),1) : 0;
}
bool Resolve() {
    if(attempted) return ready;
    attempted=true;
    auto* base=static_cast<unsigned char*>(MEM::GetModuleBase("client.dll"));
    if(!base) {attempted=false;return false;}
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    // Only the supplied legacy client has been inspected offline. Never use
    // these layouts against an arbitrary client just because a pattern matches.
    if(nt->FileHeader.TimeDateStamp!=0x65272025) {
        LOG_WARN("local inventory: unsupported client timestamp; no SOC mutation");return false;
    }
    const auto m=MEM::FindPattern("client.dll","8D 44 24 28 B9 ? ? ? ? 50");
    const auto c=MEM::FindPattern("client.dll","55 8B EC 83 EC 1C 8D 45 E4 C7 45");
    const auto b=MEM::FindPattern("client.dll","E8 ? ? ? ? 8D 4D 0F");
    const auto a=MEM::FindPattern("client.dll","55 8B EC 83 E4 F8 83 EC 3C 53 8B 5D 08 56 57 6A");
    const auto n=MEM::FindPattern("client.dll","E8 ? ? ? ? 8B 46 78 C1 E8 0A A8 01 74 13 8B 46 34");
    const auto s=MEM::FindPattern("client.dll","E8 ? ? ? ? 0F B7 0F");
    if(!m||!c||!b||!a||!n||!s) {LOG_WARN("local inventory: required signature missing");return false;}
    manager=At<void*>(reinterpret_cast<void*>(m),5);
    create_item=At<decltype(create_item)>(reinterpret_cast<void*>(c),20);
    create_cache=reinterpret_cast<decltype(create_cache)>(Relative(b));
    attribute=reinterpret_cast<decltype(attribute)>(a);
    set_name=reinterpret_cast<decltype(set_name)>(Relative(n));
    item_system=reinterpret_cast<decltype(item_system)>(Relative(s));
    ready=manager && create_item;
    LOG_INFO("local inventory: legacy SOC backend resolved=%d (experimental)",ready);
    return ready;
}
bool Present(const Owned& value) {
    const int count=At<int>(cache,24);
    if(count<0||count>100000) return false;
    auto** items=At<Item**>(cache,4);
    for(int i=0;i<count;++i) if(items[i]==value.item) return items[i]->id==value.id;
    return false;
}
void Notify(std::size_t slot,Item* item) {
    Slot<void(__thiscall*)(void*,Owner,Item*,int)>(inventory,slot)(inventory,At<Owner>(inventory,8),item,4);
}
std::uint64_t SlotItem(int team,int slot) {
    return At<std::uint64_t>(inventory,0xd8+static_cast<std::size_t>(team*57+slot)*8);
}
void EquipLocal(int team,int slot,std::uint64_t id) {
    // Both manager slots 20 AND 29 can send GC messages in this client.
    // Only modify the inspected client loadout array consumed by getter 8.
    if(team<0 || team>3 || slot<0 || slot>56) return;
    At<std::uint64_t>(inventory,0xd8+static_cast<std::size_t>(team*57+slot)*8)=id;
}
void AutoEquip(const Key& key,Item* item,void* definition) {
    const auto base=reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
    if(reinterpret_cast<std::uintptr_t>(Slot<void*>(inventory,8))!=base+0x6cc240) return;
    int team=key.first==0?3:2;
    int slot=At<int>(definition,0x28c+static_cast<std::size_t>(team)*4);
    if(slot<0 || slot>56) {team=0;slot=At<int>(definition,0x268);}
    if(slot<0 || slot>56) return;
    auto& saved=equipped[{team,slot}];
    const auto current=SlotItem(team,slot);
    if(!saved.local || current!=saved.local) saved.original=current;
    saved.local=item->id;
    EquipLocal(team,slot,item->id);
    LOG_INFO("local inventory: locally equipped item=%llu team=%d slot=%d",static_cast<unsigned long long>(item->id),team,slot);
}
void Remove(const Owned& value) {
    if(!Present(value)) return;
    for(auto it=equipped.begin();it!=equipped.end();) {
        if(it->second.local==value.id) {
            if(SlotItem(it->first.first,it->first.second)==value.id)
                EquipLocal(it->first.first,it->first.second,it->second.original);
            it=equipped.erase(it);
        } else ++it;
    }
    Notify(2,value.item);
    Slot<void(__thiscall*)(void*,Item*)>(cache,3)(cache,value.item);
    Slot<void(__thiscall*)(Item*,bool)>(value.item,0)(value.item,true);
}
bool Attributes(Item* item,void* schema,const Request& request) {
    const auto& choice=request.finish;
    if(request.music) {
        auto* def=Slot<void*(__thiscall*)(void*,int)>(schema,27)(schema,166);
        if(!def) return false;
        int music=request.music;attribute(item,def,&music);return true;
    }
    // Agents and medals are definitions, not paint kits.
    if(!choice.paint_kit && !EquipmentState::Find(item->definition)) return true;
    const int ids[]={6,7,8,80,81};
    void* defs[5]{};
    for(int i=0;i<5;++i) {
        defs[i]=Slot<void*(__thiscall*)(void*,int)>(schema,27)(schema,ids[i]);
        if(!defs[i]) return false;
    }
    float paint=static_cast<float>(choice.paint_kit),seed=static_cast<float>(choice.seed),wear=choice.wear;
    attribute(item,defs[0],&paint);attribute(item,defs[1],&seed);attribute(item,defs[2],&wear);
    if(!choice.souvenir && choice.stattrak>=0) {
        int count=choice.stattrak,type=0;
        attribute(item,defs[3],&count);attribute(item,defs[4],&type);
    }
    if(choice.name[0]) set_name(item,choice.name.data());
    return true;
}
void Run() {
    if(closing.load() && owned.empty()) {cleanup_complete.store(true);return;}
    if(stopped || !Resolve()) return;
    auto* current=Slot<void*(__thiscall*)(void*)>(manager,23)(manager);
    ReadLoadout(current);
    if(!current || (!enabled.load() && owned.empty())) return;
    auto* soc=At<void*>(current,0xb4);
    if(!soc) return;
    auto* type_cache=create_cache(soc,1);
    if(!type_cache || At<int>(type_cache,32)!=1) return;
    if(current!=inventory || type_cache!=cache || At<std::uint64_t>(current,8)!=owner_id) {
        // Old cache belongs to the engine/account. Never dereference old ledger
        // pointers after replacement; it may already have destroyed the objects.
        owned.clear();equipped.clear();inventory=current;cache=type_cache;owner_id=At<std::uint64_t>(current,8);
    }
    std::map<Key,Request> desired;
    if(enabled.load() && !closing.load()) {
        {
            std::lock_guard<std::mutex> lock(SkinState::mutex);
            for(int team=0;team<2;++team) for(const auto& pair:SkinState::teams[team]) desired[{team,pair.first}]={pair.second,0};
        }
        for(int team=0;team<2;++team) {
            const auto equipment=EquipmentState::Get(team==0);
            if(equipment.inventory_agent) desired[{team,equipment.inventory_agent}]={};
            if(equipment.inventory_medal) desired[{team,equipment.inventory_medal}]={};
            if(equipment.inventory_music) desired[{team,1314}]={{},equipment.inventory_music};
            for(const auto& selection:{equipment.knife,equipment.gloves})
                if(selection.definition) desired[{team,selection.definition}]={selection.finish,0};
        }
    }
    for(auto it=owned.begin();it!=owned.end();) {
        const auto wanted=desired.find(it->first);
        if(wanted==desired.end() || (!SkinState::Equal(wanted->second.finish,it->second.request.finish) || wanted->second.music!=it->second.request.music) || !Present(it->second)) {
            Remove(it->second);it=owned.erase(it);
        } else ++it;
    }
    if(desired.empty()) { if(closing.load()) cleanup_complete.store(true); return; }
    auto* system=item_system();
    if(!system) return;
    auto* schema=Slot<void*(__thiscall*)(void*)>(system,0)(system);
    if(!schema) return;
    for(const auto& pair:desired) {
        if(owned.count(pair.first) || pair.first.second<=0 || pair.first.second>65535) continue;
        auto* definition=Slot<void*(__thiscall*)(void*,int)>(schema,4)(schema,pair.first.second);
        if(!definition) continue;
        const int count=At<int>(cache,24);
        if(count<0||count>100000) return;
        auto** objects=At<Item**>(cache,4);
        for(int i=0;i<count;++i) {
            const auto* existing=objects[i];
            if(existing) ids.Observe(existing->id,existing->position);
        }
        std::uint64_t id{}; std::uint32_t position{};
        if(!ids.Next(id,position)) return;
        auto* item=create_item();
        if(!item) return;
        const auto& choice=pair.second.finish;
        item->id=id;item->original=0;item->account=At<std::uint32_t>(inventory,8);item->position=position;
        item->definition=static_cast<std::uint16_t>(pair.first.second);
        const unsigned quality=choice.souvenir?12u:choice.stattrak>=0?9u:EquipmentState::Knife(pair.first.second)?3u:4u;
        item->properties=static_cast<std::uint16_t>(8u | (quality<<5));
        item->flags=0;
        if(!Attributes(item,schema,pair.second)) {Slot<void(__thiscall*)(Item*,bool)>(item,0)(item,true);return;}
        Slot<void(__thiscall*)(void*,Item*)>(cache,1)(cache,item);
        const Owned candidate{item,item->id,pair.second};
        if(!Present(candidate)) {
            Slot<void(__thiscall*)(Item*,bool)>(item,0)(item,true);
            LOG_WARN("local inventory: cache rejected local item");
            return;
        }
        owned.emplace(pair.first,Owned{item,item->id,pair.second});
        Notify(0,item);
        AutoEquip(pair.first,item,definition);
        last_loadout=0;
        LOG_INFO("local inventory: added local item=%llu definition=%d paint=%d",static_cast<unsigned long long>(item->id),pair.first.second,choice.paint_kit);
    }
}
}
Loadout GetLoadout(bool ct) {
    std::lock_guard<std::mutex> lock(loadout_mutex);
    return loadouts[ct?0:1];
}
void Frame() {
    game_thread.store(GetCurrentThreadId());
    __try { Run(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        stopped=true;
        LOG_ERROR("local inventory: exception 0x%08lX; SOC backend disabled",GetExceptionCode());
    }
}
void Shutdown() {
    closing.store(true);
    enabled.store(false);
    if(game_thread.load()==GetCurrentThreadId()) { Frame(); return; }
    // Restore runs on the unload worker. Never invoke SOC callbacks there.
    // Keep hooks alive briefly so FRAME_START can perform game-thread cleanup.
    const auto deadline=GetTickCount64()+500;
    while(!cleanup_complete.load() && GetTickCount64()<deadline) Sleep(1);
    if(!cleanup_complete.load()) LOG_WARN("local inventory: game-thread cleanup unavailable; leaving engine-owned cache intact");
}
}
