// Adapted for UI-only configuration; gameplay and skin-database dependencies removed.
#define IMGUI_DEFINE_MATH_OPERATORS
#include <limits>
#include <cmath>
#include <algorithm>
#include "config_system.hpp"


#include <filesystem>
#include <fstream>
#include <format>
#include <iomanip>
#include <cstdint>
#include <sys/stat.h>

#include "../../sdk/includes/json/json.h"
#include "../../features/visuals/settings.hpp"
#include "../../features/misc/misc.hpp"

void c_config_system::setup_values( )
{
	push_item( &g_cfg->rage_bot.m_enabled, "rage_bot", "enabled", g_cfg->rage_bot.m_enabled );
	push_item( &g_cfg->rage_bot.m_silent, "rage_bot", "silent", g_cfg->rage_bot.m_silent );

	// Save weapon config for each group
	for (int group = 0; group < RAGE_WEAPON_PRESET_MAX; group++) {
		auto& cfg = g_cfg->rage_bot.m_weapon_config[group];
		std::string prefix = std::format("weapon_group_{}", group);
		
		push_item(&cfg.m_enabled, "rage_bot", prefix + "_enabled", cfg.m_enabled);
		push_item(&cfg.m_override_general, "rage_bot", prefix + "_override", cfg.m_override_general);
		
		for (auto hitbox = 0; hitbox < 6; hitbox++) {
			push_item(&cfg.m_hitboxes[hitbox], "rage_bot", std::format("{}_{}_hitbox_{}", prefix, group, hitbox), cfg.m_hitboxes[hitbox]);
			push_item(&cfg.m_multi_points[hitbox], "rage_bot", std::format("{}_{}_multipoint_{}", prefix, group, hitbox), cfg.m_multi_points[hitbox]);
			push_item(&cfg.m_hitbox_safety[hitbox], "rage_bot", std::format("{}_{}_hitbox_safety_{}", prefix, group, hitbox), cfg.m_hitbox_safety[hitbox]);
		}
		
		push_item(&cfg.m_head_scale, "rage_bot", prefix + "_head_scale", cfg.m_head_scale);
		push_item(&cfg.m_body_scale, "rage_bot", prefix + "_body_scale", cfg.m_body_scale);
		push_item(&cfg.m_hit_chance, "rage_bot", prefix + "_hit_chance", cfg.m_hit_chance);
		push_item(&cfg.m_hit_chance_seed, "rage_bot", prefix + "_hit_chance_seed", cfg.m_hit_chance_seed);
		push_item(&cfg.m_dt_hit_chance, "rage_bot", prefix + "_dt_hit_chance", cfg.m_dt_hit_chance);
		push_item(&cfg.m_extrapolation_hit_chance, "rage_bot", prefix + "_extrapolation_hit_chance", cfg.m_extrapolation_hit_chance);
		push_item(&cfg.m_force_shot, "rage_bot", prefix + "_force_shot", cfg.m_force_shot);
		push_item(&cfg.m_minimum_hit_chance, "rage_bot", prefix + "_minimum_hit_chance", cfg.m_minimum_hit_chance);
		push_item(&cfg.m_extrapolate, "rage_bot", prefix + "_extrapolate", cfg.m_extrapolate);
		push_item(&cfg.m_backtrack, "rage_bot", prefix + "_backtrack", cfg.m_backtrack);
		push_item(&cfg.m_minimum_damage, "rage_bot", prefix + "_min_damage", cfg.m_minimum_damage);
		push_item(&cfg.m_delay_shot, "rage_bot", prefix + "_delay_shot", cfg.m_delay_shot);
		push_item(&cfg.m_auto_stop, "rage_bot", prefix + "_auto_stop", cfg.m_auto_stop);
		push_item(&cfg.m_auto_stop_modes[0], "rage_bot", prefix + "_auto_stop_early", cfg.m_auto_stop_modes[0]);
		push_item(&cfg.m_auto_stop_modes[1], "rage_bot", prefix + "_auto_stop_full", cfg.m_auto_stop_modes[1]);
		push_item(&cfg.m_auto_stop_modes[2], "rage_bot", prefix + "_auto_stop_between", cfg.m_auto_stop_modes[2]);
		push_item(&cfg.m_auto_stop_modes[3], "rage_bot", prefix + "_auto_stop_air", cfg.m_auto_stop_modes[3]);
		push_item(&cfg.m_auto_stop_modes[4], "rage_bot", prefix + "_auto_stop_duck", cfg.m_auto_stop_modes[4]);
		push_item(&cfg.m_auto_scope, "rage_bot", prefix + "_auto_scope", cfg.m_auto_scope);
		push_item(&cfg.m_penetration, "rage_bot", prefix + "_penetration", cfg.m_penetration);
		push_item(&cfg.m_body_aim, "rage_bot", prefix + "_body_aim", cfg.m_body_aim);
		push_item(&cfg.m_safe_points, "rage_bot", prefix + "_safe_points", cfg.m_safe_points);

		if (group > 0) {
			push_item(&g_cfg->rage_bot.m_preset_initialized[group], "rage_bot", prefix + "_preset_initialized", g_cfg->rage_bot.m_preset_initialized[group]);
			push_item(&g_cfg->rage_bot.m_preset_custom[group], "rage_bot", prefix + "_preset_custom", g_cfg->rage_bot.m_preset_custom[group]);
			push_item(&g_cfg->rage_bot.m_preset_names[group], "rage_bot", prefix + "_preset_name", g_cfg->rage_bot.m_preset_names[group]);
			push_item(&g_cfg->rage_bot.m_preset_category[group], "rage_bot", prefix + "_preset_category", g_cfg->rage_bot.m_preset_category[group]);
			for (int weapon = 0; weapon < WEAPON_COUNT; ++weapon) {
				push_item(&g_cfg->rage_bot.m_preset_weapons[group][weapon], "rage_bot",
					prefix + "_weapon_" + std::to_string(weapon), g_cfg->rage_bot.m_preset_weapons[group][weapon]);
			}
		}
	}

	push_item( &g_cfg->rage_bot.m_override_damage_key_bind, "rage_bot", "override_damage_keybind", g_cfg->rage_bot.m_override_damage_key_bind );
	push_item( &g_cfg->rage_bot.m_override_damage_key_bind_style, "rage_bot", "override_damage_keybind_style", g_cfg->rage_bot.m_override_damage_key_bind_style );

	push_item( &g_cfg->anti_hit.m_enabled, "anti_aim", "enabled", g_cfg->anti_hit.m_enabled );
	push_item( &g_cfg->anti_hit.m_at_target, "anti_aim", "at_target", g_cfg->anti_hit.m_at_target );
	push_item( &g_cfg->anti_hit.m_pitch_mode, "anti_aim", "pitch_mode", g_cfg->anti_hit.m_pitch_mode );
	push_item( &g_cfg->anti_hit.m_pitch_amount, "anti_aim", "pitch_amount", g_cfg->anti_hit.m_pitch_amount );
	push_item( &g_cfg->anti_hit.m_yaw_mode, "anti_aim", "yaw_mode", g_cfg->anti_hit.m_yaw_mode );
	push_item( &g_cfg->anti_hit.m_yaw_amount, "anti_aim", "yaw_amount", g_cfg->anti_hit.m_yaw_amount );
	push_item( &g_cfg->anti_hit.m_jitter_enabled, "anti_aim", "jitter_enabled", g_cfg->anti_hit.m_jitter_enabled );
	push_item( &g_cfg->anti_hit.m_jitter_mode, "anti_aim", "jitter_mode", g_cfg->anti_hit.m_jitter_mode );
	push_item( &g_cfg->anti_hit.m_jitter_amount, "anti_aim", "jitter_amount", g_cfg->anti_hit.m_jitter_amount );
	push_item( &g_cfg->anti_hit.m_desync_enabled, "anti_aim", "desync_enabled", g_cfg->anti_hit.m_desync_enabled );
	push_item( &g_cfg->anti_hit.m_desync_mode, "anti_aim", "desync_mode", g_cfg->anti_hit.m_desync_mode );
	push_item( &g_cfg->anti_hit.m_desync_left, "anti_aim", "desync_left", g_cfg->anti_hit.m_desync_left );
	push_item( &g_cfg->anti_hit.m_desync_right, "anti_aim", "desync_right", g_cfg->anti_hit.m_desync_right );
	
	// Legit bot
	push_item( &g_cfg->legit_bot.m_enabled, "legit_bot", "enabled", g_cfg->legit_bot.m_enabled );
	push_item( &g_cfg->legit_bot.m_fov, "legit_bot", "fov", g_cfg->legit_bot.m_fov );
	push_item( &g_cfg->legit_bot.m_smooth, "legit_bot", "smooth", g_cfg->legit_bot.m_smooth );
	push_item( &g_cfg->legit_bot.m_triggerbot_enabled, "legit_bot", "triggerbot_enabled", g_cfg->legit_bot.m_triggerbot_enabled );
	push_item( &g_cfg->legit_bot.m_backtrack_enabled, "legit_bot", "backtrack_enabled", g_cfg->legit_bot.m_backtrack_enabled );
	push_item( &g_cfg->legit_bot.m_backtrack_ticks, "legit_bot", "backtrack_ticks", g_cfg->legit_bot.m_backtrack_ticks );

	push_item( &g_cfg->visuals.m_player_esp.m_enabled, "player_esp", "enabled", g_cfg->visuals.m_player_esp.m_enabled );
	push_item( &g_cfg->visuals.m_player_esp.m_bounding_box, "player_esp", "bounding_box", g_cfg->visuals.m_player_esp.m_bounding_box );
	push_item( &g_cfg->visuals.m_player_esp.m_health_bar, "player_esp", "health_bar", g_cfg->visuals.m_player_esp.m_health_bar );
	push_item( &g_cfg->visuals.m_player_esp.m_name, "player_esp", "name", g_cfg->visuals.m_player_esp.m_name );
	push_item( &g_cfg->visuals.m_player_esp.m_weapon, "player_esp", "weapon", g_cfg->visuals.m_player_esp.m_weapon );
	push_item( &g_cfg->visuals.m_player_esp.m_skeleton, "player_esp", "skeleton", g_cfg->visuals.m_player_esp.m_skeleton );
	
	// Misc
	push_item( &g_cfg->misc.m_bunny_hop, "misc", "bunny_hop", g_cfg->misc.m_bunny_hop );
	push_item( &g_cfg->misc.m_auto_strafe, "misc", "auto_strafe", g_cfg->misc.m_auto_strafe );
	push_item( &g_cfg->misc.m_strafe_type, "misc", "strafe_type", g_cfg->misc.m_strafe_type );
	push_item( &g_cfg->misc.m_strafe_smooth, "misc", "strafe_smooth", g_cfg->misc.m_strafe_smooth );

	auto& misc_features_state = misc_features::settings();
	push_item(&misc_features_state.automatic_grenade_release, "misc_features", "automatic_grenade_release", misc_features_state.automatic_grenade_release);
	push_item(&misc_features_state.grenade_release_damage, "misc_features", "grenade_release_damage", misc_features_state.grenade_release_damage);
	push_item(&misc_features_state.grenade_release_molly_radius, "misc_features", "grenade_release_molly_radius", misc_features_state.grenade_release_molly_radius);
	push_item(&misc_features_state.grenade_release_disable_in_air, "misc_features", "grenade_release_disable_in_air", misc_features_state.grenade_release_disable_in_air);

	push_item( &g_cfg->visuals.m_player_esp.m_bounding_box, "player_esp", "bounding_box", g_cfg->visuals.m_player_esp.m_bounding_box );
	push_item( &g_cfg->visuals.m_player_esp.m_health_bar, "player_esp", "health_bar", g_cfg->visuals.m_player_esp.m_health_bar );

	push_item( &g_cfg->world_esp.m_enable_thirdperson, "thirdperson", "enabled", g_cfg->world_esp.m_enable_thirdperson );
	push_item( &g_cfg->world_esp.m_distance, "thirdperson", "distance", g_cfg->world_esp.m_distance );
	push_item( &g_cfg->world_esp.m_enable_override_fov, "perspective", "override_fov_enabled", g_cfg->world_esp.m_enable_override_fov );
	push_item( &g_cfg->world_esp.m_override_fov, "perspective", "override_fov", g_cfg->world_esp.m_override_fov );
	push_item( &g_cfg->world_esp.m_zoom_fov, "perspective", "zoom_fov", g_cfg->world_esp.m_zoom_fov );
	push_item( &g_cfg->world_esp.m_aspect_ratio, "perspective", "aspect_ratio", g_cfg->world_esp.m_aspect_ratio );
	push_item( &g_cfg->world_esp.m_skip_second_zoom, "perspective", "skip_second_zoom", g_cfg->world_esp.m_skip_second_zoom );
	push_item( &g_cfg->world_esp.m_thirdperson_while_dead, "thirdperson", "while_dead", g_cfg->world_esp.m_thirdperson_while_dead );
	push_item( &g_cfg->world_esp.m_thirdperson_key_bind, "thirdperson", "thirdperson_keybind", g_cfg->world_esp.m_thirdperson_key_bind );
	push_item( &g_cfg->world_esp.m_thirdperson_key_bind_style, "thirdperson", "thirdperson_keybind_style", g_cfg->world_esp.m_thirdperson_key_bind_style );

	push_item( &g_cfg->skins.m_agents.m_selected_ct, "agent_changer", "selected_ct", g_cfg->skins.m_agents.m_selected_ct );
	push_item( &g_cfg->skins.m_agents.m_selected, "agent_changer", "selected_t", g_cfg->skins.m_agents.m_selected );

	push_item( &g_cfg->world.m_exposure, "world", "exposure", g_cfg->world.m_exposure );
	push_item( &g_cfg->world.m_brightness, "world", "brightness", g_cfg->world.m_brightness );
	push_item( &g_cfg->world.m_lighting, "world", "lighting", g_cfg->world.m_lighting );
	push_item( &g_cfg->world.m_render_fog, "world", "render_fog", g_cfg->world.m_render_fog );
	push_item( &g_cfg->world.m_fov, "world", "fov", g_cfg->world.m_fov );
	push_item( &g_cfg->world.m_viewmodel_fov, "world", "viewmodel_fov", g_cfg->world.m_viewmodel_fov );
	push_item( &g_cfg->world.m_viewmodel_offset_x, "world", "viewmodel_offset_x", g_cfg->world.m_viewmodel_offset_x );
	push_item( &g_cfg->world.m_viewmodel_offset_y, "world", "viewmodel_offset_y", g_cfg->world.m_viewmodel_offset_y );
	push_item( &g_cfg->world.m_viewmodel_offset_z, "world", "viewmodel_offset_z", g_cfg->world.m_viewmodel_offset_z );
	push_item( &g_cfg->world.m_remove_viewmodel_move, "world", "remove_viewmodel_move", g_cfg->world.m_remove_viewmodel_move );
	push_item( &g_cfg->world.m_remove_landing_bob, "world", "remove_landing_bob", g_cfg->world.m_remove_landing_bob );
	push_item( &g_cfg->world.m_remove_smoke, "world", "remove_smoke", g_cfg->world.m_remove_smoke );
	push_item( &g_cfg->world.m_remove_flash, "world", "remove_flash", g_cfg->world.m_remove_flash );
	push_item( &g_cfg->world.m_remove_scope_overlay, "world", "remove_scope_overlay", g_cfg->world.m_remove_scope_overlay );
	push_item( &g_cfg->world.m_visual_recoil, "world", "visual_recoil", g_cfg->world.m_visual_recoil );
	push_item( &g_cfg->world.m_remove_post_processing, "world", "remove_post_processing", g_cfg->world.m_remove_post_processing );
	push_item( &g_cfg->world.m_remove_shadows, "world", "remove_shadows", g_cfg->world.m_remove_shadows );
	push_item( &g_cfg->world.m_unlock_spectating, "world", "unlock_spectating", g_cfg->world.m_unlock_spectating );
	push_item( &g_cfg->world.m_show_viewmodel_in_scope, "world", "show_viewmodel_in_scope", g_cfg->world.m_show_viewmodel_in_scope );
	push_item( &g_cfg->world.m_draw_radar, "world", "draw_radar", g_cfg->world.m_draw_radar );
	push_item( &g_cfg->world.m_inaccuracy_overlay, "world", "inaccuracy_overlay", g_cfg->world.m_inaccuracy_overlay );
	push_item( &g_cfg->world.m_remove_death_notices, "world", "remove_death_notices", g_cfg->world.m_remove_death_notices );
	push_item( &g_cfg->world.m_remove_scoreboard, "world", "remove_scoreboard", g_cfg->world.m_remove_scoreboard );
	push_item( &g_cfg->world.m_custom_crosshair, "world", "custom_crosshair", g_cfg->world.m_custom_crosshair );
	push_item( &g_cfg->world.m_sky, "world", "sky_color", g_cfg->world.m_sky );
	push_item( &g_cfg->world.m_sky_clouds, "world", "sky_clouds_color", g_cfg->world.m_sky_clouds );
	push_item( &g_cfg->world.m_wall, "world", "m_wall_color", g_cfg->world.m_wall );

	push_item( &g_cfg->skins.m_knives.m_selected, "knife_changer", "selected", g_cfg->skins.m_knives.m_selected );
	push_item( &g_cfg->skins.m_gloves.m_selected, "glove_changer", "selected", g_cfg->skins.m_gloves.m_selected );

	// Independent game/preview visuals. The arrays are shared with the renderer, so these
	// entries also persist every user-dragged ESP offset instead of saving only the menu toggles.
	static const char* main_names[player_visuals::main_count] = {
		"bounding_box", "name", "distance", "health_bar", "ammo_bar", "skeleton"
	};
	static const char* flag_names[player_visuals::flag_count] = {
		"defuser", "blind", "scoped", "reload", "immunity", "slowed",
		"bomb", "hostage", "defuse", "pin_pulled", "money"
	};
	static const char* weapon_item_names[player_visuals::weapon_count] = {
		"text", "icon", "readiness_bar", "bomb", "defuser", "taser"
	};
	static const char* aimbot_names[player_visuals::aimbot_count] = { "hitboxes" };
	auto push_esp_color = [&]( player_visuals::esp_color& value, const std::string& category, const std::string& name ) {
		push_item( &value.primary, category, name, value.primary );
		push_item( &value.secondary, category, name + "_secondary", value.secondary );
		push_item( &value.gradient, category, name + "_gradient", value.gradient );
	};
	auto push_overlay = [&]( player_visuals::material_selection& selection, const std::string& category, const std::string& name ) {
		push_item(&selection.colors_initialized, category, name + "_colors_initialized", selection.colors_initialized);
		for (int i = 1; i < visuals_chams::mode_count; ++i) {
			push_item(&selection.values[i], category, std::format("{}_{}", name, i), selection.values[i]);
			// Keep the material index in every key: colors must not be shared by
			// rows, categories, sides, or by the legacy single-material picker.
			push_esp_color(selection.colors[i], category, std::format("{}_{}_color", name, i));
		}
	};
	for (int side_index = 0; side_index < player_visuals::side_count; ++side_index) {
		auto& side = player_visuals::settings(side_index);
		const std::string category = side_index == 0 ? "enemy_visuals" : "teammate_visuals";
		push_item(&side.esp_enabled, category, "esp_enabled", side.esp_enabled);
		push_item(&side.offscreen_arrow, category, "offscreen_arrow", side.offscreen_arrow);
		push_item(&side.sounds, category, "sounds", side.sounds);
		push_item(&side.sounds_color, category, "sounds_color", side.sounds_color);
		push_item(&side.offscreen_color, category, "offscreen_color", side.offscreen_color);
		push_item(&side.offscreen_outline, category, "offscreen_outline", side.offscreen_outline);
		push_item(&side.player_material, category, "player_material", side.player_material);
		push_item(&side.behind_walls_material, category, "behind_walls_material", side.behind_walls_material);
		push_item(&side.player_color, category, "player_color", side.player_color);
		push_item(&side.player_secondary, category, "player_secondary", side.player_secondary);
		push_item(&side.behind_walls_color, category, "behind_walls_color", side.behind_walls_color);
		push_item(&side.behind_walls_secondary, category, "behind_walls_secondary", side.behind_walls_secondary);
		push_item(&side.player_gradient, category, "player_gradient", side.player_gradient);
		push_item(&side.behind_walls_gradient, category, "behind_walls_gradient", side.behind_walls_gradient);
		push_item(&side.overlay_chams, category, "overlay_chams", side.overlay_chams);
		push_overlay(side.player_overlay, category, "overlay_player");
		push_overlay(side.behind_walls_overlay, category, "overlay_behind_walls");
		push_overlay(side.on_shot_overlay, category, "overlay_on_shot");
		push_overlay(side.history_overlay, category, "overlay_history");
		push_overlay(side.ragdoll_overlay, category, "overlay_ragdolls");
		push_item(&side.on_shot_material, category, "on_shot_material", side.on_shot_material);
		push_item(&side.on_shot_color, category, "on_shot_color", side.on_shot_color);
		push_item(&side.on_shot_secondary, category, "on_shot_secondary", side.on_shot_secondary);
		push_item(&side.on_shot_gradient, category, "on_shot_gradient", side.on_shot_gradient);
		push_item(&side.history_material, category, "history_material", side.history_material);
		push_item(&side.history_color, category, "history_color", side.history_color);
		push_item(&side.history_secondary, category, "history_secondary", side.history_secondary);
		push_item(&side.history_gradient, category, "history_gradient", side.history_gradient);
		push_item(&side.ragdoll_material, category, "ragdoll_material", side.ragdoll_material);
		push_item(&side.ragdoll_color, category, "ragdoll_color", side.ragdoll_color);
		push_item(&side.ragdoll_secondary, category, "ragdoll_secondary", side.ragdoll_secondary);
		push_item(&side.ragdoll_gradient, category, "ragdoll_gradient", side.ragdoll_gradient);
		push_item(&side.custom_sprite, category, "custom_sprite", side.custom_sprite);
		for (int i = 0; i < player_visuals::main_count; ++i) {
			push_item(&side.main[i], category, std::format("main_{}", main_names[i]), side.main[i]);
			push_esp_color(side.main_colors[i], category, std::format("main_{}_color", main_names[i]));
		}
		for (int i = 0; i < player_visuals::flag_count; ++i) {
			push_item(&side.flags[i], category, std::format("flag_{}", flag_names[i]), side.flags[i]);
			push_esp_color(side.flag_colors[i], category, std::format("flag_{}_color", flag_names[i]));
		}
		for (int i = 0; i < player_visuals::weapon_count; ++i) {
			push_item(&side.weapon[i], category, std::format("weapon_{}", weapon_item_names[i]), side.weapon[i]);
			push_esp_color(side.weapon_colors[i], category, std::format("weapon_{}_color", weapon_item_names[i]));
		}
		for (int i = 0; i < player_visuals::aimbot_count; ++i) {
			push_item(&side.aimbot[i], category, std::format("aimbot_{}", aimbot_names[i]), side.aimbot[i]);
			push_esp_color(side.aimbot_colors[i], category, std::format("aimbot_{}_color", aimbot_names[i]));
		}
		for (int i = 0; i < player_visuals::item_count; ++i) {
			push_item(&side.offsets[i].x, category, std::format("offset_{}_x", i), side.offsets[i].x);
			push_item(&side.offsets[i].y, category, std::format("offset_{}_y", i), side.offsets[i].y);
			push_item(&side.slots[i], category, std::format("slot_{}", i), side.slots[i]);
		}
	}
	auto& self_esp = player_visuals::self_esp_settings();
	push_item(&self_esp.esp_enabled, "self_visuals", "esp_enabled", self_esp.esp_enabled);
	for (int i = 0; i < player_visuals::main_count; ++i) {
		push_item(&self_esp.main[i], "self_visuals", std::format("main_{}", main_names[i]), self_esp.main[i]);
		push_esp_color(self_esp.main_colors[i], "self_visuals", std::format("main_{}_color", main_names[i]));
	}
	for (int i = 0; i < player_visuals::flag_count; ++i) {
		push_item(&self_esp.flags[i], "self_visuals", std::format("flag_{}", flag_names[i]), self_esp.flags[i]);
		push_esp_color(self_esp.flag_colors[i], "self_visuals", std::format("flag_{}_color", flag_names[i]));
	}
	for (int i = 0; i < player_visuals::weapon_count; ++i) {
		push_item(&self_esp.weapon[i], "self_visuals", std::format("weapon_{}", weapon_item_names[i]), self_esp.weapon[i]);
		push_esp_color(self_esp.weapon_colors[i], "self_visuals", std::format("weapon_{}_color", weapon_item_names[i]));
	}
	for (int i = 0; i < player_visuals::aimbot_count; ++i) {
		push_item(&self_esp.aimbot[i], "self_visuals", std::format("aimbot_{}", aimbot_names[i]), self_esp.aimbot[i]);
		push_esp_color(self_esp.aimbot_colors[i], "self_visuals", std::format("aimbot_{}_color", aimbot_names[i]));
	}
	for (int i = 0; i < player_visuals::item_count; ++i) {
		push_item(&self_esp.offsets[i].x, "self_visuals", std::format("offset_{}_x", i), self_esp.offsets[i].x);
		push_item(&self_esp.offsets[i].y, "self_visuals", std::format("offset_{}_y", i), self_esp.offsets[i].y);
		push_item(&self_esp.slots[i], "self_visuals", std::format("slot_{}", i), self_esp.slots[i]);
	}
	auto& self = player_visuals::self_settings();
	push_item(&self.local_enabled, "self_visuals", "local_enabled", self.local_enabled);
	push_item(&self.local_fake_enabled, "self_visuals", "local_fake_enabled", self.local_fake_enabled);
	push_item(&self.viewmodel_enabled, "self_visuals", "viewmodel_enabled", self.viewmodel_enabled);
	push_item(&self.weapon_enabled, "self_visuals", "weapon_enabled", self.weapon_enabled);
	push_item(&self.attachments_enabled, "self_visuals", "attachments_enabled", self.attachments_enabled);
	push_item(&self.local_material, "self_visuals", "local_material", self.local_material);
	push_item(&self.local_fake_material, "self_visuals", "local_fake_material", self.local_fake_material);
	push_item(&self.viewmodel_material, "self_visuals", "viewmodel_material", self.viewmodel_material);
	push_item(&self.weapon_material, "self_visuals", "weapon_material", self.weapon_material);
	push_item(&self.attachments_material, "self_visuals", "attachments_material", self.attachments_material);
	push_item(&self.local_color, "self_visuals", "local_color", self.local_color);
	push_item(&self.local_secondary, "self_visuals", "local_secondary", self.local_secondary);
	push_item(&self.local_fake_color, "self_visuals", "local_fake_color", self.local_fake_color);
	push_item(&self.local_fake_secondary, "self_visuals", "local_fake_secondary", self.local_fake_secondary);
	push_item(&self.viewmodel_color, "self_visuals", "viewmodel_color", self.viewmodel_color);
	push_item(&self.viewmodel_secondary, "self_visuals", "viewmodel_secondary", self.viewmodel_secondary);
	push_item(&self.weapon_color, "self_visuals", "weapon_color", self.weapon_color);
	push_item(&self.weapon_secondary, "self_visuals", "weapon_secondary", self.weapon_secondary);
	push_item(&self.attachments_color, "self_visuals", "attachments_color", self.attachments_color);
	push_item(&self.attachments_secondary, "self_visuals", "attachments_secondary", self.attachments_secondary);
	push_item(&self.glow_enabled, "self_visuals", "glow_enabled", self.glow_enabled);
	push_item(&self.glow_color, "self_visuals", "glow_color", self.glow_color);
	push_item(&self.glow_secondary, "self_visuals", "glow_secondary", self.glow_secondary);
	push_item(&self.overlay_chams, "self_visuals", "overlay_chams", self.overlay_chams);
	push_overlay(self.local_overlay, "self_visuals", "overlay_local");
	push_overlay(self.local_fake_overlay, "self_visuals", "overlay_local_fake");
	push_overlay(self.viewmodel_overlay, "self_visuals", "overlay_viewmodel");
	push_overlay(self.weapon_overlay, "self_visuals", "overlay_weapon");
	push_overlay(self.attachments_overlay, "self_visuals", "overlay_attachments");
	push_item(&self.custom_sprite, "self_visuals", "custom_sprite", self.custom_sprite);
	auto& world = player_visuals::world_settings();
	push_item(&world.enabled, "world_esp", "enabled", world.enabled);
	push_item(&world.box, "world_esp", "box", world.box);
	push_item(&world.name, "world_esp", "name", world.name);
	push_item(&world.icon, "world_esp", "icon", world.icon);
	push_item(&world.ammo_bar, "world_esp", "ammo_bar", world.ammo_bar);
	push_item(&world.smoke_range, "world_esp", "smoke_range", world.smoke_range);
	push_item(&world.molotov_range, "world_esp", "molotov_range", world.molotov_range);
	push_item(&world.bombs, "world_esp", "bombs", world.bombs);
	push_item(&world.weapons, "world_esp", "weapons", world.weapons);
	push_item(&world.hostages, "world_esp", "hostages", world.hostages);
	push_item(&world.grenades, "world_esp", "grenades", world.grenades);
	push_item(&world.windows, "world_esp", "windows", world.windows);
	push_item(&world.grenade_trajectory, "world_esp", "grenade_trajectory", world.grenade_trajectory);
	push_item(&world.grenade_proximity_warnings, "world_esp", "grenade_proximity_warnings", world.grenade_proximity_warnings);
	push_item(&world.box_color, "world_esp", "box_color", world.box_color);
	push_item(&world.name_color, "world_esp", "name_color", world.name_color);
	push_item(&world.icon_color, "world_esp", "icon_color", world.icon_color);
	push_item(&world.ammo_color, "world_esp", "ammo_color", world.ammo_color);
	push_item(&world.smoke_color, "world_esp", "smoke_color", world.smoke_color);
	push_item(&world.molotov_color, "world_esp", "molotov_color", world.molotov_color);
}

