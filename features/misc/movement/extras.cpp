#include "movement.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"
#include <cmath>

namespace {
constexpr float kLongJumpMinSpeed = 100.f; // below this a "jump" is a hop, not a long jump
} // namespace

namespace Misc::Movement::Detail {
void Extras(const Tick& tick, State& state) {
    const Settings& s = Published();

    // Long jump: the jump-duck assist. Once we have left the ground with real horizontal
    // speed, hold the duck for the rest of the arc - that is the entire technique, and it
    // adds no speed, it only stops the server from treating the arc as a normal jump. It is
    // paused while the edge assists own the movement forecast (per the menu's note).
    if (s.long_jump && !s.edge_jump && !s.edge_stop && !tick.on_ground &&
        !tick.on_ladder && !tick.noclip) {
        const float speed = std::hypot(tick.velocity.x, tick.velocity.y);
        if (speed > kLongJumpMinSpeed)
            tick.cmd->ForceButton(IN_DUCK);
    }

    // Ladder jump: dismount assist. While climbing a ladder and steering forward, issue the
    // dismount jump. "Near the top" is approximated by requiring upward motion (a player at
    // the bottom is not rising), and the server still enforces its own ladder-jump cooldown.
    if (s.ladder_jump && tick.on_ladder && tick.velocity.z > 5.f &&
        tick.cmd->HasButton(IN_FORWARD)) {
        tick.cmd->ForceButton(IN_JUMP);
    }
    (void)state;
}
} // namespace Misc::Movement::Detail
