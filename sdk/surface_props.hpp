#pragma once
#include "../utils/memory.hpp"
#include <cstddef>
#include <cstdint>
// Source surfacephysicsparams_t, surfaceaudioparams_t and
// surfacesoundnames_t precede surfacegameprops_t in VPhysicsSurfaceProps001.
struct SurfaceData {
    float physics[5];
    float audio[9];
    std::uint16_t sounds[12];
    float speed_factor,jump_factor,penetration_modifier,damage_modifier;
    std::uint16_t material;
    std::uint8_t climbable,pad;
};
static_assert(offsetof(SurfaceData,penetration_modifier)==0x58,"Source surface game properties");
class IPhysicsSurfaceProps {
public:
    const SurfaceData* GetSurfaceData(int index){return MEM::GetVFunc<const SurfaceData*(__thiscall*)(void*,int)>(this,5)(this,index);}
};
