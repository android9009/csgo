#include "movement.hpp"
#include "../../context.hpp"
#include "../../features/menu/config.hpp"
#include "../../sdk/structs.hpp"
namespace Features::Misc {
void Movement(CUserCmd* cmd) {
    if (!cmd || !ctx.local || !Config::settings.bunnyhop) return;
    constexpr int in_jump = 1 << 1;
    constexpr int on_ground = 1 << 0;
    if ((cmd->buttons & in_jump) && !(ctx.local->m_fFlags() & on_ground)) cmd->buttons &= ~in_jump;
}
}
