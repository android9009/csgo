#pragma once

// Engine trace + cvar access for the grenade prediction. cvars.cpp owns the
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
    // The prediction mirrors Airflow's c_game_trace: the entity the hull hit
    // (nullptr for the world), the surface contents at the impact point (water
    // currents are what the start_solid re-trace looks for) and all_solid.
    void* entity = nullptr;
    unsigned int contents = 0;
    bool all_solid = false;
};

// Swept trace with a caller-chosen hull and contents mask (Fatality's util_trace_hull).
// World geometry only: the filter never reports entities. Used by the camera
// thirdperson probe.
bool trace_swept(const visuals_source::vector3& from, const visuals_source::vector3& to,
                 const visuals_source::vector3& mins, const visuals_source::vector3& maxs,
                 unsigned int mask, swept_trace& out);

// Airflow's grenade sweep: same TraceRay call, but the filter reports every
// entity except `skip_entity` (the thrower), so the hull bounces off players,
// props and breakable windows the way the engine's own projectile does.
bool trace_swept_entities(const visuals_source::vector3& from, const visuals_source::vector3& to,
                          const visuals_source::vector3& mins, const visuals_source::vector3& maxs,
                          unsigned int mask, void* skip_entity, swept_trace& out);

// A client cvar as a float; `fallback` while the cvar is missing.
float world_convar(const char* name, float fallback);

// --- grenade prediction, game-thread half (grenade_trajectory.cpp) -------------
// Both are driven by the FrameStageNotify hook in source.cpp. The Present hook
// never simulates: it only draws what these published into the frame snapshot.

// End of the frame capture (FRAME_RENDER_END): runs / refreshes the physics for
// the grenade in hand and the projectiles in `frame.world`, then writes the arcs
// into `frame.grenades`.
void update_grenade_predictions(visuals_source::frame_data& frame);

// FRAME_RENDER_START: hands the current arcs to the engine debug overlay
// (Trajectory > Engine Beams), once per tick.
void submit_grenade_beams();

} // namespace world_visuals
