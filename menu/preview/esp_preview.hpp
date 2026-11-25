#pragma once
#include "../overlays/esp_drawing.hpp"
#include "../bridges/visual_runtime.hpp"
namespace EspPreview {
using namespace evo::gui;using namespace evo::ren;
class Overlay {
public:
    void render(std::shared_ptr<layer>& d,const rect& viewport){
        hits.fill({});
        if(!cfg.visuals.esp.enabled.get())return;
        const auto geometry=VisualPreview::GetGeometry();if(!geometry.valid)return;
        const vec2 extent{viewport.width(),viewport.height()};const auto& b=geometry.box;
        model_box={viewport.tl()+vec2{b.left*extent.x,b.top*extent.y},viewport.tl()+vec2{b.right*extent.x,b.bottom*extent.y}};
        const auto old=d->g.clip_rect;d->override_clip_rect(viewport);
        auto style=VisualSettings::ReadEsp(cfg.visuals.esp,cfg.visuals.esp.active_profile);
        // The grabbed element follows the cursor. Commit a dock only on release;
        // never shuffle the other elements on every mouse-move event.
        hits=EspDrawing::Render(d,model_box,style,{},geometry.skeleton,geometry.segments,extent,viewport.tl(),dragging,drag_delta);
        d->g.clip_rect=old;
    }
    bool down(char key){
        for(int i=EspDock::Count-1;i>=0;--i)if(hits[i].width()>0.f && hits[i].expand(3.f).contains(input.cursor())){
            if(key==m_right){VisualPreview::ResetDock(i);return true;}
            if(key==m_left){dragging=i;drag_start=input.cursor();drag_delta={};return true;}
        }return false;
    }
    bool move(const vec2&,const vec2&){
        if(dragging<0)return false;
        drag_delta=input.cursor()-drag_start;return true;
    }
    void up(){
        if(dragging<0)return;
        // A click is not a drop and must not reorder an existing slot.
        if(std::abs(drag_delta.x)+std::abs(drag_delta.y)>3.f){
            const auto cursor=input.cursor();
            const auto side=EspDock::Nearest(cursor.x,cursor.y,{model_box.tl().x,model_box.tl().y,model_box.width(),model_box.height()});
            const auto layout=VisualPreview::GetLayout();int order=0;
            for(int i=0;i<EspDock::Count;++i)if(i!=dragging && hits[i].width()>0.f && layout[i].side==side){
                // Top lanes grow upwards; all other label rows grow downwards.
                if(side==EspDock::Top ? cursor.y<hits[i].center().y : cursor.y>hits[i].center().y)++order;
            }
            VisualPreview::SetDock(dragging,side,order);
        }
        cancel();
    }
    void cancel(){dragging=-1;drag_delta={};}
private:
    std::array<rect,EspDock::Count> hits{};rect model_box;int dragging{-1};vec2 drag_start{},drag_delta{};
};
}
