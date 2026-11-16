#include "features/visuals/chams_passes.hpp"
#include "features/visuals/fatality_material_definitions.hpp"
#include <cassert>
#include <cmath>
#include <string_view>
int main(){
    VisualPreview::Material choice;choice.enabled=true;choice.primary=0x80402010;choice.secondary=0x4000ff80;choice.hidden=0x60203040;
    for(int kind=1;kind<=7;++kind){
        choice.kind=kind;choice.ignore_z=false;
        auto visible=ChamsPasses::Build(choice,false);
        const bool composite=kind==3 || kind==4 || kind==5 || kind==7;
        assert(visible.count==(composite ? 2 : 1));
        assert(visible.passes[visible.count-1].kind==kind);
        assert(visible.passes[visible.count-1].secondary==choice.secondary);
        choice.ignore_z=true;
        auto live=ChamsPasses::Build(choice,true);
        assert(live.count==visible.count*2);
        for(int i=0;i<visible.count;++i){
            assert(live.passes[i].ignore_z);
            assert(live.passes[i].primary==choice.hidden);
            assert((live.passes[i].secondary&0xffffffu)==(choice.hidden&0xffffffu));
            assert(!live.passes[i+visible.count].ignore_z);
            assert(live.passes[i+visible.count].kind==visible.passes[i].kind);
        }
        assert(ChamsPasses::Build(choice,false).count==visible.count);
        if(composite)assert(live.passes[0].kind==(kind==4 ? 1 : 2) && live.passes[visible.count].kind==live.passes[0].kind);
    }
    choice.kind=8;assert(ChamsPasses::Build(choice,true).count==0);
    choice.kind=3;choice.enabled=false;assert(ChamsPasses::Build(choice,true).count==0);
    auto tint=ChamsPasses::Tint(0x8000ff00,true);
    assert(tint[0]==0.f && std::abs(tint[1]-128.f/255.f)<.0001f && tint[2]==0.f);
    tint=ChamsPasses::Tint(0x00ffffff,true);assert(tint[0]==0.f && tint[1]==0.f && tint[2]==0.f);
    using namespace FatalityMaterialDefinition;using sv=std::string_view;
    assert(sv(normal).find("$selfillum")==sv::npos && sv(normal).find("$flat")==sv::npos);
    assert(sv(normal).find("VertexLitGeneric")!=sv::npos);
    assert(sv(flat).find("UnlitGeneric")!=sv::npos && sv(flat).find("$phong")==sv::npos);
    assert(sv(rim_glow).find("$envmapfresnelminmaxexp")!=sv::npos);
    assert(sv(contour).find("[0 1 6]")!=sv::npos);
    assert(sv(flow).find("TextureScroll")!=sv::npos && sv(flow).find("$additive")!=sv::npos);
    assert(sv(acrylic).find("$phongtint")!=sv::npos && sv(acrylic).find("effects/flat_normal")!=sv::npos);
}
