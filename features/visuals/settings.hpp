#pragma once

#include "../../sdk/typedefs/c_color.hpp"
#include "chams/chams.hpp"

#include <array>
#include <cstddef>
#include <string>

// Shared by the Darkside Players page and the in-game renderer. Keeping these
// values outside menu-local state is what lets the ESP remain unchanged after
// the menu is closed and makes config Save/Load cover the complete layout.
namespace player_visuals {
inline constexpr int side_count = 2; // Enemies/Teammates; Self has a separate ESP settings set.
inline constexpr int main_count = 6;
inline constexpr int flag_count = 11;
// Keep the old numeric layout IDs for offsets/slots. The four removed flags
// leave holes here, but no longer have controls, rendering, or config entries.
inline constexpr int layout_flag_count = 15;
inline constexpr int weapon_count = 6;
inline constexpr int aimbot_count = 1;
inline constexpr int item_count = main_count + layout_flag_count + weapon_count + aimbot_count;

enum main_item : int {
    main_bounding_box = 0,
    main_name,
    main_distance,
    main_health_bar,
    main_ammo_bar,
    main_skeleton,
};

enum flag_item : int {
    flag_defuser = 0,
    flag_blind,
    flag_scoped,
    flag_reload,
    flag_immunity,
    flag_slowed,
    flag_bomb,
    flag_hostage,
    flag_defuse,
    flag_pin_pulled,
    flag_money,
};

enum weapon_item : int {
    weapon_text = 0,
    weapon_icon,
    weapon_readiness_bar,
    weapon_bomb,
    weapon_defuser,
    weapon_taser,
};

enum aimbot_item : int {
    aimbot_hitboxes = 0,
};

inline constexpr int flag_layout_index(int flag) {
    // Legacy flag slots 0, 7, 13 and 14 are intentionally unused.
    // Retained flags keep their old drag/config layout IDs after the removal.
    constexpr int indices[flag_count] = { 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12 };
    return flag >= 0 && flag < flag_count ? indices[flag] : -1;
}
inline constexpr int flag_item_id(int flag) {
    const int index = flag_layout_index(flag);
    return index >= 0 ? main_count + index : -1;
}
inline constexpr int weapon_item_id(int item) { return main_count + layout_flag_count + item; }
inline constexpr int aimbot_item_id(int item) {
    return main_count + layout_flag_count + weapon_count + item;
}

struct layout_offset {
    float x = 0.f;
    float y = 0.f;
};

// ESP colors use the same primary/secondary/gradient model as the existing chams
// settings. The renderer uses the primary color for single-color elements; keeping
// the other two fields means the standard Darkside picker remains fully persistent.
struct esp_color {
    c_color primary{ 1.f, 1.f, 1.f, 1.f };
    c_color secondary{ 1.f, 1.f, 1.f, 1.f };
    bool gradient = false;
};

inline void set_esp_color(esp_color& target, const c_color& value) {
    target.primary = value;
    target.secondary = value;
    target.gradient = false;
}

struct material_selection {
    bool values[visuals_chams::mode_count] = {};
    // Overlay materials own their colors independently. The flag keeps old
    // configs on the legacy category color until overlay mode is first stored.
    esp_color colors[visuals_chams::mode_count] = {};
    bool colors_initialized = false;

    bool any() const {
        for (int i = 1; i < visuals_chams::mode_count; ++i)
            if (values[i])
                return true;
        return false;
    }
};

struct side_settings {
    bool esp_enabled = false;
    bool offscreen_arrow = false;
    bool sounds = false;
    c_color sounds_color{ 1.f, 0.25f, 0.55f, 1.f };
    c_color offscreen_color{ 0.92f, 0.94f, 1.f, 1.f };
    c_color offscreen_outline{ 0.f, 0.f, 0.f, 1.f };
    bool main[main_count] = {};
    bool flags[flag_count] = {};
    bool weapon[weapon_count] = {};
    bool aimbot[aimbot_count] = {};
    esp_color main_colors[main_count] = {};
    esp_color flag_colors[flag_count] = {};
    esp_color weapon_colors[weapon_count] = {};
    esp_color aimbot_colors[aimbot_count] = {};
    layout_offset offsets[item_count] = {};
    // -1 keeps the legacy/default anchor; 0..11 are gamesense-style slots.
    int slots[item_count] = {};

