#include "movement.hpp"
#include "../../../sdk/convars.hpp"
#include "../../../sdk/datatypes/c_user_cmd.hpp"
#include "../../../sdk/interfaces/interfaces.hpp"
#include "../../../utils/math/math.hpp"
#include <algorithm>
#include <cmath>

namespace {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD(x) ((x) * (static_cast<float>(M_PI) / 180.f))
#define RAD2DEG(x) ((x) * (180.f / static_cast<float>(M_PI)))

// `main` strafe_dir_t, the eight wish directions for the held WASD keys.
enum strafe_dir_t {
    STRAFE_FORWARDS = 0,
    STRAFE_BACKWARDS = 180,
    STRAFE_LEFT = 90,
    STRAFE_RIGHT = -90,
    STRAFE_BACK_LEFT = 135,
    STRAFE_BACK_RIGHT = -135,
};
} // namespace

namespace Misc::Movement::Detail {
void AutoStrafe(const Tick& tick, State& state) {
    const Settings& s = Published();

    // `main` c_movement::auto_strafe. Only in the air, never while sprinting; the wish is
    // clamp(5850/speed, +-cl_forwardspeed) forward and +-cl_sidespeed side by the sign of
    // the angle between velocity and the running reference yaw.
    if (tick.on_ground || tick.on_ladder || tick.noclip) return;
    if (tick.cmd->HasButton(IN_SPEED)) return;

    // Jump gate (our menu's "Jump release" items).
    if (s.strafe_jump_release && !tick.jump_held) return;
    if (s.strafe_jump_release_after &&
        !(state.jump_pressed_in_air && state.jump_released_in_air && !tick.jump_held))
        return;

    const bool holding_w = tick.cmd->HasButton(IN_FORWARD);
    const bool holding_a = tick.cmd->HasButton(IN_MOVELEFT);
    const bool holding_s = tick.cmd->HasButton(IN_BACK);
    const bool holding_d = tick.cmd->HasButton(IN_MOVERIGHT);

    Vector velocity = tick.velocity;
    velocity.z = 0.f;

    const auto speed = velocity.Length();

    // The running reference yaw. "Movement keys" steers it with the held WASD keys
    // (`main`'s behaviour); "Viewangles" keeps it on the look direction.
    state.base_view_angle.y = tick.view.y;
    if (s.strafe_movement_keys) {
        const bool pressing_move = holding_w || holding_a || holding_s || holding_d;
        if (pressing_move) {
            float wish_dir{};
            if (holding_w) {
                if (holding_a) wish_dir += (STRAFE_LEFT / 2);
                else if (holding_d) wish_dir += (STRAFE_RIGHT / 2);
                else wish_dir += STRAFE_FORWARDS;
            } else if (holding_s) {
                if (holding_a) wish_dir += STRAFE_BACK_LEFT;
                else if (holding_d) wish_dir += STRAFE_BACK_RIGHT;
                else wish_dir += STRAFE_BACKWARDS;
                tick.cmd->forwardmove = 0.f;
            } else if (holding_a)
                wish_dir += STRAFE_LEFT;
            else if (holding_d)
                wish_dir += STRAFE_RIGHT;

            state.base_view_angle.y += Math::normalize_yaw(wish_dir);
        }
    }

    const auto smooth = (1.f - (0.15f * (s.smoothing * 0.01f)));
    const auto forward_speed = ConVars::Float("cl_forwardspeed", 50.f, 450.f, 450.f);
    const auto side_speed = ConVars::Float("cl_sidespeed", 50.f, 450.f, 450.f);

    if (speed <= 0.5f) {
        tick.cmd->forwardmove = forward_speed;
        return;
    }

    const auto diff = Math::normalize_yaw(state.base_view_angle.y - RAD2DEG(std::atan2f(velocity.y, velocity.x)));

    tick.cmd->forwardmove = std::clamp((5850.f / speed), -forward_speed, forward_speed);
    tick.cmd->sidemove = (diff > 0.f) ? -side_speed : side_speed;

    state.base_view_angle.y = Math::normalize_yaw(state.base_view_angle.y - diff * smooth);
}
} // namespace Misc::Movement::Detail