void c_config_system::push_item( bool* pointer, std::string category, std::string name, bool def_value )
{
	this->booleans.push_back( { pointer, category, name, def_value } );
}

void c_config_system::push_item( int* pointer, std::string category, std::string name, int def_value )
{
	this->ints.push_back( { pointer, category, name, def_value } );
}

void c_config_system::push_item( float* pointer, std::string category, std::string name, float def_value )
{
	this->floats.push_back( { pointer, category, name, def_value } );
}

void c_config_system::push_item( c_color* pointer, std::string category, std::string name, c_color def_value )
{
	this->colors_rgb.push_back( { pointer, category, name, def_value } );
}

void c_config_system::push_item( std::string* pointer, std::string category, std::string name, std::string def_value )
{
	this->strings.push_back( { pointer, category, name, def_value } );
}

inline bool FileExists( const std::string& name ) {
	struct stat buffer;
	return ( stat( name.c_str( ), &buffer ) == 0 );
}

using nlohmann::json;
void c_config_system::read( std::string path ) {
	path = "c:\\airflow\\configs\\" + path + ".cfg";

	if ( !FileExists( path ) )
		return;

	std::ifstream reader( path );
	if ( !reader )
		return;

	json data;
	try {
		reader >> data;
	}
	catch ( const json::exception& ) {
		return;
	}
	if ( !data.is_object() )
		return;

	auto value_of = [&data]( const std::string& category, const std::string& name ) -> const json* {
		const auto group = data.find( category );
		if ( group == data.end() || !group->is_object() )
			return nullptr;
		const auto value = group->find( name );
		return value == group->end() ? nullptr : &*value;
	};
	auto read_int = []( const json& value, int& out ) -> bool {
		if ( !value.is_number_integer() )
			return false;
		if ( value.is_number_unsigned() ) {
			const auto number = value.get<std::uint64_t>();
			if ( number > static_cast<std::uint64_t>( (std::numeric_limits<int>::max)() ) )
				return false;
			out = static_cast<int>( number );
		}
		else {
			const auto number = value.get<std::int64_t>();
			if ( number < (std::numeric_limits<int>::min)() || number > (std::numeric_limits<int>::max)() )
				return false;
			out = static_cast<int>( number );
		}
		return true;
	};

	for ( auto& b : booleans )
		if ( const auto* value = value_of( b.category, b.name ); value && value->is_boolean() )
			*b.pointer = value->get<bool>();

	for ( auto& i : ints ) {
		int value = 0;
		if ( const auto* field = value_of( i.category, i.name ); field && read_int( *field, value ) ) {
			if ( i.name.find( "key_bind_style" ) != std::string::npos || i.name.find( "keybind_style" ) != std::string::npos )
				*i.pointer = value >= 0 && value <= 2 ? value : 0;
			else if ( i.name.find( "key_bind" ) != std::string::npos || i.name.find( "keybind" ) != std::string::npos )
				*i.pointer = value >= 0 && value <= 0xA5 ? value : 0;
			else
				*i.pointer = value;
		}
	}

	for ( auto& f : floats )
		if ( const auto* value = value_of( f.category, f.name ); value && value->is_number() ) {
			const float number = value->get<float>();
			if ( std::isfinite( number ) )
				*f.pointer = number;
		}

	for ( auto& s : strings )
		if ( const auto* value = value_of( s.category, s.name ); value && value->is_string() )
			*s.pointer = value->get<std::string>();

	for ( auto& c : colors_rgb ) {
		const auto* color = value_of( c.category, c.name );
		if ( !color || !color->is_object() )
			continue;
		for ( const auto& channel : { std::pair{ "r", &c.pointer->r }, std::pair{ "g", &c.pointer->g },
			std::pair{ "b", &c.pointer->b }, std::pair{ "a", &c.pointer->a } } ) {
			const auto value = color->find( channel.first );
			if ( value != color->end() && value->is_number() ) {
				const float number = value->get<float>();
				if ( std::isfinite( number ) )
					*channel.second = number;
			}
		}
	}

	for ( auto& i : keybinds )
		if ( const auto* value = value_of( i.category, i.name ); value ) {
			int key = 0;
			if ( read_int( *value, key ) )
				*i.pointer = key >= 0 && key <= 0xA5 ? key : 0;
		}

	// Before the checkbox rows were introduced, a non-zero Self material was
	// the enable switch. Migrate only missing keys; an explicit false in a new
	// config must stay false even when its material selection is non-zero.
	auto& self = player_visuals::self_settings();
	const auto migrate_self_enable = [&value_of]( const char* name, bool& enabled, int material ) {
		if ( !value_of( "self_visuals", name ) )
			enabled = material > 0;
	};
	migrate_self_enable( "local_enabled", self.local_enabled, self.local_material );
	migrate_self_enable( "viewmodel_enabled", self.viewmodel_enabled, self.viewmodel_material );
	migrate_self_enable( "weapon_enabled", self.weapon_enabled, self.weapon_material );
	migrate_self_enable( "attachments_enabled", self.attachments_enabled, self.attachments_material );

	// Older configs stored zero alpha for World colors whose tint is opaque.
	g_cfg->world.m_sky.a = 1.f;
	g_cfg->world.m_sky_clouds.a = 1.f;
	g_cfg->world.m_wall.a = 1.f;
	g_cfg->world.m_lighting.a = 1.f;
}

