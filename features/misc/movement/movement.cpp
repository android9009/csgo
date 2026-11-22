#include "movement.hpp"
#include "../../../context.hpp"
#include "../../../sdk/convars.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"
#include "../../../sdk/entity/player.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/math/math.hpp"
#include "../../../utils/log.hpp"
#include <atomic>
#include <cmath>
#include <windows.h>

namespace Misc::Movement {
namespace {
std::atomic<bool> g_strafe_viewangles{false};
std::atomic<bool> g_strafe_movement_keys{false};
std::atomic<bool> g_strafe_easy{false};
std::atomic<bool> g_strafe_jump_release{false};
std::atomic<bool> g_strafe_jump_release_after{false};
std::atomic<int> g_smoothing{0};
std::atomic<bool> g_avoid_walls{false};
std::atomic<bool> g_bhop{false};
std::atomic<bool> g_quick_stop{false};
std::atomic<bool> g_edge_jump{false};
std::atomic<bool> g_edge_stop{false};
std::atomic<bool> g_long_jump{false};
std::atomic<bool> g_ladder_jump{false};

State g_state;
} // namespace

void Publish(const Settings& settings) {
    g_strafe_viewangles.store(settings.strafe_viewangles);
    g_strafe_movement_keys.store(settings.strafe_movement_keys);
    g_strafe_easy.store(settings.strafe_easy);
    g_strafe_jump_release.store(settings.strafe_jump_release);
    g_strafe_jump_release_after.store(settings.strafe_jump_release_after);
    g_smoothing.store(settings.smoothing);
    g_avoid_walls.store(settings.avoid_walls);
    g_bhop.store(settings.bhop);
    g_quick_stop.store(settings.quick_stop);
    g_edge_jump.store(settings.edge_jump);
    g_edge_stop.store(settings.edge_stop);
    g_long_jump.store(settings.long_jump);
    g_ladder_jump.store(settings.ladder_jump);
}

void Reset() { g_state.Reset(); }
void Shutdown() { Publish(Settings{}); g_state.Reset(); }

namespace Detail {
// Built from the per-field atomics on each read so the menu thread can Publish while the
// CreateMove thread is mid-read without a torn struct.
const Settings& Published() {
    static Settings snapshot;
    snapshot.strafe_viewangles = g_strafe_viewangles.load();
    snapshot.strafe_movement_keys = g_strafe_movement_keys.load();
    snapshot.strafe_easy = g_strafe_easy.load();
    snapshot.strafe_jump_release = g_strafe_jump_release.load();
    snapshot.strafe_jump_release_after = g_strafe_jump_release_after.load();
    snapshot.autostrafe = snapshot.strafe_viewangles || snapshot.strafe_movement_keys ||
                          snapshot.strafe_easy || snapshot.strafe_jump_release ||
                          snapshot.strafe_jump_release_after;
    snapshot.smoothing = g_smoothing.load();
    snapshot.avoid_walls = g_avoid_walls.load();
    snapshot.bhop = g_bhop.load();
    snapshot.quick_stop = g_quick_stop.load();
    snapshot.edge_jump = g_edge_jump.load();
    snapshot.edge_stop = g_edge_stop.load();
    snapshot.long_jump = g_long_jump.load();
    snapshot.ladder_jump = g_ladder_jump.load();
    return snapshot;
}

void UpdateGroundTicks(Tick& tick, State& state) {
    // `main` c_movement::update_ground_ticks.
    if (tick.on_ground) state.ground_ticks++;
    else state.ground_ticks = 0;
}

void UpdateJumpGate(const Tick& tick, State& state) {
    // Edge tracking for the "Jump release" gate: the flag is cleared on the ground, so a
    // "released in air" can only come from a real press-and-release in the air.
    if (tick.on_ground) {
        state.jump_pressed_in_air = false;
        state.jump_released_in_air = false;
        state.jump_was_held = tick.jump_held;
        return;
    }
    if (tick.jump_held && !state.jump_was_held) state.jump_pressed_in_air = true;
    if (!tick.jump_held && state.jump_was_held && state.jump_pressed_in_air)
        state.jump_released_in_air = true;
    state.jump_was_held = tick.jump_held;
}

void RotateMovement(CUserCmd* cmd, const QAngle& ang) {
    // `main` c_movement::rotate_movement: rotates the wish from the strafe reference frame
    // into the command's own view frame so the camera never turns.
    if (!cmd) return;

    const Vector move{cmd->forwardmove, cmd->sidemove, 0.f};
    const float length = move.Length2D();
    if (length <= 0.f) return;

    float yaw = std::atan2(move.y, move.x) * (180.f / 3.14159265358979323846f);
    yaw += (cmd->view_angles.y - ang.y);

    Vector direction;
    Math::angle_vectors(QAngle{0.f, yaw, 0.f}, &direction);
    direction *= length;

    if (cmd->view_angles.x < -90.f || cmd->view_angles.x > 90.f)
        direction.x = -direction.x;

    cmd->forwardmove = direction.x;
    cmd->sidemove = direction.y;
}
} // namespace Detail

void CreateMove(CUserCmd* cmd) {
    if (!cmd || cmd->command_number == 0) return;
    if (!ctx.in_game || !ctx.local || !ctx.local->IsAlive()) return;
    CCSPlayer* local = ctx.local;

    const Settings& settings = Detail::Published();
    if (!settings.bhop && !settings.quick_stop && !settings.edge_jump &&
        !settings.edge_stop && !settings.autostrafe && !settings.long_jump &&
        !settings.ladder_jump)
        return;

    Tick tick;
    tick.cmd = cmd;
    tick.flags = local->m_fFlags();
    tick.move_type = local->m_MoveType();
    tick.on_ground = (tick.flags & 1) != 0; // FL_ONGROUND
    tick.on_ladder = tick.move_type == 9;   // MOVETYPE_LADDER
    tick.noclip = tick.move_type == 8;      // MOVETYPE_NOCLIP
    tick.velocity = local->m_vecVelocity();
    tick.origin = local->m_vecOrigin();
    tick.view = cmd->view_angles;
    tick.jump_held = cmd->HasButton(IN_JUMP); // raw, before bhop edits the button
    tick.ground_ticks = g_state.ground_ticks;

    // The running strafe reference starts from the view the engine handed us this tick.
    QAngle view{};
    if (Interfaces::Engine) Interfaces::Engine->GetViewAngles(view);
    g_state.base_view_angle = view;

    Detail::UpdateGroundTicks(tick, g_state);
    Detail::UpdateJumpGate(tick, g_state);

    // `main` order inside run()/run_predicted(): jump, strafe, stop, edge; then this
    // build's extra assists (long jump / ladder jump), which only touch buttons.
    if (settings.bhop) Detail::AutoJump(tick, g_state);
    if (settings.autostrafe) Detail::AutoStrafe(tick, g_state);
    if (settings.quick_stop) Detail::FastStop(tick, g_state);
    if (settings.edge_jump || settings.edge_stop) Detail::Edge(tick, g_state);
    if (settings.long_jump || settings.ladder_jump) Detail::Extras(tick, g_state);

    // `main` c_movement::run() hands the rotated wish back into the command frame. Only the
    // auto-strafe wish needs that rotation, so it is applied just when strafe ran (the other
    // features leave the user's own forward/side move intact).
    if (settings.autostrafe) Detail::RotateMovement(cmd, g_state.base_view_angle);
}
} // namespace Misc::Movement
