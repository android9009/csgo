#pragma once
#include "../../utils/math/vector.hpp"
#include "../../utils/math/qangle.hpp"
#include <array>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <cstdint>
namespace VisualMisc {
struct Settings {
    bool third{},dead{},grenade_off{},instant{},fov_enabled{},second_zoom{},viewmodel_scope{},vm_enabled{},landing_bob{};
    float distance{100},scope_alpha{},nade_alpha{},fov{90},zoom{},aspect{},vm_fov{68};
    Vector vm_position{};QAngle vm_angles{};
    unsigned scope{},removals{};bool scope_invert{},crosshair{},spread{};
    float scope_size{100},texture_alpha{},prop_alpha{};std::uint32_t scope_color{0xff000000};
    bool night{},fullbright{},sky{},fog{},exposure{},bloom{};
    std::uint32_t night_color{0xffffffff},fullbright_color{0xffffffff},sky_color{0xffffffff},fog_color{0xffffffff};
    float exposure_value{50},bloom_value{50};
};
inline std::mutex settings_mutex;
inline Settings settings;
inline void Publish(const Settings& value){std::unique_lock<std::mutex> lock(settings_mutex,std::try_to_lock);if(lock.owns_lock())settings=value;}
inline Settings Get(){static thread_local Settings copy;std::unique_lock<std::mutex> lock(settings_mutex,std::try_to_lock);if(lock.owns_lock())copy=settings;return copy;}
struct LocalFrame {bool alive{},scoped{};float spread{};};
inline std::mutex frame_mutex;
inline LocalFrame local_frame;
inline void PublishLocal(const LocalFrame& v){std::unique_lock<std::mutex> lock(frame_mutex,std::try_to_lock);if(lock.owns_lock())local_frame=v;}
inline LocalFrame GetLocal(){static thread_local LocalFrame v;std::unique_lock<std::mutex> lock(frame_mutex,std::try_to_lock);if(lock.owns_lock())v=local_frame;return v;}
inline float Smooth(float current,float target,float seconds,float rate=15.f){
    if(!std::isfinite(current))current=target;
    if(!std::isfinite(seconds) || seconds<=0.f)return current;
    return current+(target-current)*(1.f-std::exp(-rate*(std::min)(seconds,.1f)));
}
inline float ZoomFov(float original,float base,float percent){return original+(base-original)*std::clamp(percent*.01f,0.f,1.f);}
inline Vector Forward(const QAngle& a){constexpr float r=.01745329252f;return {std::cos(a.x*r)*std::cos(a.y*r),std::cos(a.x*r)*std::sin(a.y*r),-std::sin(a.x*r)};}
inline Vector ViewmodelOffset(const QAngle& a,const Vector& offset){
    constexpr float r=.01745329252f;const float sp=std::sin(a.x*r),cp=std::cos(a.x*r),sy=std::sin(a.y*r),cy=std::cos(a.y*r),sr=std::sin(a.z*r),cr=std::cos(a.z*r);
    const Vector right{-sr*sp*cy+cr*sy,-sr*sp*sy-cr*cy,-sr*cp};
    const Vector up{cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
    return right*offset.x+Forward(a)*offset.y+up*offset.z;
}
}