    int player_material = 0;
    int behind_walls_material = 0;
    c_color player_color{ 0.60f, 0.65f, 1.f, 1.f };
    c_color player_secondary{ 0.60f, 0.65f, 1.f, 1.f };
    c_color behind_walls_color{ 0.35f, 0.90f, 1.f, 1.f };
    c_color behind_walls_secondary{ 0.35f, 0.90f, 1.f, 1.f };
    bool player_gradient = false;
    bool behind_walls_gradient = false;

    // Optional multi-material mode. The legacy single-material fields above
    // remain untouched so switching modes does not lose the old selection.
    bool overlay_chams = false;
    material_selection player_overlay;
    material_selection behind_walls_overlay;
    material_selection on_shot_overlay;
    material_selection history_overlay;
    material_selection ragdoll_overlay;

    int on_shot_material = 0;
    int history_material = 0;
    int ragdoll_material = 0;
    c_color on_shot_color{ 0.90f, 0.95f, 0.95f, 1.f };
    c_color on_shot_secondary = on_shot_color;
    c_color history_color{ 0.75f, 0.75f, 0.78f, 1.f };
    c_color history_secondary = history_color;
    c_color ragdoll_color{ 0.65f, 0.63f, 0.75f, 1.f };
    c_color ragdoll_secondary = ragdoll_color;
    bool on_shot_gradient = false;
    bool history_gradient = false;
    bool ragdoll_gradient = false;

