#include "../menu/preview/skin_state.hpp"
#include <cassert>
int main(){
 using namespace PreviewSkinState;
 assert(Get(true,"weapon_ak47").paint==0);
 Set(true,"weapon_ak47",180,5,.2f); auto a=Get(true,"weapon_ak47");
 assert(a.paint==180 && a.seed==5 && a.wear==.2f && a.revision);
 assert(!Get(false,"weapon_ak47").revision);
 Set(true,"weapon_ak47",180,5,.2f);assert(Get(true,"weapon_ak47").revision==a.revision);
 Set(true,"weapon_ak47",0,0,0);assert(Get(true,"weapon_ak47").paint==0 && Get(true,"weapon_ak47").revision>a.revision);
 Set(true,"weapon_ak47",100,-1,0);assert(Get(true,"weapon_ak47").paint==0);
}
