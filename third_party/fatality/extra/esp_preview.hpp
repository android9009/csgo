#pragma once
#include "esp_drawing.hpp"
#include "visual_runtime.hpp"
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
        hits=EspDrawing::Render(d,model_box,style,{},geometry.skeleton,geometry.segments,extent,viewport.tl());
        if(dragging>=0){
            const auto side=EspDock::Nearest(input.cursor().x,input.cursor().y,{model_box.tl().x,model_box.tl().y,model_box.width(),model_box.height()});
            const auto a=side==EspDock::Left ? model_box.tl() : side==EspDock::Right ? model_box.tr() : side==EspDock::Top ? model_box.tl() : model_box.bl();
            const auto z=(side==EspDock::Left || side==EspDock::Right) ? vec2{0.f,model_box.height()} : vec2{model_box.width(),0.f};
            d->add_line(a,a+z,colors.accent,2.f);
        }
        for(const auto& hit:hits)if(hit.width()>0.f && hit.contains(input.cursor()))d->add_rect(hit,colors.accent.mod_a(.6f));
        d->g.clip_rect=old;
    }
    bool down(char key){
        for(int i=EspDock::Count-1;i>=0;--i)if(hits[i].width()>0.f && hits[i].contains(input.cursor())){
            if(key==m_right){VisualPreview::ResetDock(i);return true;}
            if(key==m_left){dragging=i;return true;}
        }return false;
    }
    bool move(const vec2&,const vec2&){
        if(dragging<0)return false;
        const auto cursor=input.cursor();const EspDock::Rect b{model_box.tl().x,model_box.tl().y,model_box.width(),model_box.height()};
        const auto side=EspDock::Nearest(cursor.x,cursor.y,b);
        int order=0;
        const auto layout=VisualPreview::GetLayout();
        // Insert among actual visible neighbors, never rotate the label glyphs.
        for(int i=0;i<EspDock::Count;++i)if(i!=dragging && hits[i].width()>0.f && layout[i].side==side && cursor.y>hits[i].center().y)++order;
        VisualPreview::SetDock(dragging,side,order);return true;
    }
    void up(){dragging=-1;}
private:
    std::array<rect,EspDock::Count> hits{};rect model_box;int dragging{-1};
};
}