    std::string custom_sprite = "models/weapons/customization/paints/custom/money";
};

struct self_chams_settings {
    // Kept for compatibility with the checkbox-era config schema. The Self UI
    // now treats material == Disabled as the canonical Off state.
    bool local_enabled = false;
    bool local_fake_enabled = false; // UI/config placeholder; no fake render record exists yet.
    bool viewmodel_enabled = false;
    bool weapon_enabled = false;
    bool attachments_enabled = false;
    int local_material = 0;
    int local_fake_material = 0; // Stored for the placeholder row only.
    int viewmodel_material = 0;
    int weapon_material = 0;
    int attachments_material = 0;
    bool overlay_chams = false;
    material_selection local_overlay;
    material_selection local_fake_overlay;
    material_selection viewmodel_overlay;
    material_selection weapon_overlay;
    material_selection attachments_overlay;
    c_color local_color{ 0.60f, 0.65f, 1.f, 1.f };
    c_color local_secondary = local_color;
    c_color local_fake_color{ 0.60f, 0.65f, 1.f, 1.f };
    c_color local_fake_secondary = local_fake_color;
    c_color viewmodel_color{ 0.60f, 0.65f, 1.f, 1.f };
    c_color viewmodel_secondary = viewmodel_color;
    c_color weapon_color{ 0.60f, 0.65f, 1.f, 1.f };
    c_color weapon_secondary = weapon_color;
    c_color attachments_color{ 0.60f, 0.65f, 1.f, 1.f };
    c_color attachments_secondary = attachments_color;
    // Independent local-player pass, separate from the Glow material option.
    bool glow_enabled = false;
    c_color glow_color{ 0.80f, 0.75f, 1.f, 1.f };
    c_color glow_secondary = glow_color;
    std::string custom_sprite = "models/weapons/customization/paints/custom/money";
};

inline self_chams_settings& self_settings() {
    static self_chams_settings value;
    return value;
}

struct world_item_settings {
    bool enabled = false;
    bool box = false;
    bool icon = false;
    bool text = false;
    bool progress_bar = false;
    bool time_remaining = false;
    bool distance = false;
    bool glow = false;
    bool trajectory = false;
    bool colorize_effects = false;
    bool colorize_smoke = false;
    bool colorize_molotov = false;
    bool colorize_explosion = false;
    bool rescue_indicator = false;
    bool bomb_indicator = false;
    bool defuse_indicator = false;
    bool defuser = false;
    bool smoke_radius = false;
    bool inferno_radius = false;
    bool projectile_inferno = false;
    c_color color{ 0.92f, 0.94f, 1.f, 1.f };
    c_color secondary_color{ 0.92f, 0.94f, 1.f, 1.f };
    c_color progress_background{ 0.f, 0.f, 0.f, 0.55f };
    c_color glow_color{ 0.92f, 0.94f, 1.f, 1.f };
};

struct world_esp_settings {
    bool enabled = false;
    bool box = true;
    bool name = true;
    bool icon = true;
    bool ammo_bar = true;
    bool smoke_range = true;
    bool molotov_range = true;
    bool bombs = true;
    bool weapons = true;
    bool hostages = true;
    bool grenades = true;
    world_item_settings bomb;
    world_item_settings weapon;
    world_item_settings hostage;
    world_item_settings grenade;
    bool windows = false;
    bool grenade_trajectory = false;
    bool grenade_proximity_warnings = false;
    bool aspect_ratio_override = false;
    int aspect_scaling = 0;
    bool scope_fov_override = false;
    int scope_fov_x1 = 40;
    int scope_fov_x2 = 10;
    bool instant_scope = false;
    bool scope_force_viewmodel_fov = false;
    bool viewmodel_fov_override = false;
    int viewmodel_fov_value = 68;
    bool viewmodel_position_override = false;
    bool viewmodel_angles_override = false;
    float viewmodel_pitch = 0.f;
    float viewmodel_yaw = 0.f;
    float viewmodel_roll = 0.f;
    bool viewmodel_modifiers_override = false;
    float modifier_bobbing = 0.5f;
    float modifier_rotation_sway = 0.5f;
    float modifier_velocity_sway = 0.5f;
    float modifier_in_air_offset = 0.5f;
    float modifier_lower_weapon = 0.5f;
    float modifier_ladder_bob = 0.5f;
    bool thirdperson_close_to_wall = true;
    bool thirdperson_disable_overhead = false;
    int unlock_spectating_mode = 0;
    int visual_recoil_mode = 0;
    int preserve_death_notices_mode = 0;
    int radar_mode = 0;
    std::array<bool, 4> radar_options{};
    int scope_overlay_mode = 0;
    c_color inaccuracy_color{ 0.92f, 0.92f, 0.92f, 1.f };
    bool preserve_death_notices = false;
    bool hide_teammate_assists = false;
    float scoreboard_opacity = 0.9f;
    float scoreboard_width = 0.5f;
    bool scoreboard_reveal_money = false;
    int scoreboard_equipment_mode = 0;
    std::array<bool, 9> scoreboard_equipment{ true, true, false, false, true, true, true, false, false };
    bool force_sniper_crosshair = false;
    bool force_crosshair_scoped = false;
    bool force_crosshair_c4 = false;
    bool hide_crosshair_knife = false;
    bool hide_crosshair_grenade = false;
    bool penetration_crosshair = false;
    c_color penetration_color{ 0.75f, 0.45f, 0.95f, 1.f };
    bool window_watermark = true;
    bool window_hotkeys = true;
    bool window_spectators = true;
    bool window_event_logs = true;
    std::array<bool, 4> watermark_items{ true, true, true, true };
    c_color windows_color{ 0.35f, 0.95f, 0.60f, 1.f };
    float windows_line_opacity = 1.f;
    float windows_background_opacity = 1.f;
    bool windows_hit_chance_override = false;
    bool windows_min_damage_override = false;
    bool windows_double_tap = false;
    bool windows_duck_peek = false;
    bool windows_slowdown = false;
    bool windows_jump_bug = false;
    std::array<bool, 13> removals{};
    int ragdoll_mode = 0;
    float ragdoll_gravity = 0.5f;
    float ragdoll_friction = 0.5f;
    bool hitmarker_2d = false;
    bool hitmarker_3d = false;
    bool damage_marker = false;
    float hitmarker_2d_duration = 0.25f;
    float hitmarker_3d_duration = 0.25f;
    float damage_marker_duration = 0.25f;
    c_color hitmarker_2d_color{ 1.f, 1.f, 1.f, 1.f };
    c_color hitmarker_3d_color{ 1.f, 1.f, 1.f, 1.f };
    c_color damage_marker_color{ 1.f, 1.f, 1.f, 1.f };
    bool tracer_enemies = false;
    bool tracer_teammates = false;
    bool tracer_local_player = false;
    float tracer_enemy_duration = 0.3f;
    float tracer_teammate_duration = 0.3f;
    float tracer_local_duration = 0.3f;
    c_color tracer_enemy_color{ 1.f, 0.35f, 0.45f, 1.f };
    c_color tracer_teammate_color{ 1.f, 0.72f, 0.48f, 1.f };
    c_color tracer_local_color{ 0.60f, 0.55f, 0.95f, 1.f };
    bool tracer_use_shared_settings = true;
    c_color trajectory_color{ 1.f, 0.25f, 0.42f, 1.f };
    float trajectory_exponent = 1.f;
    float trajectory_thickness = 1.5f;
    float trajectory_points_thickness = 2.f;
    int trajectory_shimmer = 0;
    bool trajectory_thrown_grenades = true;
    bool trajectory_pin_pulled_only = false;
    bool trajectory_precise = false;
    int watermark_position = 1;
    int watermark_density = 0;
    bool watermark_additional_colors = false;
    bool watermark_display_avatar = false;
    int hotkeys_style = 0;
    bool hotkeys_right_aligned = false;
    bool hotkeys_dynamic_header = false;
    bool bullet_impacts = false;
    bool impact_sparks = false;
    c_color bullet_impact_color{ 0.90f, 0.60f, 0.90f, 1.f };
    c_color impact_sparks_color{ 1.f, 1.f, 1.f, 1.f };
    c_color box_color{ 0.92f, 0.94f, 1.f, 1.f };
    c_color name_color{ 0.92f, 0.94f, 1.f, 1.f };
    c_color icon_color{ 0.92f, 0.94f, 1.f, 1.f };
    c_color ammo_color{ 0.55f, 0.72f, 1.f, 1.f };
    c_color smoke_color{ 0.65f, 0.65f, 0.65f, 1.f };
    c_color molotov_color{ 1.f, 0.35f, 0.12f, 1.f };
    c_color explosion_color{ 0.48f, 0.63f, 1.f, 1.f };
    c_color effects_color{ 1.f, 0.30f, 0.67f, 1.f };
};

inline world_esp_settings& world_settings() {
    static world_esp_settings value;
    return value;
}

inline std::array<side_settings, side_count> make_default_settings() {
    std::array<side_settings, side_count> result{};
    for (auto& side : result) {
        for (int item = 0; item < item_count; ++item)
            side.slots[item] = -1;
        side.main[main_bounding_box] = true;
        side.main[main_name] = true;
        side.main[main_health_bar] = true;
        side.main[main_ammo_bar] = true;
        side.weapon[weapon_text] = true;

        set_esp_color(side.main_colors[main_name], { 0.94f, 0.96f, 1.f, 1.f });
        set_esp_color(side.main_colors[main_distance], { 0.94f, 0.96f, 1.f, 1.f });
        set_esp_color(side.main_colors[main_health_bar], { 0.25f, 1.f, 0.35f, 1.f });
        set_esp_color(side.main_colors[main_ammo_bar], { 0.55f, 0.72f, 1.f, 1.f });

        for (auto& flag : side.flag_colors)
            set_esp_color(flag, { 0.92f, 0.94f, 1.f, 1.f });
        set_esp_color(side.flag_colors[flag_money], { 0.45f, 0.95f, 0.45f, 1.f });
        set_esp_color(side.flag_colors[flag_immunity], { 0.40f, 0.75f, 1.f, 1.f });
        set_esp_color(side.flag_colors[flag_defuse], { 0.40f, 0.75f, 1.f, 1.f });
        set_esp_color(side.flag_colors[flag_bomb], { 1.f, 0.48f, 0.42f, 1.f });
        set_esp_color(side.flag_colors[flag_pin_pulled], { 1.f, 0.48f, 0.42f, 1.f });

        set_esp_color(side.weapon_colors[weapon_text], { 0.94f, 0.96f, 1.f, 1.f });
        set_esp_color(side.weapon_colors[weapon_icon], { 0.94f, 0.96f, 1.f, 1.f });
        set_esp_color(side.weapon_colors[weapon_readiness_bar], { 0.50f, 1.f, 0.72f, 1.f });
        set_esp_color(side.weapon_colors[weapon_bomb], { 1.f, 0.55f, 0.35f, 1.f });
        set_esp_color(side.weapon_colors[weapon_defuser], { 0.45f, 0.80f, 1.f, 1.f });
        set_esp_color(side.weapon_colors[weapon_taser], { 1.f, 0.90f, 0.35f, 1.f });
        set_esp_color(side.aimbot_colors[aimbot_hitboxes], { 1.f, 0.35f, 0.55f, 0.90f });
    }
    result[0].esp_enabled = true;
    result[1].player_color = { 0.78f, 0.77f, 1.f, 1.f };
    result[1].player_secondary = result[1].player_color;
    result[1].behind_walls_color = { 1.f, 0.78f, 0.60f, 1.f };
    result[1].behind_walls_secondary = result[1].behind_walls_color;

    // Box and skeleton historically followed the side's chams color. Keep those
    // defaults, but from now on each ESP element owns an independent setting.
    for (auto& side : result) {
        side.main_colors[main_bounding_box].primary = side.player_color;
        side.main_colors[main_bounding_box].secondary = side.player_secondary;
        side.main_colors[main_bounding_box].gradient = side.player_gradient;
        side.main_colors[main_skeleton].primary = side.player_color;
        side.main_colors[main_skeleton].secondary = side.player_secondary;
        side.main_colors[main_skeleton].gradient = side.player_gradient;
    }
    return result;
}

inline side_settings& self_esp_settings() {
    static side_settings value = [] {
        const auto defaults = make_default_settings();
        side_settings result = defaults[0];
        result.esp_enabled = false;
        result.offscreen_arrow = false;
        return result;
    }();
    return value;
}

inline std::array<side_settings, side_count>& all_settings() {
    static std::array<side_settings, side_count> value = make_default_settings();
    return value;
}

inline side_settings& settings(int side) {
    return all_settings()[static_cast<std::size_t>(side == 1 ? 1 : 0)];
}

inline const side_settings& settings_const(int side) {
    return all_settings()[static_cast<std::size_t>(side == 1 ? 1 : 0)];
}

inline bool item_enabled(const side_settings& settings, int item) {
    if (item >= 0 && item < main_count)
        return settings.main[item];
    for (int flag = 0; flag < flag_count; ++flag)
        if (flag_item_id(flag) == item)
            return settings.flags[flag];
    constexpr int weapon_begin = main_count + layout_flag_count;
    constexpr int aimbot_begin = weapon_begin + weapon_count;
    if (item >= weapon_begin && item < aimbot_begin)
        return settings.weapon[item - weapon_begin];
    if (item >= aimbot_begin && item < item_count)
        return settings.aimbot[item - aimbot_begin];
    return false;
}
} // namespace player_visuals
