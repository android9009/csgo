#include "movement.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"

namespace Misc::Movement::Detail {
namespace {
// main keeps this state inside its movement object; one instance is enough because only the
// local player's commands are ever modified.
BhopState g_state;
} // namespace

void RunBhop(const Tick& tick) {
    const auto& runtime = GetRuntime();
    if (!runtime.bhop.load()) {
        g_state = BhopState{};
        return;
    }

    BhopInput input;
    input.jump_button = tick.cmd->HasButton(IN_JUMP);
    input.on_ground = tick.on_ground;
    input.on_ladder = tick.on_ladder;
    input.noclip = tick.noclip;

    const BhopOutput output = AdvanceBhop(g_state, input);
    if (output.force_jump) tick.cmd->ForceButton(IN_JUMP);
    if (output.remove_jump) tick.cmd->RemoveButton(IN_JUMP);
}
} // namespace Misc::Movement::Detail
