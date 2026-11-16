#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace EspDock {
enum Side {Left,Right,Top,Bottom};
constexpr int FlagCount=11, Count=6+FlagCount;
struct Placement {Side side{Right};int order{};};
using Layout=std::array<Placement,Count>;
struct Rect {float x{},y{},w{},h{};};
struct Item {bool visible{},bar{};float width{},height{};};
inline Layout Defaults(){
    Layout l{};l[0]={Top,0};l[1]={Left,0};l[2]={Bottom,0};l[3]={Bottom,1};l[4]={Bottom,2};l[5]={Bottom,3};
    for(int i=0;i<FlagCount;++i)l[6+i]={Right,i};
    return l;
}
inline Side Nearest(float x,float y,const Rect& b){
    const float d[]={std::abs(x-b.x),std::abs(x-b.x-b.w),std::abs(y-b.y),std::abs(y-b.y-b.h)};
    return static_cast<Side>(std::min_element(d,d+4)-d);
}
inline std::array<Rect,Count> Arrange(const Rect& b,const std::array<Item,Count>& items,const Layout& layout){
    std::array<Rect,Count> result{};std::array<float,4> bars{},rows{};
    std::array<int,Count> sorted{};for(int i=0;i<Count;++i)sorted[i]=i;
    std::stable_sort(sorted.begin(),sorted.end(),[&](int a,int c){return layout[a].order<layout[c].order;});
    // Bars get the inner lanes; labels never overlap a bar, independent of drag order.
    for(int id:sorted){const auto& i=items[id];if(!i.visible || !i.bar)continue;
        const int side=std::clamp(static_cast<int>(layout[id].side),0,3);const float lane=bars[side];
        if(side==Left)result[id]={b.x-7.f-lane,b.y,4.f,b.h};
        if(side==Right)result[id]={b.x+b.w+3.f+lane,b.y,4.f,b.h};
        if(side==Top)result[id]={b.x,b.y-7.f-lane,b.w,4.f};
        if(side==Bottom)result[id]={b.x,b.y+b.h+3.f+lane,b.w,4.f};
        bars[side]+=7.f;
    }
    for(int id:sorted){const auto& i=items[id];if(!i.visible || i.bar)continue;
        const int side=std::clamp(static_cast<int>(layout[id].side),0,3);const float row=rows[side];
        if(side==Left)result[id]={b.x-5.f-bars[side]-i.width,b.y+row,i.width,i.height};
        if(side==Right)result[id]={b.x+b.w+5.f+bars[side],b.y+row,i.width,i.height};
        if(side==Top)result[id]={b.x+(b.w-i.width)*.5f,b.y-4.f-bars[side]-row-i.height,i.width,i.height};
        if(side==Bottom)result[id]={b.x+(b.w-i.width)*.5f,b.y+b.h+4.f+bars[side]+row,i.width,i.height};
        rows[side]+=i.height+2.f;
    }
    return result;
}
inline void Move(Layout& l,int id,Side side,int row){
    if(id<0 || id>=Count)return;
    const int target=std::clamp(row,0,Count-1);
    for(int i=0;i<Count;++i)if(i!=id && l[i].side==side && l[i].order>=target)++l[i].order;
    l[id]={side,target};
    // Renumber after every drop so repeated dragging cannot overflow orders.
    std::array<int,Count> ids{};for(int i=0;i<Count;++i)ids[i]=i;
    std::stable_sort(ids.begin(),ids.end(),[&](int a,int b){return l[a].order<l[b].order;});
    int n[4]{};for(int i:ids)l[i].order=n[std::clamp(static_cast<int>(l[i].side),0,3)]++;
}
}
