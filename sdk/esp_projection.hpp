#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
namespace EspProjection {
struct Point {float x{},y{},z{};};
struct Clip {float x{},y{},w{};};
struct Box {float left{},top{},right{},bottom{};};
inline Clip Transform(const std::array<float,16>& m,const Point& p){
    return {m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3],m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7],m[12]*p.x+m[13]*p.y+m[14]*p.z+m[15]};
}
// Clip edges at the camera plane, not at the viewport. An off-screen corner
// must not discard a nearby player whose remaining hull is still visible.
inline bool Bounds(const std::array<float,16>& m,const std::array<Point,8>& vertices,Box& box){
    constexpr float near_w=.01f;
    std::array<Clip,8> clips{};
    box={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest()};
    bool any=false;
    auto add=[&](const Clip& c){
        const float x=.5f+c.x/(2.f*c.w),y=.5f-c.y/(2.f*c.w);
        if(!std::isfinite(x) || !std::isfinite(y))return;
        any=true;box.left=std::min(box.left,x);box.right=std::max(box.right,x);box.top=std::min(box.top,y);box.bottom=std::max(box.bottom,y);
    };
    for(int i=0;i<8;++i){clips[i]=Transform(m,vertices[i]);if(!std::isfinite(clips[i].w))return false;if(clips[i].w>=near_w)add(clips[i]);}
    for(int i=0;i<8;++i)for(int axis=0;axis<3;++axis){
        const int j=i^(1<<axis);if(i>=j)continue;
        const auto& a=clips[i];const auto& b=clips[j];
        if((a.w>=near_w)==(b.w>=near_w))continue;
        const float t=(near_w-a.w)/(b.w-a.w);
        add({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,near_w});
    }
    if(!any || box.right<=0.f || box.left>=1.f || box.bottom<=0.f || box.top>=1.f)return false;
    box.left=std::max(0.f,box.left);box.top=std::max(0.f,box.top);box.right=std::min(1.f,box.right);box.bottom=std::min(1.f,box.bottom);
    return box.right>box.left && box.bottom>box.top;
}
}
