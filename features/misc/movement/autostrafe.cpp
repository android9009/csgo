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
} // namespace

void RunAutoStrafe(const Tick& tick) {
    const auto& runtime = GetRuntime();
    if (!runtime.autostrafe.load()) return;
    if (tick.on_ladder || tick.noclip) return;
    if (tick.on_ground) return;
    if (tick.cmd->HasButton(IN_SPEED) || tick.cmd->HasButton(IN_WALK)) return;
    if (WallAhead(tick) && runtime.avoid_walls.load()) return;

    const float smooth = runtime.strafe_easy.load() ? 0.f : StrafeSmoothFactor(runtime.smoothing.load());
    const StrafeWish wish = AirStrafeWish(tick.velocity_x, tick.velocity_y, tick.view_yaw,
                                          tick.forward_speed, tick.side_speed, smooth);

    if (!wish.valid) {
        // main pushes forward while the player is still slow, so a jump from standstill
        // still builds speed.
        tick.cmd->forwardmove = wish.forward;
        tick.cmd->sidemove = 0.f;
        return;
    }

    // The wish was computed against the yaw the strafer wants this tick (the player's yaw
    // turned by `-diff * smooth`); Source will read the values against the command's own
    // yaw, so the move vector is rotated into that frame. That keeps the physical direction
    // identical and leaves view_angles untouched: writing them is what turned the camera
    // with the strafe.
    const MoveVector move = RotateIntoViewFrame(wish.forward, wish.side, wish.view_yaw,
                                                tick.cmd->view_angles.y);
    tick.cmd->forwardmove = move.forward;
    tick.cmd->sidemove = move.side;

    // "Jump release": holding jump in the air costs speed on the next landing; the button is
    // taken away for the tick so the jump has to be re-issued.
    if (runtime.strafe_jump_release.load() && tick.cmd->HasButton(IN_JUMP)) {
        tick.cmd->RemoveButton(IN_JUMP);
    }
}
} // namespace Misc::Movement::Detail
