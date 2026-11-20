#pragma once
// The MOVEMENT group box of the Misc tab (id misc>movement): auto strafe, bhop, quick stop,
// edge jump and edge stop.
//
// One folder per menu group box, one header for the folder, one translation unit per
// feature. This header holds the façade and every portable rule, so the parts that decide
// what is written into the game's own CUserCmd can be checked on a host with no game.
//
// Provenance, which is the whole point of the two blocks below:
//   - auto strafe and bhop are ports of this repository's `main` branch
//     (movement.cpp: c_movement::auto_strafe / c_movement::auto_jump), which is the only
//     reference for this client that exists here;
//   - quick stop is main's c_movement::fast_stop + c_movement::instant_stop;
//   - edge jump and edge stop have no reference implementation (main's
//     c_movement::edge_jump is an empty stub), so they are written here from scratch on
//     top of an engine trace, and documented as this build's own.
//   - "No fall damage" and "Instant duck" are *not* implemented and their menu items are
//     gone: CS:GO decides fall damage on the server from the velocity the client reports
//     at the landing tick, so no client-side input removes it, and nothing in `main` does
//     either. Shipping a checkbox that pretends otherwise is the one thing this codebase
//     does not do.
//
// Everything runs from the CreateMove detour on the game thread (sdk/hooks/hooks.cpp,
// ClientMode slot 24 - the index `main` uses for the same vtable).
#include <atomic>
#include <cmath>
#include <cstdint>

#include "../../../utils/math/vector.hpp"

class CUserCmd;

namespace Misc::Movement {
struct Settings {
    bool autostrafe{};
    // Bits of menu/tabs/tab_misc.cpp's "Auto strafe" combo box.
    bool strafe_viewangles{};        // "Viewangles": the look direction drives the strafe
    bool strafe_movement_keys{};     // "Movement keys": the pressed keys drive it
    bool strafe_easy{};              // "Easy strafe": ease the wish towards the view
    bool strafe_jump_release{};      // "Jump release (hold)": only while jump is held
    bool strafe_jump_release_after{}; // "Jump release (release)": only after a real release
    int smoothing{};      // 0..100, the "Smoothing" slider
    bool avoid_walls{};

