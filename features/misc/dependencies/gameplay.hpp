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
std::string purchase_command(const c_config_system::misc_t& misc);
bool quick_switch_weapon(std::string_view weapon);
} // namespace misc_gameplay
