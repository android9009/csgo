#include "features/skins/equipment_state.hpp"
#include <cassert>
#include <limits>
#include <set>
int main(){using namespace EquipmentState;
 std::set<int> defs;for(const auto& item:items){assert(defs.insert(item.definition).second);assert(Find(item.name)==&item);assert(Find(item.definition)==&item);}
 assert(Find("weapon_knife_karambit")->definition==507);assert(Find("sporty_gloves")->definition==5030);
 assert(Knife(42)&&Knife(59)&&Knife(515)&&!Knife(5030));
 assert(!Select(true,false,"sporty_gloves"));assert(!Select(true,true,"weapon_bayonet"));assert(!Select(true,false,"bogus"));
 assert(Select(true,false,"weapon_bayonet"));assert(Get(true).knife.definition==500&&!Get(false).knife.definition);
 SkinState::Choice finish{};finish.paint_kit=10018;finish.wear=.1f;assert(Select(false,true,"sporty_gloves",finish));assert(Get(false).gloves.finish.paint_kit==10018);
 const auto revision=Get(true).knife.revision;assert(Select(true,false,""));assert(!Get(true).knife.definition&&Get(true).knife.revision>revision);
 Agent(true,"models/player/example.mdl");assert(Get(true).agent=="models/player/example.mdl"&&Get(false).agent.empty());Agent(true,"");assert(Get(true).agent.empty());
 finish.wear=std::numeric_limits<float>::quiet_NaN();assert(!Select(false,true,"sporty_gloves",finish));
}
