#include "movement.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/math/math.hpp"
#include <cmath>

namespace {
#define RAD2DEG(x) ((x) * (180.f / 3.14159265358979323846f))
} // namespace

namespace Misc::Movement::Detail {
void InstantStop(const Tick& tick) {
    // `main` c_movement::instant_stop: point the movement opposite the velocity and push at
    // full speed, so the player counter-strafes to a stop.
    QAngle view;
    if (Interfaces::Engine) Interfaces::Engine->GetViewAngles(view);
    else view = tick.view;

    const float speed = tick.velocity.Length();
    const float vel_yaw = RAD2DEG(std::atan2(tick.velocity.y, tick.velocity.x));

    Vector direction;
    Math::angle_vectors(QAngle{0.f, view.y - vel_yaw, 0.f}, &direction);

    const Vector stop = direction * (-speed);
    tick.cmd->forwardmove = stop.x;
    tick.cmd->sidemove = stop.y;
}

void FastStop(const Tick& tick, State& state) {
    // `main` c_movement::fast_stop: on the ground long enough (3 ticks), nothing steered,
    // and not already stopped -> one counter-strafe; completed once the speed is under 15.
    if (!Published().quick_stop) { state.complete_fast_stop = false; return; }
    if (tick.ground_ticks < 3) { state.complete_fast_stop = false; return; }
    if (tick.on_ladder || tick.noclip) return;

    if (tick.cmd->HasButton(IN_MOVELEFT) || tick.cmd->HasButton(IN_MOVERIGHT) ||
        tick.cmd->HasButton(IN_BACK) || tick.cmd->HasButton(IN_FORWARD)) {
        state.complete_fast_stop = false;
        return;
    }
    if (state.complete_fast_stop) return;

    InstantStop(tick);

    if (tick.velocity.Length2D() <= 15.f) state.complete_fast_stop = true;
}
} // namespace Misc::Movement::Detail
