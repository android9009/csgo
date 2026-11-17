#include "../features/misc/prediction_compression.hpp"
#include "../features/misc/state.hpp"
#include <cassert>
#include <limits>
int main(){
    using PredictionCompression::Restore;
    Vector live{1.01f,1.99f,3.02f};const Vector saved{1,2,3};
    assert(Restore(live,saved,.03125f) && live.x==1 && live.z==3);
    live={1.04f,2,3};assert(!Restore(live,saved,.03125f) && live.x==1.04f);
    live={1,2,std::numeric_limits<float>::quiet_NaN()};assert(!Restore(live,saved,.03125f));
    live=saved;assert(!Restore(live,saved,-1));
    float fall=600.49f;assert(Restore(fall,600,.5f) && fall==600);
    fall=601;assert(!Restore(fall,600,.5f) && fall==601);
    fall=std::numeric_limits<float>::infinity();assert(!Restore(fall,600,.5f));
    MiscRuntime::PeekMarker marker;static_assert(sizeof(marker)<=16,"marker must not carry a tick-rate view matrix");
    marker.valid=true;marker.origin={10,20,30};MiscRuntime::PublishPeekMarker(marker);
    assert(MiscRuntime::GetPeekMarker().origin.x==10);
    MiscRuntime::Settings settings;settings.reveal_ranks=true;MiscRuntime::Publish(settings);assert(MiscRuntime::Get().reveal_ranks);
}
