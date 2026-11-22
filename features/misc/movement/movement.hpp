#pragma once
// MOVEMENT group box of the Misc tab. Logic ported from this repository's `main` branch
// (airflow) c_movement: auto_jump, auto_strafe, fast_stop/instant_stop, edge forecast.
// `main` drives it from a per-fake-command prediction loop; this build has no prediction
// loop, so the same functions run once per real command from the CreateMove detour.
#include <cstdint>
#include "../../../utils/math/vector.hpp"
#include "../../../utils/math/qangle.hpp"

class CUserCmd;

namespace Misc::Movement {
// Bits of menu/tabs/tab_misc.cpp "Air strafe" combo box (cfg_t::autostrafe_*).
struct Settings {
    bool autostrafe{};            // any air-strafe mode selected
    bool strafe_viewangles{};     // "Viewangles": the look direction drives the strafe
    bool strafe_movement_keys{};  // "Movement keys": the held WASD keys steer it
    bool strafe_easy{};           // "Easy strafe": ease the direction, do not snap
    bool strafe_jump_release{};   // "Jump release (while held)": only while jump is held
    bool strafe_jump_release_after{}; // "Jump release (after release)": only after a real release
    int smoothing{};              // 0..100, "Smoothing" slider
    bool avoid_walls{};           // "Avoid walls"

    bool bhop{};                  // "Bhop"
    bool quick_stop{};            // "Quick stop" (cfg.legit.quick_stop)
    bool edge_jump{};             // "Edge jump" (experimental)
    bool edge_stop{};             // "Edge stop" (experimental)

    // MOVEMENT EXTRA group box
    bool long_jump{};             // "Long jump": jump-duck assist after takeoff
    bool ladder_jump{};           // "Ladder jump": dismount assist while climbing
};

// Per-tick shared state, mirrored from `main` c_movement's member fields. The movement
// functions are separate translation units, so the state lives here and is handed to them.
struct State {
    // bhop (auto_jump)
    bool last_jumped = false;
    bool should_fake = false;

    // quick stop (fast_stop)
    bool complete_fast_stop = false;

    // ground tracking
    int ground_ticks = 0;

    // auto strafe
    Vector base_view_angle{};   // the running strafe reference (yaw only is used)

    // air-strafe jump gate (our menu's "Jump release" items; `main` has no such gate)
    bool jump_was_held = false;
    bool jump_pressed_in_air = false;
    bool jump_released_in_air = false;

    void Reset() {
        last_jumped = should_fake = false;
        complete_fast_stop = false;
        ground_ticks = 0;
        base_view_angle = Vector{};
        jump_was_held = jump_pressed_in_air = jump_released_in_air = false;
    }
};

// The per-tick snapshot the façade collects once and hands to each feature, so none of them
// re-reads the local player.
struct Tick {
    CUserCmd* cmd{};
    Vector velocity{};
    Vector origin{};
    QAngle view{};
    int flags{};
    int move_type{};
    bool on_ground{};
    bool on_ladder{};
    bool noclip{};
    bool jump_held{};       // the raw player jump button, captured before any feature edits it
    int ground_ticks{};
};

void Publish(const Settings& settings);
// Called from the CreateMove detour after the original ran.
void CreateMove(CUserCmd* cmd);
void Reset();
void Shutdown();

namespace Detail {
// The published settings, read by the per-feature translation units.
const Settings& Published();

// One pass each, called in `main`'s order.
void UpdateGroundTicks(Tick& tick, State& state);   // movement.cpp
void UpdateJumpGate(const Tick& tick, State& state); // movement.cpp
void AutoJump(const Tick& tick, State& state);      // bhop.cpp
void AutoStrafe(const Tick& tick, State& state);    // autostrafe.cpp
void FastStop(const Tick& tick, State& state);      // quickstop.cpp
void InstantStop(const Tick& tick);                 // quickstop.cpp
void Edge(const Tick& tick, State& state);          // edge.cpp
void Extras(const Tick& tick, State& state);        // extras.cpp (long jump + ladder jump)
void RotateMovement(CUserCmd* cmd, const QAngle& ang); // movement.cpp
} // namespace Detail
} // namespace Misc::Movement
