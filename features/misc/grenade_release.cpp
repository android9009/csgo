#include "dependencies/gameplay.hpp"

#include <algorithm>
#include <cmath>

namespace {
using namespace misc_runtime;
using misc_gameplay::grenade_release_input;
using misc_gameplay::grenade_release_player;
using misc_gameplay::grenade_release_trace;
using misc_gameplay::grenade_release_trace_fn;
using misc_gameplay::grenade_release_player_fn;

struct impact_prediction {
    vec3 point{};
    float time = 0.f;
    bool valid = false;
    bool ground_impact = false;
};

bool simulate_impact(const grenade_release_input& input, void* context,
                     grenade_release_trace_fn trace, impact_prediction& result) {
    if (!trace || (input.weapon_id != 44 && input.weapon_id != 46 && input.weapon_id != 48) ||
        input.tick_interval <= 0.f || input.tick_interval > 0.05f ||
        input.throw_velocity <= 0.f) {
        return false;
    }

    const float strength = std::clamp(input.throw_strength, 0.f, 1.f);
    vec3 angles = input.view_angles;
    if (angles.x < -90.f)
        angles.x += 360.f;
    else if (angles.x > 90.f)
        angles.x -= 360.f;
    angles.x -= (90.f - std::fabs(angles.x)) * (10.f / 90.f);
    const vec3 direction = angle_vectors(angles);

    const vec3 source{ input.eye_position.x, input.eye_position.y,
        input.eye_position.z + (strength * 12.f - 12.f) };
    grenade_release_trace start_trace{};
    if (!trace(context, source, source + direction * 22.f, true, start_trace) ||
        start_trace.start_solid || start_trace.all_solid) {
        return false;
    }
    const vec3 origin = start_trace.end - direction * 6.f;

    vec3 inherited_velocity = input.player_velocity;
    if (inherited_velocity.length_2d() < 10.f)
        inherited_velocity = {};
    const float launch_speed = std::clamp(input.throw_velocity * 0.9f, 15.f, 750.f) *
        (strength * 0.7f + 0.3f);
    vec3 velocity = direction * launch_speed + inherited_velocity * 1.25f;

    constexpr float gravity = 800.f;
    const bool molotov = input.weapon_id == 46 || input.weapon_id == 48;
    const float fuse = molotov ? 2.f : 1.5f;
    const int steps = static_cast<int>(std::ceil(fuse / input.tick_interval));
    vec3 position = origin;

    for (int step = 0; step < steps; ++step) {
        const float old_z = velocity.z;
        const float next_z = old_z - gravity * 0.4f * input.tick_interval;
        const vec3 movement{
            velocity.x * input.tick_interval,
            velocity.y * input.tick_interval,
            (old_z + next_z) * 0.5f * input.tick_interval
        };
        velocity.z = next_z;

        grenade_release_trace collision{};
        if (!trace(context, position, position + movement, true, collision) ||
            collision.start_solid || collision.all_solid) {
            return false;
        }

        if (collision.fraction < 1.f) {
            position = collision.end;
            const float elapsed = (step + collision.fraction) * input.tick_interval;
            constexpr float molotov_min_surface_normal_z = 0.8660254f; // cos(30 degrees)
            if (molotov && collision.normal.z >= molotov_min_surface_normal_z) {
                result.point = position;
                result.time = elapsed;
                result.valid = true;
                result.ground_impact = true;
                return true;
            }

            const vec3 normal = collision.normal;
            const float back_off = 2.f * (velocity.x * normal.x + velocity.y * normal.y +
                velocity.z * normal.z);
            velocity = (velocity - normal * back_off) * 0.45f;
            const float speed_squared = velocity.x * velocity.x + velocity.y * velocity.y +
                velocity.z * velocity.z;
            if (normal.z > 0.7f && speed_squared < 400.f)
                velocity = {};
        } else {
            position = position + movement;
        }
    }

    if (molotov)
        return false; // Fail closed unless the predicted fire lands on walkable ground.
    result.point = position;
    result.time = fuse;
    result.valid = true;
    return true;
}

bool blast_visible(void* context, grenade_release_trace_fn trace,
                   const vec3& from, const vec3& to) {
    if (!trace)
        return false;
    const vec3 start{ from.x, from.y, from.z + 4.f };
    grenade_release_trace output{};
    return trace(context, start, to, false, output) && !output.start_solid &&
        !output.all_solid && output.fraction >= 0.97f;
}

vec3 player_center(const grenade_release_player& player, float lead_time) {
    const vec3 center = player.origin + (player.mins + player.maxs) * 0.5f;
    return center + player.velocity * std::clamp(lead_time, 0.f, 0.75f);
}

float estimate_he_damage(const grenade_release_input& input, void* context,
                         grenade_release_trace_fn trace, grenade_release_player_fn get_player,
                         const impact_prediction& prediction) {
    if (!get_player || input.local_team < 2 || input.local_team > 3 || !prediction.valid)
        return 0.f;

    float maximum_damage = 0.f;
    const int max_clients = std::clamp(input.max_clients, 1, 64);
    for (int index = 1; index <= max_clients; ++index) {
        grenade_release_player target{};
        if (!get_player(context, index, target) || target.health <= 0 ||
            target.team == input.local_team) {
            continue;
        }
        const vec3 center = player_center(target, prediction.time);
        const float distance = (center - prediction.point).length();
        if (distance > 350.f || !blast_visible(context, trace, prediction.point, center))
            continue;

        // Conservative CS:GO HE falloff approximation, with armor mitigation. The engine's
        // grenade-prediction event is not exposed by this client, so never treat this as exact.
        const float adjusted_distance = std::max(0.f, distance - 25.f);
        float damage = 105.f * std::exp(-(adjusted_distance * adjusted_distance) /
            (2.f * 140.f * 140.f));
        if (target.armor > 0)
            damage *= 0.5f;
        maximum_damage = std::max(maximum_damage, damage);
    }
    return maximum_damage;
}

bool molotov_covers_enemy(const grenade_release_input& input, void* context,
                          grenade_release_trace_fn trace, grenade_release_player_fn get_player,
                          const impact_prediction& prediction) {
    if (!get_player || input.local_team < 2 || input.local_team > 3 ||
        !prediction.valid || !prediction.ground_impact) {
        return false;
    }

    const int radius = std::clamp(input.molly_radius, 1, 200);
    const float radius_squared = static_cast<float>(radius * radius);
    const int max_clients = std::clamp(input.max_clients, 1, 64);
    for (int index = 1; index <= max_clients; ++index) {
        grenade_release_player target{};
        if (!get_player(context, index, target) || target.health <= 0 ||
            target.team == input.local_team) {
            continue;
        }
        const vec3 center = player_center(target, prediction.time);
        const float dx = center.x - prediction.point.x;
        const float dy = center.y - prediction.point.y;
        const float dz = std::fabs(center.z - prediction.point.z);
        if (dx * dx + dy * dy <= radius_squared && dz <= 96.f &&
            blast_visible(context, trace, prediction.point, center)) {
            return true;
        }
    }
    return false;
}
} // namespace

namespace misc_gameplay {
bool grenade_release_should_throw(const grenade_release_input& input, void* context,
    grenade_release_trace_fn trace, grenade_release_player_fn player) {
    if ((input.disable_in_air && !(input.local_flags & misc_runtime::fl_on_ground)) ||
        input.local_team < 2 || input.local_team > 3) {
        return false;
    }

    impact_prediction prediction{};
    if (!simulate_impact(input, context, trace, prediction))
        return false;
    if (input.weapon_id == 44) {
        const int threshold = std::clamp(input.damage_threshold, 1, 50);
        return estimate_he_damage(input, context, trace, player, prediction) >=
            static_cast<float>(threshold);
    }
    if (input.weapon_id == 46 || input.weapon_id == 48)
        return molotov_covers_enemy(input, context, trace, player, prediction);
    return false;
}
} // namespace misc_gameplay
