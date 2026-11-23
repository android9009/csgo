#include "equipment_changer.hpp"
#include "equipment_state.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include "../../sdk/netvars.hpp"
#include "../../utils/log.hpp"
#include "../../utils/memory.hpp"
#include "../../utils/patterns/patterns.hpp"
#include <windows.h>
#include <map>
#include <set>
#include <cstring>

namespace EquipmentChanger { namespace {
template<class T>T& At(void* e,std::ptrdiff_t o){return *reinterpret_cast<T*>(static_cast<unsigned char*>(e)+o);}
template<class T>T Slot(void* e,int i){return reinterpret_cast<T>((*reinterpret_cast<void***>(e))[i]);}
struct Vars {
 std::ptrdiff_t team{},health{},model{},body{},weapons{},wearables{},viewmodel{},viewweapon{},worldmodel{};
 std::ptrdiff_t def{},high{},idlow{},account{},quality{},paint{},seed{},wear{},stat{},name{};
};
Vars Resolve(){
 auto& p=PropManager::Get(); Vars v;
 v.team=p.GetOffset("DT_BaseEntity","m_iTeamNum");v.health=p.GetOffset("DT_BasePlayer","m_iHealth");
 v.model=p.GetOffset("DT_BaseEntity","m_nModelIndex");v.body=p.GetOffset("DT_BaseAnimating","m_nBody");
 v.weapons=p.GetOffset("DT_BaseCombatCharacter","m_hMyWeapons");v.wearables=p.GetOffset("DT_BaseCombatCharacter","m_hMyWearables");
 v.viewmodel=p.GetOffset("DT_BasePlayer","m_hViewModel[0]");
 v.viewweapon=p.GetOffset("DT_BaseViewModel","m_hWeapon");v.worldmodel=p.GetOffset("DT_BaseCombatWeapon","m_hWeaponWorldModel");
 const auto t="DT_BaseAttributableItem";
 v.def=p.GetOffset(t,"m_iItemDefinitionIndex");v.high=p.GetOffset(t,"m_iItemIDHigh");v.idlow=p.GetOffset(t,"m_iItemIDLow");
 v.account=p.GetOffset(t,"m_iAccountID");
 v.quality=p.GetOffset(t,"m_iEntityQuality");v.paint=p.GetOffset(t,"m_nFallbackPaintKit");v.seed=p.GetOffset(t,"m_nFallbackSeed");
 v.wear=p.GetOffset(t,"m_flFallbackWear");v.stat=p.GetOffset(t,"m_nFallbackStatTrak");v.name=p.GetOffset(t,"m_szCustomName");return v;
}
bool Offset(std::ptrdiff_t o){return o>0 && o<0x10000;}
bool Valid(const Vars& v){for(auto o:{v.team,v.health,v.model,v.def,v.high,v.account,v.quality,v.paint,v.seed,v.wear,v.stat,v.name})if(!Offset(o))return false;return true;}
bool Handle(std::uint32_t h){return h && h!=0xffffffffu;}
void* Entity(std::uint32_t h){return Handle(h)?Interfaces::EntityList->GetClientEntityFromHandle(h):nullptr;}
// These are the Source 1/Fatality IClientNetworkable and C_BaseEntity interfaces.
// Class identity is checked before wearable lifecycle calls; arbitrary entities
// are never released, and only entities created by this module are destroyed.
void* Networkable(void* entity,const char* required=nullptr){
 if(!entity)return nullptr;auto* net=static_cast<unsigned char*>(entity)+8;
 auto* cls=Slot<ClientClass*(__thiscall*)(void*)>(net,2)(net);
 if(!cls || !cls->m_pNetworkName || (required && std::strcmp(cls->m_pNetworkName,required)))return nullptr;
 return net;
}
void Notify(void* e){if(auto* n=Networkable(e)){Slot<void(__thiscall*)(void*,int)>(n,6)(n,0);Slot<void(__thiscall*)(void*,int)>(n,7)(n,0);}}
// A client-created wearable otherwise sits at the world origin; park it where the game
// can never draw or collide with it (the original `main` make_glove does the same).
// SetAbsOrigin is a non-virtual client function: the legacy-catalogue signature is
// relative=true, so the published value is a client.dll offset and is rebased on
// client.dll. The call is guarded: on a build where the signature misses, the wearable
// simply stays put.
void MoveFar(void* e){
 if(!e)return;
 const auto rel=Patterns::Get("set_abs_origin");
 const auto base=reinterpret_cast<std::uintptr_t>(MEM::GetModuleBase("client.dll"));
 if(!rel || !base)return;
 __try {
  const float parked[3]={16384.f,16384.f,16384.f};
  reinterpret_cast<void(__thiscall*)(void*,const float*)>(base+rel)(e,parked);
 }
 __except (EXCEPTION_EXECUTE_HANDLER) {}
}
void SetModel(void* e,int index,const Vars& v){
 if(e && index>0 && At<int>(e,v.model)!=index)Slot<void(__thiscall*)(void*,int)>(e,75)(e,index);
}
int Model(const char* path){
 const int index=Interfaces::ModelInfo->GetModelIndex(path);
 if(index>0)return index;
 static std::set<std::string> warned;if(warned.insert(path).second)LOG_WARN("equipment: model is not precached: %s",path);
 return -1;
}
struct Saved {void* entity{};short def{};int model{},high{},account{},quality{},paint{},seed{},stat{};float wear{};char name[32]{};};
std::map<std::uint32_t,Saved> saved;
void* player{};int original_agent{},applied_agent{},original_body{};bool body_changed{};
std::uint32_t created_glove{},previous_wearable{};std::uint32_t serial_counter{1};

Saved& Capture(std::uint32_t h,void* e,const Vars& v){
 auto it=saved.find(h);if(it!=saved.end() && it->second.entity==e)return it->second;
 Saved s{};s.entity=e;s.def=At<short>(e,v.def);s.model=At<int>(e,v.model);s.high=At<int>(e,v.high);s.account=At<int>(e,v.account);
 s.quality=At<int>(e,v.quality);s.paint=At<int>(e,v.paint);s.seed=At<int>(e,v.seed);s.stat=At<int>(e,v.stat);s.wear=At<float>(e,v.wear);
 std::memcpy(s.name,&At<char>(e,v.name),32);return saved.insert_or_assign(h,s).first->second;
}
void Restore(std::uint32_t h,const Vars& v){
 auto it=saved.find(h);if(it==saved.end())return;const auto s=it->second;auto* e=Entity(h);
 // The pointer check alone is not enough: after a full update the entity pool can
 // hand the same address to a different weapon. Only restore onto an entity that
 // still looks like the one captured (same def, or still a knife for a knife that
 // had a custom def applied).
 if(e==s.entity && (At<short>(e,v.def)==s.def || EquipmentState::Knife(At<short>(e,v.def)))){
  At<short>(e,v.def)=s.def;At<int>(e,v.high)=s.high;At<int>(e,v.account)=s.account;At<int>(e,v.quality)=s.quality;
  At<int>(e,v.paint)=s.paint;At<int>(e,v.seed)=s.seed;At<int>(e,v.stat)=s.stat;At<float>(e,v.wear)=s.wear;std::memcpy(&At<char>(e,v.name),s.name,32);SetModel(e,s.model,v);Notify(e);}
 saved.erase(it);
}
void Apply(std::uint32_t h,void* e,const EquipmentState::Selection& c,int model,std::uint32_t account,const Vars& v){
 auto& original=Capture(h,e,v);
 At<int>(e,v.high)=-1;if(Offset(v.idlow))At<int>(e,v.idlow)=-1;
 At<int>(e,v.account)=static_cast<int>(account);
 At<int>(e,v.quality)=EquipmentState::Knife(c.definition)?3:4;At<int>(e,v.paint)=c.finish.paint_kit;At<int>(e,v.seed)=c.finish.seed;
 At<float>(e,v.wear)=(std::max)(.000001f,c.finish.wear);At<int>(e,v.stat)=c.finish.stattrak==-2?original.stat:c.finish.stattrak;
 std::memcpy(&At<char>(e,v.name),c.finish.name.data(),32);
 // The def and the model move together, and only when the def actually changes
 // (reference behaviour): the engine re-derives everything else, and the skin
 // material rebuild after a full update is the skin changer's job. Swapping the
 // model through -1 on a carried weapon churns the game's own view-model setup.
 if(At<short>(e,v.def)!=static_cast<short>(c.definition)){
  At<short>(e,v.def)=static_cast<short>(c.definition);
  SetModel(e,model,v);
  Notify(e);
  LOG_INFO("equipment: applied definition=%d paint=%d model=%d",c.definition,c.finish.paint_kit,model);
 }else{SetModel(e,model,v);}
}
void RemoveCreated(void* local,const Vars& v){
 if(!created_glove)return;auto* glove=Entity(created_glove);
 if(local && Offset(v.wearables) && At<std::uint32_t>(local,v.wearables)==created_glove)At<std::uint32_t>(local,v.wearables)=previous_wearable;
 saved.erase(created_glove);
 if(auto* net=Networkable(glove,"CEconWearable")){Slot<void(__thiscall*)(void*)>(net,12)(net);Slot<void(__thiscall*)(void*)>(net,1)(net);}
 created_glove=0;previous_wearable=0;
}
std::uint32_t Account(){alignas(8) unsigned char info[512]{};using Fn=bool(__thiscall*)(void*,int,void*);
 if(!Slot<Fn>(Interfaces::Engine,8)(Interfaces::Engine,Interfaces::Engine->GetLocalPlayer(),info))return 0;
 std::uint32_t a{};std::memcpy(&a,info+8,4);return a;}
void Gloves(void* local,const EquipmentState::Selection& c,std::uint32_t account,const Vars& v,std::set<std::uint32_t>& held){
 if(!Offset(v.wearables) || !Offset(v.body))return;
 if(!c.definition || At<int>(local,v.health)<=0){
  if(!created_glove){const auto h=At<std::uint32_t>(local,v.wearables);Restore(h,v);}else RemoveCreated(local,v);
  if(body_changed){At<int>(local,v.body)=original_body;body_changed=false;}return;
 }
 const auto* item=EquipmentState::Find(c.definition);if(!item)return;
 int model=Model(item->world);if(model<=0)model=Model(item->view);if(model<=0)return;
 auto& handle=At<std::uint32_t>(local,v.wearables);auto* glove=Entity(handle);
 if(created_glove && handle!=created_glove) {
  if(!glove && Entity(created_glove)){handle=created_glove;glove=Entity(handle);}
  else RemoveCreated(local,v);
 }
 if(!glove){
  CreateClientClassFn create=nullptr;for(auto* cls=Interfaces::Client->GetAllClasses();cls;cls=cls->m_pNext)
   if(cls->m_pNetworkName && !std::strcmp(cls->m_pNetworkName,"CEconWearable")){create=cls->m_pCreateFn;break;}
  if(!create)return;const int entry=Interfaces::EntityList->GetHighestEntityIndex()+1;if(entry<=0 || entry>=2048)return;
  const int serial=static_cast<int>((++serial_counter)%0x1000);create(entry,serial);
  const auto new_handle=static_cast<std::uint32_t>(entry)|(static_cast<std::uint32_t>(serial)<<16);
  glove=Entity(new_handle);if(!Networkable(glove,"CEconWearable"))return;
  previous_wearable=handle;created_glove=handle=new_handle;
  MoveFar(glove);
  LOG_INFO("equipment: created experimental wearable handle=%u",new_handle);
 }
 if(!Networkable(glove,"CEconWearable"))return;
 if(!body_changed){original_body=At<int>(local,v.body);body_changed=true;}
 At<int>(local,v.body)=1;held.insert(handle);Apply(handle,glove,c,model,account,v);
}
} // namespace
void FrameStage(){
 if(!Interfaces::Engine || !Interfaces::EntityList || !Interfaces::ModelInfo || !Interfaces::Client || !PropManager::Get().IsReady() || !Interfaces::Engine->IsInGame()){Reset();return;}
 const auto v=Resolve();if(!Valid(v))return;
 auto* local=Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer());if(!local){Reset();return;}
 if(player!=local){Reset();player=local;}
 const int team=At<int>(local,v.team);if(team!=2 && team!=3)return;const auto choice=EquipmentState::Get(team==3);
 if(!choice.agent.empty()){
  const int index=Model(choice.agent.c_str());if(index>0){const int current=At<int>(local,v.model);
   if(!original_agent || current!=applied_agent)original_agent=current;SetModel(local,index,v);applied_agent=index;}
 }else if(original_agent){SetModel(local,original_agent,v);original_agent=applied_agent=0;}
 const auto account=Account();if(!account)return;
 std::set<std::uint32_t> held;
 if(Offset(v.weapons))for(int i=0;i<64;++i){
  const auto h=At<std::uint32_t>(local,v.weapons+static_cast<std::ptrdiff_t>(i)*4);auto* e=Entity(h);if(!e || !EquipmentState::Knife(At<short>(e,v.def)))continue;
  if(!choice.knife.definition){Restore(h,v);continue;}const auto* item=EquipmentState::Find(choice.knife.definition);if(!item)continue;
  const int view=Model(item->view),world=Model(item->world);if(view<=0 || world<=0)continue;
  held.insert(h);Apply(h,e,choice.knife,view,account,v);
  // The world model and the carried view model follow the weapon. SetModel is
  // a no-op while the values already match, so each is one write after a
  // (re)creation and silent otherwise. While the knife is not carried the view
  // model belongs to another weapon and the engine owns it - a stale-restore
  // map is how models leak across weapons.
  if(Offset(v.worldmodel)){auto* wm=Entity(At<std::uint32_t>(e,v.worldmodel));if(wm)SetModel(wm,world,v);}
  if(Offset(v.viewmodel) && Offset(v.viewweapon)){const auto vmh=At<std::uint32_t>(local,v.viewmodel);auto* vm=Entity(vmh);
   if(vm && At<std::uint32_t>(vm,v.viewweapon)==h)SetModel(vm,view,v);}
 }
 Gloves(local,choice.gloves,account,v,held);
 for(auto it=saved.begin();it!=saved.end();){auto h=it->first;++it;if(!held.count(h))Restore(h,v);}
}
void Reset(){saved.clear();player=nullptr;original_agent=applied_agent=original_body=0;body_changed=false;created_glove=previous_wearable=0;}
void Shutdown(){
 if(Interfaces::Engine && Interfaces::Engine->IsInGame() && Interfaces::EntityList && PropManager::Get().IsReady()){
 const auto v=Resolve();if(Valid(v)){
 auto* local=Interfaces::EntityList->GetClientEntity(Interfaces::Engine->GetLocalPlayer());
 if(local==player){if(original_agent)SetModel(local,original_agent,v);if(body_changed && Offset(v.body))At<int>(local,v.body)=original_body;}
  RemoveCreated(local==player?local:nullptr,v);
  while(!saved.empty())Restore(saved.begin()->first,v);}}
 Reset();
}
}
