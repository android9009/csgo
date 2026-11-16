#include "sdk/visual_preview_state.hpp"
#include "sdk/world_esp_state.hpp"
#include <cassert>
#include <limits>
int main(){
    using namespace VisualPreview;
    auto p=ClampPosition({-2.f,100.f},{20.f,20.f},{80.f,80.f});assert(p.x==0.f && p.y==60.f);
    p=ClampPosition({20.f,20.f},{100.f,100.f},{80.f,80.f});assert(p.x==0.f && p.y==0.f);
    SetOffset(Name,{.2f,-.3f});assert(GetLayout()[Name].x==.2f);
    SetOffset(Health,{4.f,-4.f});assert(GetLayout()[Health].x==1.f && GetLayout()[Health].y==-1.f);
    SetOffset(Name,{std::numeric_limits<float>::quiet_NaN(),0});assert(GetLayout()[Name].x==0.f);
    Material a,b;assert(a==b);b.primary=0xffff0000;assert(!(a==b));SetMaterial(b);assert(GetMaterial()==b);
    using namespace WorldEsp;
    assert(Classify("CWeaponAWP")==WorldEsp::Weapon);assert(Classify("CAK47")==WorldEsp::Weapon);
    assert(Classify("CSmokeGrenadeProjectile")==Grenade);assert(Classify("CMolotovProjectile")==Grenade);
    assert(Classify("CFlashbang")==Grenade);assert(Projectile("CSmokeGrenadeProjectile"));assert(!Projectile("CSmokeGrenade"));
    assert(Classify("CC4")==C4 && Classify("CPlantedC4")==C4);assert(Classify("CCSPlayer")==None);
    Frame f;f.matrix={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};Item item;
    float x{},y{};assert(Project(f,item,x,y) && x==.5f && y==.5f);
    item.x=20.f;assert(!Project(f,item,x,y));item.x=0;f.matrix[15]=-1;assert(!Project(f,item,x,y));
    assert(BombDamage(100,100)<BombDamage(100,0));assert(BombDamage(1500,0)<BombDamage(100,0));
    assert(BombDamage(std::numeric_limits<float>::quiet_NaN(),0)==0);
    static Frame snapshot;f.valid=true;f.count=1;Publish(f);CopyFrame(snapshot);assert(snapshot.valid && snapshot.count==1);
    Options o;o.enabled[0]=true;SetOptions(o);assert(GetOptions().Any());
}
