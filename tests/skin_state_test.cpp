#include "features/skins/skin_state.hpp"
#include <cassert>
#include <limits>
#include <thread>
int main() {
    using namespace SkinState;
    assert(Definition("weapon_ak47") == 7 && Definition("weapon_awp") == 9 && Definition("nonsense") == 0);
    Choice a{}; a.paint_kit=180; a.seed=5; a.wear=.2f; a.stattrak=5;
    assert(Set(true,7,a)); Choice out{}; assert(Get(true,7,out) && out.paint_kit==180 && out.stattrak==5);
    assert(!Get(false,7,out)); auto first=out.revision;
    a.stattrak=-1; a.souvenir=true; assert(Set(false,7,a)); assert(Get(false,7,out) && out.souvenir && out.revision > first);
    a.stattrak=-2; a.souvenir=false; assert(Set(true,7,a)); assert(Get(true,7,out) && out.stattrak == -2);
    a.stattrak=-3; assert(!Set(true,7,a));
    a.souvenir=true; a.stattrak=1; assert(!Set(true,7,a)); a.souvenir=false; a.wear=std::numeric_limits<float>::quiet_NaN(); assert(!Set(true,7,a));
    std::thread t([]{ Choice c{}; c.paint_kit=2; for(int i=0;i<1000;++i) { Set(false,9,c); Choice x{}; assert(Get(false,9,x)); }});
    for (int i=0;i<1000;++i) { Choice x{}; assert(Get(true,7,x)); }
    t.join(); Remove(true,7); assert(!Get(true,7,out) && Get(false,7,out));
}
