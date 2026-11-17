#pragma once
#include "../../utils/math/vector.hpp"
#include <cmath>
namespace PredictionCompression {
// Correct only network quantization noise; never hide a real server correction.
inline bool Restore(Vector& live,const Vector& saved,float tolerance){
    if(!live.IsValid() || !saved.IsValid() || !std::isfinite(tolerance) || tolerance<0.f)return false;
    if(std::abs(live.x-saved.x)>tolerance || std::abs(live.y-saved.y)>tolerance || std::abs(live.z-saved.z)>tolerance)return false;
    live=saved;return true;
}
inline bool Restore(float& live,float saved,float tolerance){
    if(!std::isfinite(live) || !std::isfinite(saved) || !std::isfinite(tolerance) || tolerance<0.f || std::abs(live-saved)>tolerance)return false;
    live=saved;return true;
}
}
