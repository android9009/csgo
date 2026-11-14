#include "dependencies/gameplay.hpp"

#include <algorithm>
#include <cmath>

namespace misc_gameplay {
using namespace misc_runtime;

void air_strafe(user_cmd* cmd, const vec3& velocity, int flags, bool enabled, int strafe_type, int smooth_value) {
    if (!enabled || (flags & fl_on_ground) || (cmd->buttons & in_speed))
        return;
    const float speed = velocity.length_2d();
    if (speed <= 0.5f) {
        cmd->forwardmove = 450.f;
        return;
    }
    if (strafe_type == 0) {
        cmd->forwardmove = std::clamp(5850.f / speed, -450.f, 450.f);
        if (cmd->mouse_dx > 1)
            cmd->sidemove = 450.f;
        else if (cmd->mouse_dx < -1)
            cmd->sidemove = -450.f;
        return;
    }

    const bool w = (cmd->buttons & in_forward) != 0;
    const bool a = (cmd->buttons & in_moveleft) != 0;
    const bool s = (cmd->buttons & in_back) != 0;
    const bool d = (cmd->buttons & in_moveright) != 0;
    float wish = 0.f;
    if (w) {
        if (a) wish = 45.f;
        else if (d) wish = -45.f;
    } else if (s) {
        if (a) wish = 135.f;
        else if (d) wish = -135.f;
        else wish = 180.f;
    } else if (a) {
        wish = 90.f;
    } else if (d) {
        wish = -90.f;
    }

    // Follow Airflow's directional wish-angle calculation, but keep the
    // player's camera untouched. Rotate the movement vector instead.
    const float view_yaw = cmd->viewangles.y;
    float base_yaw = view_yaw;
    if (w || a || s || d)
        base_yaw = normalize_yaw(base_yaw + wish);

    const float velocity_yaw = std::atan2(velocity.y, velocity.x) * 180.f / pi;
    const float difference = normalize_yaw(base_yaw - velocity_yaw);
    const float smooth = 1.f - 0.15f * (1.f - std::clamp(smooth_value, 0, 100) / 100.f);
    cmd->forwardmove = std::clamp(5850.f / speed, -450.f, 450.f);
    cmd->sidemove = difference > 0.f ? -450.f : 450.f;
    base_yaw = normalize_yaw(base_yaw - difference * smooth);

    const float move_length = std::sqrt(cmd->forwardmove * cmd->forwardmove + cmd->sidemove * cmd->sidemove);
    if (move_length > 0.001f) {
        const float move_angle = std::atan2(cmd->sidemove, cmd->forwardmove) +
            (view_yaw - base_yaw) * pi / 180.f;
        cmd->forwardmove = std::cos(move_angle) * move_length;
        cmd->sidemove = std::sin(move_angle) * move_length;
    }
}

} // namespace misc_gameplay