    bool bhop{};
    bool quick_stop{};    // "Quick stop" - cfg.legit.quick_stop, the menu item is in this group
    bool edge_jump{};
    bool edge_stop{};
};

void Publish(const Settings& settings);
// Called from the ClientMode::CreateMove detour, after the original ran.
void CreateMove(CUserCmd* cmd);
void Shutdown();

// ---------------------------------------------------------------------------
// Per-tick shared state and the pieces. Each feature lives in its own translation unit and
// is called with the tick the façade collected once, so none of them re-reads the local
// player.
// ---------------------------------------------------------------------------
namespace Detail {
struct Tick {
    CUserCmd* cmd{};
    bool on_ground{};
    // The player's own input, captured before anything in this module touches the command.
    bool jump_held{};
    bool jump_pressed{};   // the button went down on this very tick
    bool key_forward{};
    bool key_back{};
    bool key_left{};
    bool key_right{};
    bool on_ladder{};
    bool noclip{};
    int ground_ticks{};
    float velocity_x{};
    float velocity_y{};
    float speed{};
    float view_yaw{};
    float forward_speed{};
    float side_speed{};
    Vector origin{};   // the local player's feet, used by the wall and edge probes
};

struct Runtime {
    std::atomic<bool> autostrafe{false};
    std::atomic<bool> strafe_viewangles{false};
    std::atomic<bool> strafe_movement_keys{false};
    std::atomic<bool> strafe_easy{false};
    std::atomic<bool> strafe_jump_release{false};
    std::atomic<bool> strafe_jump_release_after{false};
    std::atomic<bool> avoid_walls{false};
    std::atomic<int> smoothing{0};
    std::atomic<bool> bhop{false};
    std::atomic<bool> quick_stop{false};
    std::atomic<bool> edge_jump{false};
    std::atomic<bool> edge_stop{false};
};

Runtime& GetRuntime();

void RunEdge(const Tick& tick);       // edge.cpp
void RunBhop(const Tick& tick);       // bhop.cpp
void RunAutoStrafe(const Tick& tick); // autostrafe.cpp
// Drops the strafer's own state (the jump gate's edges and the "Easy strafe" easing); used
// when the module is switched off or unloaded so the next jump starts clean.
void ResetAutoStrafe();
void RunQuickStop(const Tick& tick);  // quickstop.cpp
} // namespace Detail

// ---------------------------------------------------------------------------
// Shared math. Yaw is degrees, +x forward, the same convention the engine uses.
// ---------------------------------------------------------------------------
constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadToDeg = 180.f / kPi;
constexpr float kDegToRad = kPi / 180.f;

inline float NormalizeYaw(float yaw) {
    if (!(yaw > -360.f && yaw < 360.f)) {
        if (!std::isfinite(yaw)) return 0.f;
        yaw = std::fmod(yaw, 360.f);
    }
    if (yaw > 180.f) yaw -= 360.f;
    if (yaw < -180.f) yaw += 360.f;
    return yaw;
}

inline float YawOfVelocity(float velocity_x, float velocity_y) {
    return std::atan2(velocity_y, velocity_x) * kRadToDeg;
}

// ---------------------------------------------------------------------------
// Auto strafe (autostrafe.cpp) - `main`: c_movement::auto_strafe
//
// `main` feeds the wish direction through its own view angle and picks the side that
// accelerates: the forward speed is 5850 / speed (the classic air-acceleration term), the
// side is the full cl_sidespeed with the sign of the angle between the velocity and the
// view, and the view is then rotated by `-diff * smooth` so the next tick strafes the other
// way. This is the same function with the view rotation returned instead of applied.
// ---------------------------------------------------------------------------
constexpr float kStrafeGain = 5850.f;   // main: HACKS->cmd->forwardmove = 5850 / speed
constexpr float kMinStrafeSpeed = 0.5f; // below this there is no direction to strafe along

struct StrafeWish {
    float forward{};
    float side{};
    float view_yaw{};   // the yaw the *next* tick wants
    bool valid{};
};

inline float StrafeSmoothFactor(int smoothing_percent) {
    const int clamped = smoothing_percent < 0 ? 0 : (smoothing_percent > 100 ? 100 : smoothing_percent);
    // main: smooth = 1 - 0.15 * (strafe_smooth * 0.01)
    return 1.f - 0.15f * (static_cast<float>(clamped) * 0.01f);
}

inline StrafeWish AirStrafeWish(float velocity_x, float velocity_y, float view_yaw,
                                float forward_speed, float side_speed, float smooth) {
    StrafeWish wish;
    wish.view_yaw = NormalizeYaw(view_yaw);
    const float speed = std::sqrt(velocity_x * velocity_x + velocity_y * velocity_y);
    if (!(speed > kMinStrafeSpeed)) {
        // main starts the player off by pushing forward; there is no velocity to align to.
        wish.forward = forward_speed;
        return wish;
    }
    const float diff = NormalizeYaw(view_yaw - YawOfVelocity(velocity_x, velocity_y));
    float forward = kStrafeGain / speed;
    if (forward > forward_speed) forward = forward_speed;
    if (forward < -forward_speed) forward = -forward_speed;
    wish.forward = forward;
    wish.side = diff > 0.f ? -side_speed : side_speed;
    wish.view_yaw = NormalizeYaw(view_yaw - diff * smooth);
    wish.valid = true;
    return wish;
}

// Source reads forwardmove/sidemove in the command's OWN view frame (its yaw is
// cmd->view_angles.y), so a wish computed in a different frame has to be rotated into that
// one. Rotating the movement - instead of writing the command's angles and restoring the
// engine's afterwards - is why the camera does not move: `main`'s auto strafe never writes
// view angles either, it only turns its own base angle for the wish maths.
struct MoveVector {
    float forward{};
    float side{};
};

inline MoveVector RotateIntoViewFrame(float forward, float side, float wish_yaw, float view_yaw) {
    const float delta = (NormalizeYaw(wish_yaw) - NormalizeYaw(view_yaw)) * kDegToRad;
    const float c = std::cos(delta);
    const float s = std::sin(delta);
    // forward' = f*cos(d) + s*sin(d), side' = -f*sin(d) + s*cos(d).
    return MoveVector{forward * c + side * s, -forward * s + side * c};
}

// ---------------------------------------------------------------------------
// Air strafe modes (autostrafe.cpp)
//
// Direction and permission are two different things and are kept apart here:
//   - `StrafeSource` picks where the *direction* comes from: the look direction
//     ("Viewangles") or the movement keys in the camera frame ("Movement keys");
//   - `JumpGate` decides whether the strafer may touch the command at all on this tick;
//   - "Easy strafe" only ever eases the direction, it never turns the camera.
// ---------------------------------------------------------------------------
enum class StrafeSource { Viewangles, MovementKeys };
enum class JumpGate { Off, WhileHeld, AfterRelease };

inline StrafeSource SourceFrom(const Settings& settings) {
    // "Movement keys" wins when both are selected: it is the more specific request, and the
    // menu shows the two as alternatives.
    return settings.strafe_movement_keys ? StrafeSource::MovementKeys : StrafeSource::Viewangles;
}

inline JumpGate GateFrom(bool while_held, bool after_release) {
    if (after_release) return JumpGate::AfterRelease;
    if (while_held) return JumpGate::WhileHeld;
    return JumpGate::Off;
}

// Yaw of the physical movement keys in the camera frame. `W+D` gives the forward-right
// diagonal, `S` points backwards, opposite keys cancel each other out, and no keys at all
// gives `valid == false` - the strafer must then leave the input alone instead of picking a
// direction of its own. Source's yaw grows to the left, hence the minus.
struct KeyDirection {
    float yaw{};
    bool valid{};
};

inline KeyDirection MovementKeyDirection(float view_yaw, bool forward, bool back, bool left,
                                         bool right) {
    const float f = (forward ? 1.f : 0.f) - (back ? 1.f : 0.f);
    const float s = (right ? 1.f : 0.f) - (left ? 1.f : 0.f);
    if (f == 0.f && s == 0.f) return KeyDirection{};
    return KeyDirection{NormalizeYaw(view_yaw - std::atan2(s, f) * kRadToDeg), true};
}

// Shortest-arc rotation from `current` towards `target`, limited to `max_step` degrees, so a
// 179 -> -179 crossing is a two degree turn and not almost a full circle.
inline float RotateTowardsYaw(float current, float target, float max_step) {
    const float delta = NormalizeYaw(target - current);
    if (!std::isfinite(delta)) return NormalizeYaw(current);
    const float step = max_step < 0.f ? 0.f : max_step;
    if (std::fabs(delta) <= step) return NormalizeYaw(target);
    return NormalizeYaw(current + (delta > 0.f ? step : -step));
}

// "Smoothing" slider (0..100) as a per-tick turn limit for "Easy strafe": 20 degrees at 0,
// 2 degrees at 100. It is a turn rate for the wish, not for the camera.
inline float EasyStrafeStep(int smoothing_percent) {
    const int clamped =
        smoothing_percent < 0 ? 0 : (smoothing_percent > 100 ? 100 : smoothing_percent);
    return 20.f - 18.f * (static_cast<float>(clamped) * 0.01f);
}

// The jump gate. `pressed` and `released` are edges of this airtime: they are cleared when
// the player is back on the ground, so "released" can only be true for a real press-and-
// release in the air and never for "the space bar was never touched at all".
struct JumpGateState {
    bool pressed_in_air{};
    bool was_held{};
    bool released_in_air{};

