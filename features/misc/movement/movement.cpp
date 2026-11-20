#include "movement.hpp"
#include "../../../context.hpp"
#include "../../../sdk/convars.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/log.hpp"
#include <windows.h>

namespace Misc::Movement {
namespace {
bool g_failed{};
bool g_reported{};

// main's cl_forwardspeed / cl_sidespeed: the wish speed the movement input resolves to.
// Read through the same cvar layer ping spike uses, with the CS:GO defaults as the
// fallback, so a failed lookup changes nothing.
float ForwardSpeed() { return ConVars::Float("cl_forwardspeed", 50.f, 450.f, 450.f); }
float SideSpeed() { return ConVars::Float("cl_sidespeed", 50.f, 450.f, 450.f); }

void CreateMoveImpl(CUserCmd* cmd) {
    if (!cmd || cmd->command_number == 0) return;
    if (!ctx.in_game || !ctx.local) return;
    CCSPlayer* local = ctx.local;
    if (!local->IsAlive()) return;

    Detail::Tick tick;
    tick.cmd = cmd;
    const int flags = local->m_fFlags();
    const int move_type = local->m_MoveType();
    tick.on_ground = (flags & 1) != 0; // FL_ONGROUND
    // Ground ticks: main counts consecutive grounded ticks and treats three as "settled",
    // because the first frames after a landing still carry the landing velocity.
    static int ground_ticks = 0;
    ground_ticks = tick.on_ground ? ground_ticks + 1 : 0;
    tick.ground_ticks = ground_ticks;
    tick.on_ladder = move_type == 9; // MOVETYPE_LADDER
    tick.noclip = move_type == 8;    // MOVETYPE_NOCLIP
    const Vector velocity = local->m_vecVelocity();
    tick.velocity_x = std::isfinite(velocity.x) ? velocity.x : 0.f;
    tick.velocity_y = std::isfinite(velocity.y) ? velocity.y : 0.f;
    tick.speed = std::sqrt(tick.velocity_x * tick.velocity_x + tick.velocity_y * tick.velocity_y);
    tick.view_yaw = cmd->view_angles.IsValid() ? NormalizeYaw(cmd->view_angles.y) : 0.f;
    tick.origin = local->m_vecOrigin();
    tick.forward_speed = ForwardSpeed();
    tick.side_speed = SideSpeed();

    // The edge check reads the velocity the player has now, the strafe writes the wish that
    // changes it, and quick stop runs last because it only applies when nothing is steered.
    Detail::RunEdge(tick);
    Detail::RunBhop(tick);
    Detail::RunAutoStrafe(tick);
    Detail::RunQuickStop(tick);

    if (!g_reported) {
        g_reported = true;
        const auto& runtime = Detail::GetRuntime();
        LOG_INFO("movement: active from ClientMode::CreateMove (autostrafe=%d bhop=%d quick stop=%d "
                 "edge jump=%d edge stop=%d)",
                 static_cast<int>(runtime.autostrafe.load()), static_cast<int>(runtime.bhop.load()),
                 static_cast<int>(runtime.quick_stop.load()),
                 static_cast<int>(runtime.edge_jump.load()),
                 static_cast<int>(runtime.edge_stop.load()));
    }
}
} // namespace

namespace Detail {
Runtime& GetRuntime() {
    static Runtime runtime;
    return runtime;
}
} // namespace Detail

void Publish(const Settings& settings) {
    auto& runtime = Detail::GetRuntime();
    runtime.autostrafe.store(settings.autostrafe);
    runtime.strafe_viewangles.store(settings.strafe_viewangles);
    runtime.strafe_movement_keys.store(settings.strafe_movement_keys);
    runtime.strafe_easy.store(settings.strafe_easy);
    runtime.strafe_jump_release.store(settings.strafe_jump_release);
    runtime.avoid_walls.store(settings.avoid_walls);
    runtime.smoothing.store(settings.smoothing);
    runtime.bhop.store(settings.bhop);
    runtime.quick_stop.store(settings.quick_stop);
    runtime.edge_jump.store(settings.edge_jump);
    runtime.edge_stop.store(settings.edge_stop);
}

void CreateMove(CUserCmd* cmd) {
    if (g_failed) return;
    ctx.in_createmove = true;
    // SEH: the command and the player entity belong to the game and can go stale; one
    // exception disables the module for the session instead of throwing on every tick.
    __try { CreateMoveImpl(cmd); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_failed = true;
        ctx.in_createmove = false;
        LOG_ERROR("movement: create-move exception 0x%08lX; movement stopped", GetExceptionCode());
        return;
    }
    ctx.in_createmove = false;
}

void Shutdown() { Publish(Settings{}); }
} // namespace Misc::Movement