void c_config_system::save( std::string path ) {
	std::error_code directory_error;
	std::filesystem::create_directories( "c:\\airflow\\configs\\", directory_error );
	if ( directory_error )
		return;
	path = "c:\\airflow\\configs\\" + path + ".cfg";

	json ret_data;
	auto sanity_checks = [&]( std::string category ) -> void {
		if ( ret_data.find( category ) == ret_data.end( ) )
			ret_data[ category ] = json( );
	};

	for ( auto& b : booleans ) {
		sanity_checks( b.category );
		ret_data[ b.category ][ b.name ] = *b.pointer;
	}

	for ( auto& i : ints ) {
		sanity_checks( i.category );
		ret_data[ i.category ][ i.name ] = *i.pointer;
	}

	for ( auto& f : floats ) {
		sanity_checks( f.category );
		ret_data[ f.category ][ f.name ] = *f.pointer;
	}

	for ( auto& s : strings ) {
		sanity_checks( s.category );
		ret_data[ s.category ][ s.name ] = *s.pointer;
	}

	for ( auto& c : colors_rgb ) {
		sanity_checks( c.category );
		json d;
		c_color clr = *c.pointer;
		d[ "r" ] = clr.r;
		d[ "g" ] = clr.g;
		d[ "b" ] = clr.b;
		d[ "a" ] = clr.a;
		ret_data[ c.category ][ c.name ] = d;
	}

	for ( auto& i : keybinds ) {
		sanity_checks( i.category );
		ret_data[ i.category ][ i.name ] = *i.pointer;
	}

	std::ofstream writer( path, std::ios::trunc );
	if ( writer )
		writer << std::setw( 4 ) << ret_data << std::endl;
}

