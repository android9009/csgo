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
    bool arrows{};float arrow_distance{50.f},arrow_size{24.f};std::uint32_t arrow_color{};
    bool trajectory{};std::uint32_t trajectory_hit{},trajectory_miss{};
    unsigned tracer_targets{},hitmarkers{};bool impacts{};
    std::array<unsigned,3> tracer_styles{};
    std::array<std::uint32_t,3> tracer_colors{},hit_colors{};
    std::array<float,3> hit_duration{1.f,1.f,1.f};
    std::uint32_t impact_color{};
    bool vignette{},local_contrast{},correction{},grayscale{};
    unsigned tone_curve{};std::uint32_t vignette_color{};
    float contrast{},highlights{},shadows{},saturation{},vibrance{},temperature{},tint{};
    float local_strength{},edge_strength{},vignette_start{},vignette_end{},vignette_blur{};
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
struct ArrowPoint {Vector position{};float alpha{};};
struct ArrowFrame {std::array<ArrowPoint,64> points{};Vector eye{};};
inline std::mutex arrow_mutex;
inline ArrowFrame arrow_frame;
inline void PublishArrows(const ArrowFrame& v){std::unique_lock<std::mutex> lock(arrow_mutex,std::try_to_lock);if(lock.owns_lock())arrow_frame=v;}
inline bool CopyArrows(ArrowFrame& v){std::unique_lock<std::mutex> lock(arrow_mutex,std::try_to_lock);if(!lock.owns_lock())return false;v=arrow_frame;return true;}
struct Trajectory {std::array<Vector,256> points{};std::array<bool,256> bounces{};std::size_t count{};bool hit{},detonated{};};
inline std::mutex trajectory_mutex;
inline Trajectory trajectory;
inline void PublishTrajectory(const Trajectory& v){std::unique_lock<std::mutex> lock(trajectory_mutex,std::try_to_lock);if(lock.owns_lock())trajectory=v;}
inline bool CopyTrajectory(Trajectory& v){std::unique_lock<std::mutex> lock(trajectory_mutex,std::try_to_lock);if(!lock.owns_lock())return false;v=trajectory;return true;}
struct EventVisual {Vector start{},end{};float time{};unsigned kind{},group{};};
struct EventFrame {std::array<EventVisual,256> records{};std::size_t count{};float time{};};
inline std::mutex event_mutex;
inline EventFrame event_frame;
inline void ClearEvents(){std::lock_guard<std::mutex> lock(event_mutex);event_frame={};}
inline void AddEvent(const EventVisual& value){
    std::lock_guard<std::mutex> lock(event_mutex);
    if(event_frame.count==event_frame.records.size()){
        std::move(event_frame.records.begin()+1,event_frame.records.end(),event_frame.records.begin());--event_frame.count;
    }
    event_frame.records[event_frame.count++]=value;
}
inline void TickEvents(float now){
    std::lock_guard<std::mutex> lock(event_mutex);
    if(now<event_frame.time)event_frame.count=0;
    auto end=std::remove_if(event_frame.records.begin(),event_frame.records.begin()+event_frame.count,[now](const EventVisual& v){return now-v.time>10.f || now<v.time;});
    event_frame.count=static_cast<std::size_t>(end-event_frame.records.begin());event_frame.time=now;
}
inline bool CopyEvents(EventFrame& out){std::unique_lock<std::mutex> lock(event_mutex,std::try_to_lock);if(!lock.owns_lock())return false;out=event_frame;return true;}
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
