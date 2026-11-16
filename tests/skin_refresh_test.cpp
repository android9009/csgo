#include "features/skins/refresh_gate.hpp"
#include "features/skins/skin_state.hpp"
#include "features/skins/equipment_state.hpp"
#include <cassert>
int main(){
 SkinRefresh::Gate gate;
 auto tick=[&]{return gate.Observe(SkinState::Revision(),EquipmentState::Revision());};
 assert(!tick());SkinState::Choice c{};c.paint_kit=180;c.stattrak=-1;
 assert(SkinState::Set(true,7,c));assert(tick());
 // Simulate unchanged UI commits and repeated entity recreation/network frames.
 for(int frame=0;frame<10000;++frame){assert(SkinState::Set(true,7,c));assert(!tick());}
 ++c.seed;assert(SkinState::Set(true,7,c));assert(tick());assert(!tick());
 assert(EquipmentState::Select(true,false,"weapon_bayonet"));assert(tick());
 for(int frame=0;frame<10000;++frame){assert(EquipmentState::Select(true,false,"weapon_bayonet"));assert(!tick());}
 SkinState::Remove(true,7);assert(tick());SkinState::Remove(true,7);assert(!tick());
 assert(EquipmentState::Select(true,false,""));assert(tick());
 assert(EquipmentState::Select(true,false,""));assert(!tick());
 EquipmentState::Agent(false,"models/player/example.mdl");assert(tick());
 EquipmentState::Agent(false,"models/player/example.mdl");assert(!tick());
}
