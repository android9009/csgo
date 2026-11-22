#include "movement.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"

namespace Misc::Movement::Detail {
void AutoJump(const Tick& tick, State& state) {
    // `main` c_movement::auto_jump, verbatim logic: hold-jump is released for one tick and
    // re-issued on landing, because holding IN_JUMP permanently drops every second jump.
    if (tick.on_ladder || tick.noclip) return;

    if (!state.last_jumped && state.should_fake) {
        state.should_fake = false;
        tick.cmd->ForceButton(IN_JUMP);
    } else if (tick.cmd->HasButton(IN_JUMP)) {
        if (tick.on_ground) {
            state.should_fake = state.last_jumped = true;
        } else {
            tick.cmd->RemoveButton(IN_JUMP);
            state.last_jumped = false;
        }
    } else {
        state.should_fake = state.last_jumped = false;
    }
}
} // namespace Misc::Movement::Detail
