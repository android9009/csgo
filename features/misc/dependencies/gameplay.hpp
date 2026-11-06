#pragma once

#include "source_types.hpp"
#include "../../../sdk/config_system/config_system.hpp"

#include <string>
#include <string_view>

namespace misc_gameplay {
void bunny_hop(misc_runtime::user_cmd* cmd, int flags, bool enabled);
void air_strafe(misc_runtime::user_cmd* cmd, const misc_runtime::vec3& velocity, int flags,
                bool enabled, int strafe_type, int smooth);
bool compensate_grenade_throw(misc_runtime::user_cmd* cmd, const misc_runtime::vec3& velocity,
                              const misc_runtime::vec3& previous_velocity, float throw_velocity,
                              float strength);

struct grenade_release_trace {
    misc_runtime::vec3 end{};
    misc_runtime::vec3 normal{};
    float fraction = 1.f;
    bool start_solid = false;
    bool all_solid = false;
};

struct grenade_release_player {
    misc_runtime::vec3 origin{};
    misc_runtime::vec3 velocity{};
    misc_runtime::vec3 mins{};
    misc_runtime::vec3 maxs{};
    int team = 0;
    int health = 0;
    int armor = 100;
};

struct grenade_release_input {
    misc_runtime::vec3 eye_position{};
    misc_runtime::vec3 view_angles{};
    misc_runtime::vec3 player_velocity{};
    int weapon_id = 0;
    int local_team = 0;
    int local_flags = 0;
    int max_clients = 0;
    int damage_threshold = 50;
    int molly_radius = 100;
    float throw_strength = 0.f;
    float throw_velocity = 0.f;
    float tick_interval = 0.f;
    bool disable_in_air = false;
};

using grenade_release_trace_fn = bool (*)(void*, const misc_runtime::vec3&,
    const misc_runtime::vec3&, bool hull, grenade_release_trace&);
using grenade_release_player_fn = bool (*)(void*, int index, grenade_release_player&);
bool grenade_release_should_throw(const grenade_release_input& input, void* context,
    grenade_release_trace_fn trace, grenade_release_player_fn player);

std::string purchase_command(const c_config_system::misc_t& misc);
bool quick_switch_weapon(std::string_view weapon);
} // namespace misc_gameplay
