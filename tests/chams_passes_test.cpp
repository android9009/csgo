#include "features/visuals/chams_passes.hpp"
#include "features/visuals/fatality_material_definitions.hpp"
#include <cassert>
#include <cmath>
#include <string_view>
int main(){
    VisualPreview::Material choice;choice.enabled=true;choice.primary=0x80402010;choice.secondary=0x4000ff80;choice.hidden=0x60203040;
    for(int kind=0;kind<ChamsPasses::KindCount;++kind){
        choice.kind=kind;choice.ignore_z=false;
        auto visible=ChamsPasses::Build(choice,false);
        if(kind!=2 && kind!=7 && kind!=ChamsPasses::Shaded && kind!=ChamsPasses::Metallic){assert(visible.count==0);assert(ChamsPasses::Selection(kind)==0);continue;}
        const bool composite=kind==7;
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
        if(composite)assert(live.passes[0].kind==2 && live.passes[visible.count].kind==2);
    }
    choice.kind=7;choice.enabled=false;assert(ChamsPasses::Build(choice,true).count==0);
    auto tint=ChamsPasses::Tint(0x8000ff00,true);
    assert(tint[0]==0.f && std::abs(tint[1]-128.f/255.f)<.0001f && tint[2]==0.f);
    tint=ChamsPasses::Tint(0x00ffffff,true);assert(tint[0]==0.f && tint[1]==0.f && tint[2]==0.f);
    assert(ChamsPasses::ShadedExponent(0x00ffffff)==255);
    assert(ChamsPasses::ShadedExponent(0xffffffff)==1);
    assert(ChamsPasses::MetallicFresnel(0x00ffffff)==1.f);
    assert(ChamsPasses::MetallicFresnel(0xffffffff)==0.f);
    assert(ChamsPasses::FreshSnapshot(1.f,1.2f));
    assert(!ChamsPasses::FreshSnapshot(1.f,1.3f));
    assert(!ChamsPasses::FreshSnapshot(2.f,1.f));
    assert(ChamsPasses::GhostFade(0.f)==1.f && ChamsPasses::GhostFade(.5f)==1.f);
    assert(ChamsPasses::GhostFade(.8f)>.49f && ChamsPasses::GhostFade(.8f)<.51f);
    assert(ChamsPasses::GhostFade(1.f)==0.f && ChamsPasses::GhostFade(-1.f)==0.f);
    using sv=std::string_view;
    assert(ChamsPasses::Selection(8)==0 && ChamsPasses::Selection(11)==0);
    assert(sv(FatalityMaterialDefinition::shaded).find("$phongtint")!=sv::npos);
    assert(sv(FatalityMaterialDefinition::metallic).find("editor/cube_vertigo_blurred_hdr")!=sv::npos);
    assert(sv(FatalityMaterialDefinition::flat).find("UnlitGeneric")!=sv::npos);
}
