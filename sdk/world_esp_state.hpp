#pragma once
#include <array>
#include <mutex>
#include <string_view>
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace WorldEsp {
enum Category { Weapon, Grenade, C4, None };
inline Category Classify(std::string_view name) {
    if(name=="CPlantedC4" || name=="CC4")return C4;
    if(name=="CFlashbang" || name.find("Grenade")!=name.npos || name=="CMolotovProjectile" || name=="CInferno" || name=="CDecoyProjectile")return Grenade;
    if(name.substr(0,7)=="CWeapon" || name=="CAK47" || name=="CDEagle" || name=="CKnife")return Weapon;
    return None;
}
inline bool Projectile(std::string_view name){return name.find("Projectile")!=name.npos || name=="CInferno";}
struct Options {
    std::array<bool,3> enabled{},icons{};
    std::array<std::uint32_t,3> colors{0xffffffff,0xffffffff,0xffffffff};
    std::uint32_t timer_color{0xffffffff};
    bool timer{},damage{},defuse{};
    bool Any() const { return enabled[0] || enabled[1] || enabled[2]; }
};
struct Item {
    Category category{None};float x{},y{},z{};
    char label[48]{};
    float explode_at{},defuse_at{}; bool planted{},defusing{};
    int estimated_damage{};
};
struct Frame {
    std::array<Item,256> items{};std::size_t count{};
    std::array<float,16> matrix{};
    float time{};bool valid{};
};
inline std::mutex mutex;
inline Options settings_state;
inline Frame frame_state;
inline void SetOptions(const Options& v){std::lock_guard<std::mutex> lock(mutex);settings_state=v;}
inline Options GetOptions(){std::lock_guard<std::mutex> lock(mutex);return settings_state;}
inline void Publish(const Frame& v){std::lock_guard<std::mutex> lock(mutex);frame_state=v;}
inline void CopyFrame(Frame& out){std::lock_guard<std::mutex> lock(mutex);out=frame_state;}
inline bool ProjectUnclipped(const Frame& f,const Item& item,float& x,float& y) {
    const auto& m=f.matrix;
    const float w=m[12]*item.x+m[13]*item.y+m[14]*item.z+m[15];
    if(!std::isfinite(w) || w<=.01f)return false;
    x=.5f+(m[0]*item.x+m[1]*item.y+m[2]*item.z+m[3])/(2.f*w);
    y=.5f-(m[4]*item.x+m[5]*item.y+m[6]*item.z+m[7])/(2.f*w);
    return std::isfinite(x) && std::isfinite(y);
}
inline bool Project(const Frame& f,const Item& item,float& x,float& y){
    return ProjectUnclipped(f,item,x,y) && x>=0.f && x<=1.f && y>=0.f && y<=1.f;
}
inline int BombDamage(float distance,int armor) {
    if(!std::isfinite(distance) || distance<0.f)return 0;
    const float d=(distance-75.68f)/789.2f;
    float damage=450.7f*std::exp(-d*d*.5f);
    if(armor>0){const float reduced=damage*.5f;damage=(damage-reduced)*.5f>armor ? damage-static_cast<float>(armor)*2.f : reduced;}
    return static_cast<int>(std::ceil((std::max)(0.f,damage)));
}
void Capture();
}
