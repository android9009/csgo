#include "movement.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include <cmath>

namespace Misc::Movement::Detail {
namespace {
// "Avoid walls" (the slider box of the auto strafe settings): a short probe forward. When
// something is in the way the wish is left alone for this tick, because accelerating into a
// wall gains nothing and the rotation would push the player along it.
bool WallAhead(const Tick& tick) {
    if (!Interfaces::EngineTrace) return false;
    const Vector origin = tick.origin;
    const Vector forward{std::cos(tick.view_yaw * kDegToRad), std::sin(tick.view_yaw * kDegToRad), 0.f};
    Ray_t ray;
    ray.Init(origin, origin + forward * 32.f);
    CTraceFilterSimple filter(nullptr);
    CGameTrace trace;
    Interfaces::EngineTrace->TraceRay(ray, 0x200400B /* MASK_PLAYERSOLID */, &filter, &trace);
    return trace.fraction < 1.f;
}

// State that outlives a single tick. It is deliberately kept here and not in the tick: the
// easing and the jump gate are about what happened on the previous ticks.
struct GateAndSteer {
    JumpGateState jump{};
    float steer{};
    bool steer_valid{};

    void ResetSteer() {
        steer = 0.f;
        steer_valid = false;
    }
} g_state;
} // namespace

void RunAutoStrafe(const Tick& tick) {
    const auto& runtime = GetRuntime();

    // The jump gate is an *input* decision: it is evaluated before anything in this function
    // writes into the command, including the early exit for a player who is barely moving.
    // It therefore sees the button state the player had when the tick started.
    const JumpGate gate = GateFrom(runtime.strafe_jump_release.load(),
                                   runtime.strafe_jump_release_after.load());
    g_state.jump.Update(tick.jump_held, tick.on_ground);

    if (!runtime.autostrafe.load()) {
        g_state.ResetSteer();
        return;
    }
    if (tick.on_ground || tick.on_ladder || tick.noclip) {
        // The easing is an air-only state: landing starts it from scratch.
        g_state.ResetSteer();
        return;
    }
    if (!g_state.jump.Allows(gate, tick.jump_held)) return;
    if (tick.cmd->HasButton(IN_SPEED) || tick.cmd->HasButton(IN_WALK)) return;
    if (WallAhead(tick) && runtime.avoid_walls.load()) return;

    // 1. Which direction does the player want to move?
    const StrafeSource source = runtime.strafe_movement_keys.load() ? StrafeSource::MovementKeys
                                                                   : StrafeSource::Viewangles;
    const KeyDirection keys = MovementKeyDirection(tick.view_yaw, tick.key_forward, tick.key_back,
                                                   tick.key_left, tick.key_right);
    const bool easy = runtime.strafe_easy.load();
    const StrafeDirection direction =
        ResolveStrafeDirection(source, tick, keys, easy,
                               EasyStrafeStep(runtime.smoothing.load()), g_state.steer,
                               g_state.steer_valid);
    if (!direction.valid) {
        // "Movement keys" with no key held: there is no direction to strafe along. The input
        // is left exactly as the player gave it - no guessed direction, no leftover from the
        // previous tick.
        return;
    }

    // 2. Turn that direction into the air-acceleration wish. `main`'s maths, measured against
    //    the chosen direction instead of always against the look direction; when the source
    //    is "Viewangles" and "Easy strafe" is off this is exactly main's behaviour.
    const float smooth = runtime.strafe_easy.load() ? 0.f : StrafeSmoothFactor(runtime.smoothing.load());
    const StrafeWish wish = AirStrafeWish(tick.velocity_x, tick.velocity_y, direction.yaw,
                                          tick.forward_speed, tick.side_speed, smooth);

    if (!wish.valid) {
        // main pushes forward while the player is still slow, so a jump from standstill
        // still builds speed.
        tick.cmd->forwardmove = wish.forward;
        tick.cmd->sidemove = 0.f;
        return;
    }

    // 3. Source reads forwardmove/sidemove in the command's OWN view frame, so the wish -
    //    expressed in the frame of the direction chosen above - is rotated into it. Only the
    //    movement components are written: the look direction is read, never produced, which
    //    is what keeps the camera still while the player accelerates.
    const MoveVector move =
        RotateIntoViewFrame(wish.forward, wish.side, wish.view_yaw, tick.cmd->view_angles.y);
    tick.cmd->forwardmove = move.forward;
    tick.cmd->sidemove = move.side;
}

void ResetAutoStrafe() { g_state = GateAndSteer{}; }
} // namespace Misc::Movement::Detail
