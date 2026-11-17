#include "sdk/world_esp_state.hpp"
#include <cassert>
#include <limits>
int main(){
    using namespace WorldEsp;
    assert(Classify("CWeaponFlashbang")==Weapon && Classify("CWeaponSmokeGrenade")==Weapon && Classify("CWeaponMolotov")==Weapon);
    assert(Classify("RandomGrenadeThing")==None && Classify("CBaseCSGrenadeProjectile")==Grenade);
    assert(Classify("CPlantedC4")==C4 && !Projectile("CWeaponSmokeGrenade"));
    Frame f;f.matrix={1,0,0,0,0,1,0,0,0,0,1,-1,0,0,1,0};assert(ValidMatrix(f.matrix));
    Item p;p.z=10;float x{},y{};assert(Project(f,p,x,y));
    p.x=std::numeric_limits<float>::infinity();assert(!ProjectUnclipped(f,p,x,y));p.x=0;
    auto original=f.matrix;f.matrix[1]=std::numeric_limits<float>::quiet_NaN();assert(!ValidMatrix(f.matrix) && !Project(f,p,x,y));
    f.matrix=original;f.matrix[14]=0;assert(!ValidMatrix(f.matrix));
    f.matrix=original;f.matrix[0]=std::numeric_limits<float>::denorm_min();assert(!ValidMatrix(f.matrix));
    f.matrix=original;f.valid=true;f.framecount=100;f.time=10;f.captured_ms=1000;
    assert(Fresh(f,102,10.02f,1020));assert(!Fresh(f,103,10.02f,1020));
    assert(!Fresh(f,99,10.02f,1020));assert(!Fresh(f,101,9.f,1020));assert(!Fresh(f,101,10.02f,2000));
    Frame copy;Publish(f);CopyFrame(copy);assert(copy.framecount==100 && copy.captured_ms==1000);
}
