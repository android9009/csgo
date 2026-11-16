#include "sdk/preview_sequence.hpp"
#include <cassert>
int main(){
 using namespace PreviewSequence;
 assert(Family("weapon_glock")=="pistol");
 assert(Score("pistol_idle_stand","weapon_glock")>Score("rifle_idle_stand","weapon_glock"));
 assert(Score("pistol_crouch_idle","weapon_glock")==-1);
 assert(Score("rifle_sit_idle","weapon_ak47")==-1);
 assert(Select({},"weapon_glock")==-1);
 std::vector<std::uint8_t> b(1024);
 auto set=[&](int pos,int v){std::memcpy(b.data()+pos,&v,4);};
 set(0,0x54534449);set(188,3);set(192,196);
 const char* names[]={"rifle_stand_idle","pistol_stand_idle","pistol_crouch_idle"};
 for(int i=0;i<3;++i){int desc=196+i*212,label=850+i*40;set(desc+4,label-desc);std::memcpy(b.data()+label,names[i],std::strlen(names[i])+1);}
 assert(Select(b,"weapon_glock")==1);assert(Select(b,"weapon_ak47")==0);
 set(192,0x7fffffff);assert(Select(b,"weapon_glock")==-1);
}
