#pragma once
#include <array>
#include <mutex>
#include <string_view>
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace WorldEsp {
enum Category { Weapon, Grenade, C4, None };
inline bool Projectile(std::string_view name){return name=="CBaseCSGrenadeProjectile" || name=="CSmokeGrenadeProjectile" || name=="CDecoyProjectile" || name=="CMolotovProjectile" || name=="CInferno";}
inline Category Classify(std::string_view name) {
    if(name=="CPlantedC4" || name=="CC4")return C4;
    if(Projectile(name) || name=="CFlashbang")return Grenade;
    if(name.substr(0,7)=="CWeapon" || name=="CAK47" || name=="CDEagle" || name=="CKnife" || name=="CSmokeGrenade" || name=="CHEGrenade" || name=="CMolotovGrenade" || name=="CIncendiaryGrenade" || name=="CDecoyGrenade")return Weapon;
    return None;
}
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
    float time{};std::uint32_t framecount{};std::uint64_t captured_ms{};bool valid{};
};
inline std::mutex mutex;
inline Options settings_state;
inline Frame frame_state;
inline void SetOptions(const Options& v){std::lock_guard<std::mutex> lock(mutex);settings_state=v;}
inline Options GetOptions(){std::lock_guard<std::mutex> lock(mutex);return settings_state;}
inline void Publish(const Frame& v){std::lock_guard<std::mutex> lock(mutex);frame_state=v;}
inline void CopyFrame(Frame& out){std::lock_guard<std::mutex> lock(mutex);out=frame_state;}
inline bool ValidMatrix(const std::array<float,16>& m){
    for(float v:m)if(!std::isfinite(v) || std::abs(v)>1.e8f || (v!=0.f && std::fpclassify(v)==FP_SUBNORMAL))return false;
    const double ax=m[0],ay=m[1],az=m[2],bx=m[4],by=m[5],bz=m[6],cx=m[12],cy=m[13],cz=m[14];
    const double aa=ax*ax+ay*ay+az*az,bb=bx*bx+by*by+bz*bz,cc=cx*cx+cy*cy+cz*cz;
    if(aa<1.e-6 || aa>1.e6 || bb<1.e-6 || bb>1.e6 || cc<.25 || cc>4.)return false;
    const double ux=by*cz-bz*cy,uy=bz*cx-bx*cz,uz=bx*cy-by*cx;
    const double vx=cy*az-cz*ay,vy=cz*ax-cx*az,vz=cx*ay-cy*ax;
    const double wx=ay*bz-az*by,wy=az*bx-ax*bz,wz=ax*by-ay*bx;
    const double det=ax*ux+ay*uy+az*uz;if(std::abs(det)<1.e-8)return false;
    // Solve for the camera origin, then project a point 64 units forward.
    const double x=(-m[3]*ux-m[7]*vx-m[15]*wx)/det+cx/std::sqrt(cc)*64.;
    const double y=(-m[3]*uy-m[7]*vy-m[15]*wy)/det+cy/std::sqrt(cc)*64.;
    const double z=(-m[3]*uz-m[7]*vz-m[15]*wz)/det+cz/std::sqrt(cc)*64.;
    const double w=cx*x+cy*y+cz*z+m[15];
    const double px=(ax*x+ay*y+az*z+m[3])/w,py=(bx*x+by*y+bz*z+m[7])/w;
    return std::isfinite(w) && w>.01 && std::isfinite(px) && std::isfinite(py) && std::abs(px)<4. && std::abs(py)<4. && (std::abs(m[8])+std::abs(m[9])+std::abs(m[10])>.001f);
}
inline bool Fresh(const Frame& f,std::uint32_t frame,float time,std::uint64_t now){return f.valid && frame>=f.framecount && frame-f.framecount<=2 && std::isfinite(time) && time>=f.time && time-f.time<=.25f && now>=f.captured_ms && now-f.captured_ms<=250;}
inline bool ProjectUnclipped(const Frame& f,const Item& item,float& x,float& y) {
    if(!ValidMatrix(f.matrix) || !std::isfinite(item.x) || !std::isfinite(item.y) || !std::isfinite(item.z))return false;
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
void Initialize();
void Capture(); // stage 6: matrix + freshness only
void Scan(); // stage 4: throttled read-only entity scan
bool RenderClock(std::uint32_t& frame,float& time);
}