void c_config_system::refresh( ) {
	config_files.clear( );
	std::error_code error;
	if ( !std::filesystem::exists( "c:\\airflow\\configs\\", error ) )
		return;
	for ( const auto& p : std::filesystem::directory_iterator( "c:\\airflow\\configs\\", error ) ) {
		if ( !std::filesystem::is_directory( p ) &&
			p.path( ).extension( ).string( ) == ".cfg" ) {
			auto file = p.path( ).filename( ).string( );
			config_files.push_back( file.substr( 0, file.size( ) - 4 ) );
		}
	}
	std::sort(config_files.begin(), config_files.end());
}

void c_config_system::remove( std::string path ) {
	path = "c:\\airflow\\configs\\" + path + ".cfg";

	std::filesystem::remove( path );
	refresh( );
}

void c_config_system::load( std::string path ) {
	// load is an alias for read
	read( path );
}

void c_config_system::reset( ) {
	for ( auto& b : booleans )
		*b.pointer = b.default_value;
	for ( auto& i : ints )
		*i.pointer = i.default_value;
	for ( auto& c : colors_rgb )
		*c.pointer = c.default_value;
	for ( auto& f : floats )
		*f.pointer = f.default_value;
	for ( auto& s : strings )
		*s.pointer = s.default_value;
	for ( auto& i : keybinds )
		*i.pointer = i.default_value;
}

