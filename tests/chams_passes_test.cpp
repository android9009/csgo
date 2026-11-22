#include "features/visuals/chams_passes.hpp"
#include "features/visuals/chams_materials.hpp"
#include <cassert>
#include <cmath>
#include <string_view>
static bool supported(int kind) {
    return kind==1 || kind==2 || kind==5 || kind==6 || kind==ChamsPasses::GlowKind ||
        kind==8 || kind==ChamsPasses::Shaded || kind==ChamsPasses::Metallic;
}
int main(){
    VisualPreview::Material choice;choice.enabled=true;choice.primary=0x80402010;choice.secondary=0x4000ff80;choice.hidden=0x60203040;
    for(int kind=0;kind<ChamsPasses::KindCount;++kind){
        choice.kind=kind;choice.ignore_z=false;
        auto visible=ChamsPasses::Build(choice,false);
        if(!supported(kind)){assert(visible.count==0);assert(ChamsPasses::Selection(kind)==0);continue;}
        const bool composite=ChamsPasses::Composite(kind);
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
    choice.kind=ChamsPasses::GlowKind;choice.enabled=false;assert(ChamsPasses::Build(choice,true).count==0);
    auto tint=ChamsPasses::Tint(0x8000ff00,true);
    assert(tint[0]==0.f && std::abs(tint[1]-128.f/255.f)<.0001f && tint[2]==0.f);
    tint=ChamsPasses::Tint(0x00ffffff,true);assert(tint[0]==0.f && tint[1]==0.f && tint[2]==0.f);
    assert(ChamsPasses::GlowFill(0x00ffffff)==0.f);
    assert(ChamsPasses::GlowFill(0x27ffffff)>.0f && ChamsPasses::GlowFill(0x27ffffff)<.1f);
    assert(ChamsPasses::GlowFill(0xffffffff)==.85f);
    assert(ChamsPasses::FreshSnapshot(1.f,1.2f));
    assert(!ChamsPasses::FreshSnapshot(1.f,1.3f));
    assert(!ChamsPasses::FreshSnapshot(2.f,1.f));
    assert(ChamsPasses::GhostFade(0.f)==1.f && ChamsPasses::GhostFade(.5f)==1.f);
    assert(ChamsPasses::GhostFade(.8f)>.49f && ChamsPasses::GhostFade(.8f)<.51f);
    assert(ChamsPasses::GhostFade(1.f)==0.f && ChamsPasses::GhostFade(-1.f)==0.f);
    assert(ChamsPasses::UsesSecondary(ChamsPasses::GlowKind) && ChamsPasses::UsesSecondary(ChamsPasses::Metallic));
    assert(!ChamsPasses::UsesSecondary(2) && !ChamsPasses::UsesSecondary(ChamsPasses::Shaded));
    using sv=std::string_view;
    for(int i=0;i<ChamsMaterial::Count;++i){
        assert(ChamsMaterial::Shader(i));
        const char* vmt=ChamsMaterial::VMT(i);
        assert(sv(ChamsMaterial::Shader(i))==
            (i==ChamsMaterial::Flat ? "UnlitGeneric" : "VertexLitGeneric"));
        const char* body=ChamsMaterial::Parameters(vmt);assert(body);
        assert(sv(body).find("Generic")==sv::npos);
        assert(ChamsMaterial::Name(i));
    }
    assert(sv(ChamsMaterial::VMT(ChamsMaterial::Flat)).find("UnlitGeneric")!=sv::npos);
    assert(sv(ChamsMaterial::VMT(ChamsMaterial::Glow)).find("cube_white")!=sv::npos);
    assert(sv(ChamsMaterial::VMT(ChamsMaterial::World)).find("money")!=sv::npos);
    assert(!ChamsMaterial::Parameters("not a VMT"));
    assert(ChamsPasses::Selection(3)==0 && ChamsPasses::Selection(4)==0 && ChamsPasses::Selection(11)==0);
}
