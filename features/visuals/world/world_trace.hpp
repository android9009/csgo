#pragma once

// Engine trace + cvar access for the grenade prediction. world_cvars.cpp owns the
// TraceRay call and the client cvar interface, so both are exposed here instead of
// duplicating the vtable layout in the trajectory translation unit.
#include "../dependencies/source.hpp"

namespace world_visuals {

struct swept_trace {
    bool hit = false;
    bool start_solid = false;
    float fraction = 1.f;
    visuals_source::vector3 end{};
    visuals_source::vector3 normal{};
};

// Swept trace with a caller-chosen hull and contents mask (Fatality's util_trace_hull).
bool trace_swept(const visuals_source::vector3& from, const visuals_source::vector3& to,
                 const visuals_source::vector3& mins, const visuals_source::vector3& maxs,
                 unsigned int mask, swept_trace& out);

// A client cvar as a float; `fallback` while the cvar is missing.
float world_convar(const char* name, float fallback);

} // namespace world_visuals