static std::string selected_cfg = "";
bool confirm_opened = false;
bool confirmed = false;
bool delete_cfg = false;

void confirm_save( bool deleting )
{
	confirm_opened = true;
	delete_cfg = deleting;
}

void final_save( )
{
	if ( delete_cfg )
		g_config_system->remove( selected_cfg );
	else
		g_config_system->save( selected_cfg );


	confirm_opened = false;
	confirmed = true;
}

void no_confirm( )
{
	confirm_opened = false;
	confirmed = false;
}

void window( )
{
	if ( !confirm_opened )
		return;

	ImGui::Begin( "confirmation", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar );
	ImGui::SetWindowSize( ImVec2( 240, 145 ) );
	ImGui::SetCursorPosY( 39 );

	if ( confirm_opened )
		delete_cfg ? ImGui::Text( "are you sure want to delete this config?" ) : ImGui::Text( "are you sure want to save this config?" );

	ImGui::SetCursorPosX( 4 );
	ImGui::SetCursorPosY( 61 );

	if ( confirm_opened && ImGui::Button( delete_cfg ? "delete" : "save" ) )
		final_save( );

	ImGui::SetCursorPosY( 86 );
	ImGui::SetCursorPosX( 4 );

	if ( confirm_opened && ImGui::Button( "cancel" ) )
		no_confirm( );

	ImGui::End( );
}

