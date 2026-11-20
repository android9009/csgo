#include "movement.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"

namespace Misc::Movement::Detail {
namespace {
// main: complete_fast_stop - once the velocity is gone the wish is not written again until
// the player steers, otherwise every idle tick would keep sending a counter-move.
bool g_completed{};
} // namespace

void RunQuickStop(const Tick& tick) {
    const auto& runtime = GetRuntime();
    const bool steering = tick.cmd->HasButton(kMoveButtons);
    if (!ShouldQuickStop(runtime.quick_stop.load(), tick.ground_ticks, steering, g_completed)) {
        if (steering) g_completed = false;
        return;
    }
    if (tick.on_ladder || tick.noclip) return;

    const StopWish wish = InstantStopWish(tick.velocity_x, tick.velocity_y, tick.view_yaw);
    if (!wish.valid) {
        g_completed = true;
        return;
    }

    tick.cmd->forwardmove = wish.forward;
    tick.cmd->sidemove = wish.side;
    if (tick.speed <= kStopSpeedThreshold) g_completed = true;
}
} // namespace Misc::Movement::Detail