    void Reset() { *this = JumpGateState{}; }

    void Update(bool held, bool on_ground) {
        if (on_ground) {
            Reset();
            was_held = held;
            return;
        }
        if (held && !was_held) pressed_in_air = true;
        if (!held && was_held && pressed_in_air) released_in_air = true;
        was_held = held;
    }

    bool Allows(JumpGate gate, bool held) const {
        switch (gate) {
        case JumpGate::WhileHeld:
            return held;
        case JumpGate::AfterRelease:
            return pressed_in_air && released_in_air && !held;
        case JumpGate::Off:
        default:
            return true;
        }
    }
};

// The direction the wish is computed against, after the source and "Easy strafe" have had
// their say. `steer` is the persistent state of the easing and is reset on landing.
struct StrafeDirection {
    float yaw{};
    bool valid{};
};

inline StrafeDirection ResolveStrafeDirection(const StrafeSource source, const Detail::Tick& tick,
                                              const KeyDirection& keys, bool easy, float max_step,
                                              float& steer, bool& steer_valid) {
    StrafeDirection direction;
    if (source == StrafeSource::MovementKeys) {
        if (!keys.valid) return direction; // no keys: no direction, not a guessed one
        direction = StrafeDirection{keys.yaw, true};
    } else {
        direction = StrafeDirection{NormalizeYaw(tick.view_yaw), true};
    }
    if (!easy) return direction;
    if (!steer_valid) {
        steer = direction.yaw;
        steer_valid = true;
    }
    // "Easy strafe" eases the direction towards the look direction: the wish follows the
    // view over a few ticks instead of snapping, and the camera is never written.
    steer = RotateTowardsYaw(steer, tick.view_yaw, max_step);
    return StrafeDirection{steer, true};
}

// ---------------------------------------------------------------------------
// Bhop (bhop.cpp) - `main`: c_movement::auto_jump
//
// The state machine exists because a jump has to be released for one tick before the next
// one is accepted; holding IN_JUMP permanently makes the game drop every second jump and
// costs speed. The portable form keeps main's two flags and returns what to do with the
// button this tick.
// ---------------------------------------------------------------------------
struct BhopState {
    bool last_jumped{};
    bool should_fake{};
};

struct BhopInput {
    bool jump_button{};  // the player is holding the jump key
    bool on_ground{};
    bool on_ladder{};
    bool noclip{};
};

struct BhopOutput {
    bool force_jump{};   // add IN_JUMP for this tick
    bool remove_jump{};  // take IN_JUMP away for this tick
};

inline BhopOutput AdvanceBhop(BhopState& state, const BhopInput& input) {
    BhopOutput output;
    if (input.on_ladder || input.noclip) return output;
    if (!state.last_jumped && state.should_fake) {
        state.should_fake = false;
        output.force_jump = true;
        return output;
    }
    if (input.jump_button) {
        if (input.on_ground) {
            state.should_fake = true;
            state.last_jumped = true;
        } else {
            state.last_jumped = false;
            output.remove_jump = true;
        }
    } else {
        state.should_fake = false;
        state.last_jumped = false;
    }
    return output;
}

// ---------------------------------------------------------------------------
// Quick stop (quickstop.cpp) - `main`: c_movement::fast_stop + c_movement::instant_stop
//
// Standing still fast is counter-strafing: point the movement the opposite way to the
// velocity and push at full speed. main reads the velocity's yaw, subtracts it from the
// view yaw, turns that into a direction and scales it by -speed.
// ---------------------------------------------------------------------------
constexpr float kStopSpeedThreshold = 15.f; // main: complete when length_2d() <= 15
constexpr int kGroundTicksForStop = 3;      // main: on_ground() == ground_ticks >= 3

struct StopWish {
    float forward{};
    float side{};
    bool valid{};
};

inline StopWish InstantStopWish(float velocity_x, float velocity_y, float view_yaw) {
    StopWish wish;
    const float speed = std::sqrt(velocity_x * velocity_x + velocity_y * velocity_y);
    if (!(speed > 0.f) || !std::isfinite(speed)) return wish;
    const float angle = NormalizeYaw(view_yaw - YawOfVelocity(velocity_x, velocity_y));
    wish.forward = -speed * std::cos(angle * kDegToRad);
    wish.side = -speed * std::sin(angle * kDegToRad);
    wish.valid = true;
    return wish;
}

// main's fast_stop guard: on the ground long enough, no steering held, not already stopped.
inline bool ShouldQuickStop(bool enabled, int ground_ticks, bool steering, bool completed) {
    if (!enabled || completed) return false;
    if (ground_ticks < kGroundTicksForStop) return false;
    return !steering;
}

// ---------------------------------------------------------------------------
// Edge jump / edge stop (edge.cpp) - this build's own, no reference exists.
//
// Both need the same question answered: "is the ground under my feet ending right here?".
// A short trace along the direction of travel answers it. edge.cpp does the tracing; the
// decision on top of it is pure and lives here.
// ---------------------------------------------------------------------------
constexpr float kEdgeProbeDistance = 24.f;  // how far ahead of the feet the probe lands
constexpr float kEdgeDrop = 16.f;           // a step down deeper than this is an edge
constexpr float kEdgeMinSpeed = 90.f;       // slower than this is walking off, not jumping

struct EdgeProbe {
    bool hit_ground{};   // the downward probe found floor
    float drop{};        // how far below the feet that floor is, in units
    float speed{};       // horizontal speed this tick
};

inline bool IsEdge(const EdgeProbe& probe) {
    return !probe.hit_ground || probe.drop > kEdgeDrop;
}

// Jumping off the edge: only worth doing while actually moving, otherwise the player is
// standing at the edge and a jump would just be a jump.
inline bool ShouldEdgeJump(bool enabled, const EdgeProbe& probe) {
    if (!enabled) return false;
    return IsEdge(probe) && probe.speed > kEdgeMinSpeed;
}

// Not walking off it: the same edge, but the player wants to stay. The wish is the full
// counter-strafe, so the movement stops before the feet leave the floor.
inline bool ShouldEdgeStop(bool enabled, const EdgeProbe& probe) {
    if (!enabled) return false;
    return IsEdge(probe) && probe.speed > kStopSpeedThreshold;
}
} // namespace Misc::Movement