void c_config_system::menu( ) {
	refresh( );

	static char cfg_name[ 26 ];

	if ( ImGui::BeginChild( "Settings" ) ) {
		if ( ImGui::BeginListBox( "##cfglist", ImVec2( -1, -205 ) ) ) {
			for ( const auto& cfg : config_files )
				if ( ImGui::Selectable( cfg.c_str( ), cfg == selected_cfg ) )
					selected_cfg = cfg;

			ImGui::EndListBox( );
		}
		ImGui::SetCursorPosY( ImGui::GetWindowSize( ).y - 193 );
		if ( ImGui::InputText( " ", cfg_name, 26 ) ) selected_cfg = std::string( cfg_name );
		if ( std::string( cfg_name ) != "" )
		{
			ImGui::SetCursorPosX( 4 );
			if ( ImGui::Button( "create" ) ) {

				if ( !selected_cfg.empty( ) )
					save( selected_cfg );

				refresh( );
			}
		}
		ImGui::SetCursorPosX( 4 );
		if ( ImGui::Button( "load" ) ) {
			if ( !selected_cfg.empty( ) )
				read( selected_cfg );

			refresh( );
		}
		ImGui::SetCursorPosX( 4 );
		if ( ImGui::Button( "save" ) ) {
			if ( !selected_cfg.empty( ) )
				confirm_save( false );

			refresh( );
		}

		window( );

		ImGui::SetCursorPosX( 4 );
		if ( ImGui::Button( "delete") ) {
			if ( !selected_cfg.empty( ) )
				confirm_save( true );

			refresh( );
		}
		ImGui::SetCursorPosX( 4 );
		if ( ImGui::Button( "reset" ) )
			reset( );

		ImGui::SetCursorPosX( 4 );
		if ( ImGui::Button( "refresh" ) )
			refresh( );
	}
	ImGui::EndChild( );
}