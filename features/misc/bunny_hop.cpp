#include "dependencies/gameplay.hpp"

namespace misc_gameplay {
using namespace misc_runtime;

void bunny_hop(user_cmd* cmd, int flags, bool enabled) {
    if (!enabled || !(cmd->buttons & in_jump))
        return;
    if (!(flags & fl_on_ground))
        cmd->buttons &= ~in_jump;
}

} // namespace misc_gameplay
