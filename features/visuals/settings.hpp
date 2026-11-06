#pragma once

#include "../../sdk/typedefs/c_color.hpp"
#include "chams.hpp"

#include <array>
#include <cstddef>
#include <string>

// Shared by the Darkside Players page and the in-game renderer. Keeping these
// values outside menu-local state is what lets the ESP remain unchanged after
// the menu is closed and makes config Save/Load cover the complete layout.
namespace player_visuals {
inline constexpr int side_count = 2; // Enemies/Teammates; Self has a separate ESP settings set.
inline constexpr int main_count = 6;
inline constexpr int flag_count = 15;
inline constexpr int weapon_count = 6;
inline constexpr int aimbot_count = 1;
inline constexpr int item_count = main_count + flag_count + weapon_count + aimbot_count;

enum main_item : int {
    main_bounding_box = 0,
    main_name,
    main_distance,
    main_health_bar,
    main_ammo_bar,
    main_skeleton,
};

enum flag_item : int {
    flag_unarmored = 0,
    flag_defuser,
    flag_blind,
    flag_scoped,
    flag_reload,
    flag_immunity,
    flag_slowed,
    flag_vulnerable,
    flag_bomb,
    flag_hostage,
    flag_defuse,
    flag_pin_pulled,
    flag_money,
    flag_order_priority,
    flag_delay,
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

inline constexpr int flag_item_id(int flag) { return main_count + flag; }
inline constexpr int weapon_item_id(int item) { return main_count + flag_count + item; }
inline constexpr int aimbot_item_id(int item) {
    return main_count + flag_count + weapon_count + item;
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

struct world_esp_settings {
    bool enabled = false;
    bool box = true;
    bool name = true;
    bool icon = true;
    bool ammo_bar = true;
    bool smoke_range = true;
    bool molotov_range = true;
    c_color box_color{ 0.92f, 0.94f, 1.f, 1.f };
    c_color name_color{ 0.92f, 0.94f, 1.f, 1.f };
    c_color icon_color{ 0.92f, 0.94f, 1.f, 1.f };
    c_color ammo_color{ 0.55f, 0.72f, 1.f, 1.f };
    c_color smoke_color{ 0.65f, 0.65f, 0.65f, 1.f };
    c_color molotov_color{ 1.f, 0.35f, 0.12f, 1.f };
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
        set_esp_color(side.flag_colors[flag_vulnerable], { 1.f, 0.48f, 0.42f, 1.f });
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
    if (item >= main_count && item < main_count + flag_count)
        return settings.flags[item - main_count];
    if (item >= main_count + flag_count && item < main_count + flag_count + weapon_count)
        return settings.weapon[item - main_count - flag_count];
    if (item >= main_count + flag_count + weapon_count && item < item_count)
        return settings.aimbot[item - main_count - flag_count - weapon_count];
    return false;
}
} // namespace player_visuals
