#include "features/skins/equipment_state.hpp"
#include <cassert>
#include "features/skins/local_inventory.hpp"
#include <limits>
#include <set>
int main(){using namespace EquipmentState;
 std::set<int> defs;for(const auto& item:items){assert(defs.insert(item.definition).second);assert(Find(item.name)==&item);assert(Find(item.definition)==&item);}
 assert(Find("weapon_knife_karambit")->definition==507);assert(Find("sporty_gloves")->definition==5030);
 assert(Knife(42)&&Knife(59)&&Knife(515)&&!Knife(5030));
 assert(!Select(true,false,"sporty_gloves"));assert(!Select(true,true,"weapon_bayonet"));assert(!Select(true,false,"bogus"));
 assert(Select(true,false,"weapon_bayonet"));assert(Get(true).knife.definition==500&&!Get(false).knife.definition);
 SkinState::Choice finish{};finish.paint_kit=10018;finish.wear=.1f;assert(Select(false,true,"sporty_gloves",finish));assert(Get(false).gloves.finish.paint_kit==10018);
 const auto stable=Revision();
 for(int i=0;i<1000;++i) assert(Select(true,false,"weapon_bayonet"));
 assert(Revision()==stable);
 const auto revision=Get(true).knife.revision;assert(Select(true,false,""));assert(!Get(true).knife.definition&&Get(true).knife.revision>revision);
 Agent(true,"models/player/example.mdl");assert(Get(true).agent=="models/player/example.mdl"&&Get(false).agent.empty());Agent(true,"");assert(Get(true).agent.empty());
 const auto no_agent=Revision();Agent(true,"");assert(Revision()==no_agent);
 const auto defaults=Revision();assert(Select(true,false,""));assert(Revision()==defaults);
 finish.wear=std::numeric_limits<float>::quiet_NaN();assert(!Select(false,true,"sporty_gloves",finish));
 InventorySelection(true,0,4711);InventorySelection(false,1,874);InventorySelection(true,2,42);
 assert(Get(true).inventory_agent==4711 && Get(false).inventory_agent==0);
 assert(Get(false).inventory_medal==874 && Get(true).inventory_music==42);
 InventorySelection(true,0,0);assert(Get(true).inventory_agent==0);
 LocalInventory::Loadout ct_loadout{true,{16,61,9}};
 LocalInventory::Loadout t_loadout{true,{7,4,9}};
 assert(ct_loadout.Contains(61) && !ct_loadout.Contains(32));
 assert(t_loadout.Contains(4) && !t_loadout.Contains(61));
 ct_loadout.ready=false;assert(!ct_loadout.Contains(61));
}
