// Visuals menu page only. Gameplay ESP/rendering code belongs under features/visuals.
#include "menu.hpp"
#include "hashes.hpp"
#include "../features/visuals/chams.hpp"
#include "../features/visuals/settings.hpp"
#include "../sdk/config_system/config_system.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {
color_value make_ui_color(float r, float g, float b) {
    color_value value;
    value.a = ImVec4(r, g, b, 1.f);
    value.b = value.a;
    return value;
}

struct esp_items_ui_state {
    bool main_items[player_visuals::main_count] = {};
    bool flags[player_visuals::flag_count] = {};
    bool weapon_items[player_visuals::weapon_count] = {};
    bool aimbot_items[player_visuals::aimbot_count] = {};
};

struct chams_ui_selection {
    // Persistent storage is required by menu_binds::multibox_item().
    bool values[visuals_chams::mode_count] = {};
    color_value colors[visuals_chams::mode_count];
};

struct player_side_ui_state {
    // Missing backends stay in per-side UI state, so Enemies, Teammates and Self never
    // overwrite one another while another page is hidden.
    bool offscreen_arrow = false;
    bool sounds = false;
    int player_model = 0;
    int behind_walls_model = 0;
    int on_shot_model = 0;
    int history_model = 0;
    int ragdolls_model = 0;
    bool disable_model_rendering = false;
    bool glow = false;
    int local_model = 0;
    int local_weapon = 0;
    int local_transparency = 100;
    int local_chams_model = 0;
    int local_fake_chams_model = 0;
    int viewmodel_chams_model = 0;
    int weapon_chams_model = 0;
    int attachments_chams_model = 0;
    bool overlay_chams = false;
    chams_ui_selection player_overlay;
    chams_ui_selection behind_walls_overlay;
    chams_ui_selection on_shot_overlay;
    chams_ui_selection history_overlay;
    chams_ui_selection ragdoll_overlay;
    chams_ui_selection local_overlay;
    chams_ui_selection local_fake_overlay;
    chams_ui_selection viewmodel_overlay;
    chams_ui_selection weapon_overlay;
    chams_ui_selection attachments_overlay;

    color_value offscreen_color = make_ui_color(0.92f, 0.94f, 1.f);
    color_value sounds_color = make_ui_color(1.f, 0.25f, 0.55f);
    color_value player_color = make_ui_color(0.60f, 0.65f, 1.f);
    color_value local_fake_color = make_ui_color(0.60f, 0.65f, 1.f);
    color_value behind_walls_color = make_ui_color(0.35f, 0.90f, 1.f);
    color_value weapon_color = make_ui_color(1.f, 0.35f, 0.55f);
    color_value on_shot_color = make_ui_color(0.90f, 0.95f, 0.95f);
    color_value history_color = make_ui_color(0.75f, 0.75f, 0.78f);
    color_value ragdolls_color = make_ui_color(0.65f, 0.63f, 0.75f);
    color_value kill_effect_color = make_ui_color(1.f, 0.20f, 0.30f);
    color_value glow_color = make_ui_color(0.80f, 0.75f, 1.f);
    char custom_sprite[128] = "models/weapons/customization/paints/custom/money";
    esp_items_ui_state esp_items;
    color_value main_colors[player_visuals::main_count];
    color_value flag_colors[player_visuals::flag_count];
    color_value weapon_colors[player_visuals::weapon_count];
    color_value aimbot_colors[player_visuals::aimbot_count];
};

struct players_ui_state {
    int active_side = 0; // 0 = Enemies, 1 = Teammates, 2 = Self
    player_side_ui_state sides[3];

    players_ui_state() {
        for (auto& side : sides) {
            side.esp_items.main_items[0] = true; // Bounding Box
            side.esp_items.main_items[1] = true; // Name
            side.esp_items.main_items[3] = true; // Health Bar
            side.esp_items.main_items[4] = true; // Ammo Bar
            side.esp_items.weapon_items[0] = true; // Text
        }
        sides[1].player_color = make_ui_color(0.78f, 0.77f, 1.f);
        sides[1].behind_walls_color = make_ui_color(1.f, 0.78f, 0.60f);
        sides[1].glow_color = make_ui_color(0.70f, 0.70f, 0.72f);
    }
};

players_ui_state& player_ui() {
    static players_ui_state state;
    return state;
}

ImVec4 to_vec4(const c_color& color) {
    return ImVec4(color.r, color.g, color.b, color.a);
}

c_color to_config_color(const ImVec4& color) {
    return c_color(color.x, color.y, color.z, color.w);
}

template <std::size_t N>
void load_esp_colors(color_value (&view)[N], const player_visuals::esp_color (&config)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        view[i].a = to_vec4(config[i].primary);
        view[i].b = to_vec4(config[i].secondary);
        view[i].gradient = config[i].gradient;
    }
}

template <std::size_t N>
void store_esp_colors(player_visuals::esp_color (&config)[N], const color_value (&view)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        config[i].primary = to_config_color(view[i].a);
        config[i].secondary = to_config_color(view[i].b);
        config[i].gradient = view[i].gradient;
    }
}

void load_chams_selection(chams_ui_selection& view,
                          const player_visuals::material_selection& config,
                          const color_value& legacy_color) {
    for (int i = 0; i < visuals_chams::mode_count; ++i) {
        view.values[i] = config.values[i];
        if (config.colors_initialized) {
            view.colors[i].a = to_vec4(config.colors[i].primary);
            view.colors[i].b = to_vec4(config.colors[i].secondary);
            view.colors[i].gradient = config.colors[i].gradient;
        } else {
            // Migration path for configs written before per-material colors
            // existed: seed every slot from that row's old single color.
            view.colors[i] = legacy_color;
        }
    }
    view.values[visuals_chams::disabled] = false;
}

void store_chams_selection(player_visuals::material_selection& config,
                           const chams_ui_selection& view, bool overlay) {
    for (int i = 0; i < visuals_chams::mode_count; ++i) {
        config.values[i] = view.values[i];
        if (overlay) {
            config.colors[i].primary = to_config_color(view.colors[i].a);
            config.colors[i].secondary = to_config_color(view.colors[i].b);
            config.colors[i].gradient = view.colors[i].gradient;
        }
    }
    config.values[visuals_chams::disabled] = false;
    if (overlay)
        config.colors_initialized = true;
}

void seed_chams_selection(chams_ui_selection& selection, int material) {
    if (material <= visuals_chams::disabled || material >= visuals_chams::mode_count)
        return;
    for (int i = 1; i < visuals_chams::mode_count; ++i)
        if (selection.values[i])
            return;
    selection.values[material] = true;
}

void draw_overlay_checkbox(player_side_ui_state& side, bool self) {
    if (!gui.row_checkbox("Overlay Chams", &side.overlay_chams,
                          "Enable multi-material chams; an empty selection disables that pass.") ||
        !side.overlay_chams) {
        return;
    }
    if (self) {
        seed_chams_selection(side.local_overlay, side.local_chams_model);
        seed_chams_selection(side.local_fake_overlay, side.local_fake_chams_model);
        seed_chams_selection(side.viewmodel_overlay, side.viewmodel_chams_model);
        seed_chams_selection(side.weapon_overlay, side.weapon_chams_model);
        seed_chams_selection(side.attachments_overlay, side.attachments_chams_model);
    } else {
        seed_chams_selection(side.player_overlay, side.player_model);
        seed_chams_selection(side.behind_walls_overlay, side.behind_walls_model);
        seed_chams_selection(side.on_shot_overlay, side.on_shot_model);
        seed_chams_selection(side.history_overlay, side.history_model);
        seed_chams_selection(side.ragdoll_overlay, side.ragdolls_model);
    }
}

void draw_chams_row(const char* label, int* material, chams_ui_selection& selection,
                    color_value* color, bool overlay, const char* const* names) {
    if (!overlay) {
        gui.row_combo(label, color, material, names, visuals_chams::mode_count);
        return;
    }
    // MultiComboWithColors opens an independent picker for every material.
    // Do not mirror one slot back into the category color or the other slots.
    gui.row_multibox(label, names + 1, selection.values + 1, selection.colors + 1,
        visuals_chams::mode_count - 1,
        "Select multiple materials; each entry has its own color. Empty selection disables this pass.");
}

void load_shared_player_views(players_ui_state& ui) {
    for (int side_index = 0; side_index < player_visuals::side_count; ++side_index) {
        const auto& config = player_visuals::settings(side_index);
        auto& view = ui.sides[side_index];
        view.offscreen_arrow = config.offscreen_arrow;
        view.offscreen_color.a = to_vec4(config.offscreen_color);
        view.offscreen_color.b = view.offscreen_color.a;
        view.offscreen_color.gradient = false;
        view.player_model = std::clamp(config.player_material, 0, visuals_chams::mode_count - 1);
        view.behind_walls_model = std::clamp(config.behind_walls_material, 0, visuals_chams::mode_count - 1);
        view.player_color.a = to_vec4(config.player_color);
        view.player_color.b = to_vec4(config.player_secondary);
        view.player_color.gradient = config.player_gradient;
        view.behind_walls_color.a = to_vec4(config.behind_walls_color);
        view.behind_walls_color.b = to_vec4(config.behind_walls_secondary);
        view.behind_walls_color.gradient = config.behind_walls_gradient;
        view.on_shot_model = std::clamp(config.on_shot_material, 0, visuals_chams::mode_count - 1);
        view.history_model = std::clamp(config.history_material, 0, visuals_chams::mode_count - 1);
        view.ragdolls_model = std::clamp(config.ragdoll_material, 0, visuals_chams::mode_count - 1);
        view.on_shot_color.a = to_vec4(config.on_shot_color);
        view.on_shot_color.b = to_vec4(config.on_shot_secondary);
        view.on_shot_color.gradient = config.on_shot_gradient;
        view.history_color.a = to_vec4(config.history_color);
        view.history_color.b = to_vec4(config.history_secondary);
        view.history_color.gradient = config.history_gradient;
        view.ragdolls_color.a = to_vec4(config.ragdoll_color);
        view.ragdolls_color.b = to_vec4(config.ragdoll_secondary);
        view.ragdolls_color.gradient = config.ragdoll_gradient;
        view.overlay_chams = config.overlay_chams;
        load_chams_selection(view.player_overlay, config.player_overlay, view.player_color);
        load_chams_selection(view.behind_walls_overlay, config.behind_walls_overlay, view.behind_walls_color);
        load_chams_selection(view.on_shot_overlay, config.on_shot_overlay, view.on_shot_color);
        load_chams_selection(view.history_overlay, config.history_overlay, view.history_color);
        load_chams_selection(view.ragdoll_overlay, config.ragdoll_overlay, view.ragdolls_color);
        std::strncpy(view.custom_sprite, config.custom_sprite.c_str(), sizeof(view.custom_sprite) - 1);
        view.custom_sprite[sizeof(view.custom_sprite) - 1] = '\0';
        std::copy(std::begin(config.main), std::end(config.main), std::begin(view.esp_items.main_items));
        std::copy(std::begin(config.flags), std::end(config.flags), std::begin(view.esp_items.flags));
        std::copy(std::begin(config.weapon), std::end(config.weapon), std::begin(view.esp_items.weapon_items));
        std::copy(std::begin(config.aimbot), std::end(config.aimbot), std::begin(view.esp_items.aimbot_items));
        load_esp_colors(view.main_colors, config.main_colors);
        load_esp_colors(view.flag_colors, config.flag_colors);
        load_esp_colors(view.weapon_colors, config.weapon_colors);
        load_esp_colors(view.aimbot_colors, config.aimbot_colors);
    }
}

void store_shared_player_views(players_ui_state& ui) {
    for (int side_index = 0; side_index < player_visuals::side_count; ++side_index) {
        auto& config = player_visuals::settings(side_index);
        const auto& view = ui.sides[side_index];
        config.offscreen_arrow = view.offscreen_arrow;
        config.offscreen_color = to_config_color(view.offscreen_color.a);
        config.player_material = view.player_model;
        config.behind_walls_material = view.behind_walls_model;
        config.player_color = to_config_color(view.player_color.a);
        config.player_secondary = to_config_color(view.player_color.b);
        config.player_gradient = view.player_color.gradient;
        config.behind_walls_color = to_config_color(view.behind_walls_color.a);
        config.behind_walls_secondary = to_config_color(view.behind_walls_color.b);
        config.behind_walls_gradient = view.behind_walls_color.gradient;
        config.on_shot_material = view.on_shot_model;
        config.history_material = view.history_model;
        config.ragdoll_material = view.ragdolls_model;
        config.on_shot_color = to_config_color(view.on_shot_color.a);
        config.on_shot_secondary = to_config_color(view.on_shot_color.b);
        config.on_shot_gradient = view.on_shot_color.gradient;
        config.history_color = to_config_color(view.history_color.a);
        config.history_secondary = to_config_color(view.history_color.b);
        config.history_gradient = view.history_color.gradient;
        config.ragdoll_color = to_config_color(view.ragdolls_color.a);
        config.ragdoll_secondary = to_config_color(view.ragdolls_color.b);
        config.ragdoll_gradient = view.ragdolls_color.gradient;
        config.overlay_chams = view.overlay_chams;
        store_chams_selection(config.player_overlay, view.player_overlay, view.overlay_chams);
        store_chams_selection(config.behind_walls_overlay, view.behind_walls_overlay, view.overlay_chams);
        store_chams_selection(config.on_shot_overlay, view.on_shot_overlay, view.overlay_chams);
        store_chams_selection(config.history_overlay, view.history_overlay, view.overlay_chams);
        store_chams_selection(config.ragdoll_overlay, view.ragdoll_overlay, view.overlay_chams);
        config.custom_sprite = view.custom_sprite;
        std::copy(std::begin(view.esp_items.main_items), std::end(view.esp_items.main_items), std::begin(config.main));
        std::copy(std::begin(view.esp_items.flags), std::end(view.esp_items.flags), std::begin(config.flags));
        std::copy(std::begin(view.esp_items.weapon_items), std::end(view.esp_items.weapon_items), std::begin(config.weapon));
        std::copy(std::begin(view.esp_items.aimbot_items), std::end(view.esp_items.aimbot_items), std::begin(config.aimbot));
        store_esp_colors(config.main_colors, view.main_colors);
        store_esp_colors(config.flag_colors, view.flag_colors);
        store_esp_colors(config.weapon_colors, view.weapon_colors);
        store_esp_colors(config.aimbot_colors, view.aimbot_colors);
    }
}

void load_shared_self_view(player_side_ui_state& view) {
    const auto& config = player_visuals::self_settings();
    const auto effective_material = [](bool enabled, int material) {
        // Checkbox-era configs can contain enabled=false with a preserved
        // material. Off is now represented solely by Disabled in the view.
        return enabled ? std::clamp(material, 0, visuals_chams::mode_count - 1) : visuals_chams::disabled;
    };
    view.local_chams_model = effective_material(config.local_enabled, config.local_material);
    view.local_fake_chams_model = effective_material(config.local_fake_enabled, config.local_fake_material);
    view.viewmodel_chams_model = effective_material(config.viewmodel_enabled, config.viewmodel_material);
    view.weapon_chams_model = effective_material(config.weapon_enabled, config.weapon_material);
    view.attachments_chams_model = effective_material(config.attachments_enabled, config.attachments_material);
    view.player_color.a = to_vec4(config.local_color);
    view.player_color.b = to_vec4(config.local_secondary);
    view.local_fake_color.a = to_vec4(config.local_fake_color);
    view.local_fake_color.b = to_vec4(config.local_fake_secondary);
    view.behind_walls_color.a = to_vec4(config.viewmodel_color);
    view.behind_walls_color.b = to_vec4(config.viewmodel_secondary);
    view.weapon_color.a = to_vec4(config.weapon_color);
    view.weapon_color.b = to_vec4(config.weapon_secondary);
    view.on_shot_color.a = to_vec4(config.attachments_color);
    view.on_shot_color.b = to_vec4(config.attachments_secondary);
    view.glow = config.glow_enabled;
    view.glow_color.a = to_vec4(config.glow_color);
    view.glow_color.b = to_vec4(config.glow_secondary);
    view.glow_color.gradient = false;
    view.overlay_chams = config.overlay_chams;
    load_chams_selection(view.local_overlay, config.local_overlay, view.player_color);
    load_chams_selection(view.local_fake_overlay, config.local_fake_overlay, view.local_fake_color);
    load_chams_selection(view.viewmodel_overlay, config.viewmodel_overlay, view.behind_walls_color);
    load_chams_selection(view.weapon_overlay, config.weapon_overlay, view.weapon_color);
    load_chams_selection(view.attachments_overlay, config.attachments_overlay, view.on_shot_color);
    std::strncpy(view.custom_sprite, config.custom_sprite.c_str(), sizeof(view.custom_sprite) - 1);
    view.custom_sprite[sizeof(view.custom_sprite) - 1] = '\0';
    const auto& esp = player_visuals::self_esp_settings();
    std::copy(std::begin(esp.main), std::end(esp.main), std::begin(view.esp_items.main_items));
    std::copy(std::begin(esp.flags), std::end(esp.flags), std::begin(view.esp_items.flags));
    std::copy(std::begin(esp.weapon), std::end(esp.weapon), std::begin(view.esp_items.weapon_items));
    // Local ESP deliberately has no hitbox overlay; keep the Enemy/Teammate
    // setting independent and clear any value from the previous Self panel.
    view.esp_items.aimbot_items[player_visuals::aimbot_hitboxes] = false;
    load_esp_colors(view.main_colors, esp.main_colors);
    load_esp_colors(view.flag_colors, esp.flag_colors);
    load_esp_colors(view.weapon_colors, esp.weapon_colors);
    load_esp_colors(view.aimbot_colors, esp.aimbot_colors);
}

void store_shared_self_view(player_side_ui_state& view) {
    auto& config = player_visuals::self_settings();
    const auto clamp_material = [](int& material) {
        material = std::clamp(material, 0, visuals_chams::mode_count - 1);
    };
    clamp_material(view.local_chams_model);
    clamp_material(view.local_fake_chams_model);
    clamp_material(view.viewmodel_chams_model);
    clamp_material(view.weapon_chams_model);
    clamp_material(view.attachments_chams_model);
    config.local_material = view.local_chams_model;
    config.local_fake_material = view.local_fake_chams_model;
    config.viewmodel_material = view.viewmodel_chams_model;
    config.weapon_material = view.weapon_chams_model;
    config.attachments_material = view.attachments_chams_model;
    // Keep the previous config schema coherent, but do not use a separate
    // checkbox in the UI: a non-Disabled material is the enabled state.
    config.local_enabled = config.local_material != visuals_chams::disabled;
    config.local_fake_enabled = config.local_fake_material != visuals_chams::disabled;
    config.viewmodel_enabled = config.viewmodel_material != visuals_chams::disabled;
    config.weapon_enabled = config.weapon_material != visuals_chams::disabled;
    config.attachments_enabled = config.attachments_material != visuals_chams::disabled;
    config.local_color = to_config_color(view.player_color.a);
    config.local_secondary = to_config_color(view.player_color.b);
    config.local_fake_color = to_config_color(view.local_fake_color.a);
    config.local_fake_secondary = to_config_color(view.local_fake_color.b);
    config.viewmodel_color = to_config_color(view.behind_walls_color.a);
    config.viewmodel_secondary = to_config_color(view.behind_walls_color.b);
    config.weapon_color = to_config_color(view.weapon_color.a);
    config.weapon_secondary = to_config_color(view.weapon_color.b);
    config.attachments_color = to_config_color(view.on_shot_color.a);
    config.attachments_secondary = to_config_color(view.on_shot_color.b);
    config.glow_enabled = view.glow;
    config.glow_color = to_config_color(view.glow_color.a);
    config.glow_secondary = to_config_color(view.glow_color.b);
    config.overlay_chams = view.overlay_chams;
    store_chams_selection(config.local_overlay, view.local_overlay, view.overlay_chams);
    store_chams_selection(config.local_fake_overlay, view.local_fake_overlay, view.overlay_chams);
    store_chams_selection(config.viewmodel_overlay, view.viewmodel_overlay, view.overlay_chams);
    store_chams_selection(config.weapon_overlay, view.weapon_overlay, view.overlay_chams);
    store_chams_selection(config.attachments_overlay, view.attachments_overlay, view.overlay_chams);
    config.custom_sprite = view.custom_sprite;
    auto& esp = player_visuals::self_esp_settings();
    std::copy(std::begin(view.esp_items.main_items), std::end(view.esp_items.main_items), std::begin(esp.main));
    std::copy(std::begin(view.esp_items.flags), std::end(view.esp_items.flags), std::begin(esp.flags));
    std::copy(std::begin(view.esp_items.weapon_items), std::end(view.esp_items.weapon_items), std::begin(esp.weapon));
    esp.aimbot[player_visuals::aimbot_hitboxes] = false;
    store_esp_colors(esp.main_colors, view.main_colors);
    store_esp_colors(esp.flag_colors, view.flag_colors);
    store_esp_colors(esp.weapon_colors, view.weapon_colors);
    store_esp_colors(esp.aimbot_colors, view.aimbot_colors);
}

void draw_custom_sprite_row(player_side_ui_state& side) {
    const bool overlay_sprite = side.overlay_chams &&
        (side.player_overlay.values[visuals_chams::custom_sprite] ||
         side.behind_walls_overlay.values[visuals_chams::custom_sprite] ||
         side.on_shot_overlay.values[visuals_chams::custom_sprite] ||
         side.history_overlay.values[visuals_chams::custom_sprite] ||
         side.ragdoll_overlay.values[visuals_chams::custom_sprite] ||
         side.local_overlay.values[visuals_chams::custom_sprite] ||
         side.local_fake_overlay.values[visuals_chams::custom_sprite] ||
         side.viewmodel_overlay.values[visuals_chams::custom_sprite] ||
         side.weapon_overlay.values[visuals_chams::custom_sprite] ||
         side.attachments_overlay.values[visuals_chams::custom_sprite]);
    if (!overlay_sprite && side.player_model != visuals_chams::custom_sprite &&
        side.behind_walls_model != visuals_chams::custom_sprite &&
        side.on_shot_model != visuals_chams::custom_sprite &&
        side.history_model != visuals_chams::custom_sprite &&
        side.ragdolls_model != visuals_chams::custom_sprite &&
        side.local_chams_model != visuals_chams::custom_sprite &&
        side.local_fake_chams_model != visuals_chams::custom_sprite &&
        side.viewmodel_chams_model != visuals_chams::custom_sprite &&
        side.weapon_chams_model != visuals_chams::custom_sprite &&
        side.attachments_chams_model != visuals_chams::custom_sprite) {
        return;
    }
    if (gui.row_popup("Custom Sprite")) {
        gui.row_begin();
        ImGui::InputText("Texture", side.custom_sprite, IM_ARRAYSIZE(side.custom_sprite));
        gui.popup_end();
    }
}

// Kept with the UI settings, but deliberately unreachable: the Visuals parent only expands to
// Players/World and has no content page of its own.
[[maybe_unused]] void draw_visuals_common() {
    auto& effects = g_cfg->visuals.m_effects;
    auto& indicators = g_cfg->visuals.m_indicators;

    gui.begin_grid(2);
    gui.grid_col(0);
    gui.grid_box("EFFECTS", k_auto_height);
    gui.row_checkbox("Hit Marker", &effects.m_hitmarker);
    gui.row_checkbox("Hit Marker Sound", &effects.m_hitmarker_sound);
    gui.row_checkbox("Bullet Tracers", &effects.m_bullet_tracers);
    gui.row_checkbox("Bullet Impacts", &effects.m_bullet_impacts);
    gui.end_group_box();

    gui.grid_col(1);
    gui.grid_box("INDICATORS", k_auto_height);
    gui.row_checkbox("Crosshair", &indicators.m_crosshair);
    gui.row_checkbox("Spread Circle", &indicators.m_spread_circle);
    gui.row_checkbox("AA Lines", &indicators.m_aa_lines);
    gui.row_checkbox("Desync", &indicators.m_desync);
    gui.row_checkbox("Manual AA", &indicators.m_manual_aa);
    gui.end_group_box();

    // Keep the original ESP switches reachable from the common page; the Players
    // page above is reserved for the reference Enemy / Enemy Model layout.
    gui.grid_col(1);
    gui.grid_box("PLAYER ESP", k_auto_height);
    gui.row_checkbox("Teammates", &g_cfg->visuals.m_player_esp.m_teammates);
    gui.row_checkbox("Bounding Box", &g_cfg->visuals.m_player_esp.m_bounding_box);
    gui.row_checkbox("Name", &g_cfg->visuals.m_player_esp.m_name);
    gui.row_checkbox("Health", &g_cfg->visuals.m_player_esp.m_health_bar);
    gui.row_checkbox("Weapon", &g_cfg->visuals.m_player_esp.m_weapon);
    gui.row_checkbox("Skeleton", &g_cfg->visuals.m_player_esp.m_skeleton);
    gui.end_group_box();
    gui.end_grid();
}

void draw_esp_section_title(const char* title) {
    gui.row_begin();
    ImGui::TextColored(gui.text_disabled.to_vec4(1.f, false), "%s", gui.tr(title));
}

void draw_esp_settings(player_side_ui_state& side, bool self, float height) {
    static const char* main_items[player_visuals::main_count] = {
        "Box", "Name", "Distance", "HP Bar", "Ammo Bar", "Skeleton"
    };
    static const char* flags[player_visuals::flag_count] = {
        "Defuser", "Blind", "Scoped", "Reload", "Immunity", "Slowed",
        "Bomb", "Hostage", "Defuse", "Pin Pulled", "Money"
    };
    static const char* weapon_items[player_visuals::weapon_count] = {
        "Text", "Icon", "Readiness Bar", "Bomb", "Defuser", "Taser"
    };
    static const char* aimbot_items[player_visuals::aimbot_count] = { "Hitboxes" };

    // This replaces the old agent/ESP preview with real controls. The list is
    // scrollable so every existing ESP item remains reachable at any menu scale.
    gui.grid_box("PLAYER ESP", height, true);
    draw_esp_section_title("MAIN");
    for (int i = 0; i < player_visuals::main_count; ++i)
        gui.row_checkbox(main_items[i], &side.esp_items.main_items[i], &side.main_colors[i]);

    gui.row_multibox("Flag Selection", flags, side.esp_items.flags, side.flag_colors,
        player_visuals::flag_count, "Select flags; each list entry has its own color picker.");

    gui.row_multibox("Weapon Selection", weapon_items, side.esp_items.weapon_items, side.weapon_colors,
        player_visuals::weapon_count, "Select weapon items; each list entry has its own color picker.");

    if (!self) {
        for (int i = 0; i < player_visuals::aimbot_count; ++i)
            gui.row_checkbox(aimbot_items[i], &side.esp_items.aimbot_items[i], &side.aimbot_colors[i]);
    }
    gui.end_group_box();
}

void draw_overlay_models(player_side_ui_state& side, bool self) {
    gui.grid_box("OVERLAY MODELS", k_auto_height);
    draw_overlay_checkbox(side, self);
    gui.end_group_box();
}

void draw_enemy_side(player_side_ui_state& side) {
    auto& effects = g_cfg->visuals.m_effects;
    auto& glow = g_cfg->visuals.m_glow;

    gui.grid_box("ENEMY", k_auto_height);
    gui.row_checkbox("Enabled", &player_visuals::settings(0).esp_enabled);
    gui.row_checkbox("Offscreen Arrow", &side.offscreen_arrow, &side.offscreen_color);
    gui.row_checkbox("Sounds", &side.sounds, &side.sounds_color);
    gui.end_group_box();

    gui.grid_box("ENEMY MODEL", k_auto_height);
    draw_chams_row("Player", &side.player_model, side.player_overlay, &side.player_color,
        side.overlay_chams, visuals_chams::mode_names);
    draw_chams_row("Behind Walls", &side.behind_walls_model, side.behind_walls_overlay,
        &side.behind_walls_color, side.overlay_chams, visuals_chams::mode_names);
    draw_custom_sprite_row(side);
    draw_chams_row("On Shot", &side.on_shot_model, side.on_shot_overlay, &side.on_shot_color,
        side.overlay_chams, visuals_chams::mode_names);
    draw_chams_row("History", &side.history_model, side.history_overlay, &side.history_color,
        side.overlay_chams, visuals_chams::mode_names);
    draw_chams_row("Ragdolls", &side.ragdolls_model, side.ragdoll_overlay, &side.ragdolls_color,
        side.overlay_chams, visuals_chams::mode_names);
    gui.row_checkbox("Kill Effect", &effects.m_kill_effect, &side.kill_effect_color);
    gui.row_checkbox("Glow", &glow.m_enabled, &side.glow_color);
    gui.end_group_box();
}

void draw_teammate_side(player_side_ui_state& side) {
    gui.grid_box("TEAMMATE", k_auto_height);
    gui.row_checkbox("Enabled", &player_visuals::settings(1).esp_enabled);
    gui.end_group_box();

    gui.grid_box("TEAMMATE MODEL", k_auto_height);
    draw_chams_row("Player", &side.player_model, side.player_overlay, &side.player_color,
        side.overlay_chams, visuals_chams::mode_names);
    draw_chams_row("Behind Walls", &side.behind_walls_model, side.behind_walls_overlay,
        &side.behind_walls_color, side.overlay_chams, visuals_chams::mode_names);
    draw_custom_sprite_row(side);
    gui.row_checkbox("Disable Model Rendering", &side.disable_model_rendering);
    gui.row_checkbox("Glow", &side.glow, &side.glow_color);
    gui.end_group_box();
}

const char* const* self_chams_mode_names() {
    static const char* names[] = {
        "Off",
        visuals_chams::mode_names[visuals_chams::textured],
        visuals_chams::mode_names[visuals_chams::normal],
        visuals_chams::mode_names[visuals_chams::shaded],
        visuals_chams::mode_names[visuals_chams::flat],
        visuals_chams::mode_names[visuals_chams::metallic],
        visuals_chams::mode_names[visuals_chams::glow],
        visuals_chams::mode_names[visuals_chams::rim_glow],
        visuals_chams::mode_names[visuals_chams::bubble],
        visuals_chams::mode_names[visuals_chams::fade_up],
        visuals_chams::mode_names[visuals_chams::contour],
        visuals_chams::mode_names[visuals_chams::flow],
        visuals_chams::mode_names[visuals_chams::acrylic],
        visuals_chams::mode_names[visuals_chams::legacy],
        visuals_chams::mode_names[visuals_chams::custom_sprite]
    };
    static_assert(IM_ARRAYSIZE(names) == visuals_chams::mode_count, "Self chams mode list mismatch");
    return names;
}

void draw_self_side(player_side_ui_state& side) {
    static const char* local_models[] = { "Default", "Counter-Terrorist", "Terrorist" };
    static const char* local_weapons[] = { "Default", "Current Weapon" };

    gui.grid_box("LOCAL PLAYER MODEL", k_auto_height);
    gui.row_checkbox("ESP Enabled", &player_visuals::self_esp_settings().esp_enabled);
    if (gui.row_popup("Model")) {
        gui.row_begin(); ImGui::Combo("Mode", &side.local_model, local_models, IM_ARRAYSIZE(local_models));
        gui.popup_end();
    }
    if (gui.row_popup("Weapon")) {
        gui.row_begin(); ImGui::Combo("Mode", &side.local_weapon, local_weapons, IM_ARRAYSIZE(local_weapons));
        gui.popup_end();
    }
    if (gui.row_popup("Transparency")) {
        gui.row_begin(); ImGui::SliderInt("Opacity", &side.local_transparency, 0, 100, "%d%%");
        gui.popup_end();
    }
    gui.end_group_box();

    gui.grid_box("LOCAL CHAMS", k_auto_height);
    draw_chams_row("Local Player", &side.local_chams_model, side.local_overlay, &side.player_color,
        side.overlay_chams, self_chams_mode_names());
    // Fake remains a UI/config placeholder; no fake model record is rendered.
    draw_chams_row("Local Player Fake", &side.local_fake_chams_model, side.local_fake_overlay,
        &side.local_fake_color, side.overlay_chams, self_chams_mode_names());
    draw_chams_row("Viewmodel", &side.viewmodel_chams_model, side.viewmodel_overlay,
        &side.behind_walls_color, side.overlay_chams, self_chams_mode_names());
    draw_chams_row("Weapon", &side.weapon_chams_model, side.weapon_overlay, &side.weapon_color,
        side.overlay_chams, self_chams_mode_names());
    draw_chams_row("Attachments", &side.attachments_chams_model, side.attachments_overlay,
        &side.on_shot_color, side.overlay_chams, self_chams_mode_names());
    draw_custom_sprite_row(side);
    gui.row_checkbox("Glow", &side.glow, &side.glow_color);
    gui.end_group_box();
}

void draw_player_side_tabs(players_ui_state& ui, ImVec2 pos) {
    static const char* labels[] = { "Enemies", "Teammates", "Self" };
    static float hover[3] = {};
    static float selected[3] = {};

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 saved_cursor = window->DC.CursorPos;
    const ImVec2 saved_cursor_max = window->DC.CursorMaxPos;
    const float s = gui.m_scale;
    const float ease = ImClamp(ImGui::GetIO().DeltaTime * 15.f, 0.f, 1.f);
    ImDrawList* draw = window->DrawList;

    ImGui::PushID("##players_top_side_tabs");
    for (int i = 0; i < 3; ++i) {
        const char* label = gui.tr(labels[i]);
        const ImVec2 text_size = ImGui::CalcTextSize(label);
        const ImVec2 size(text_size.x + 26.f * s, 30.f * s);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(pos);
        if (ImGui::InvisibleButton("##side", size))
            ui.active_side = i;
        hover[i] = ImLerp(hover[i], ImGui::IsItemHovered() ? 1.f : 0.f, ease);
        selected[i] = ImLerp(selected[i], ui.active_side == i ? 1.f : 0.f, ease);
        const float plate = ImMax(hover[i] * 0.5f, selected[i]);
        if (plate > 0.01f) {
            draw->AddRectFilled(pos, pos + size,
                gui.frame_inactive.to_im_color(0.35f + 0.55f * plate), 6.f * s);
        }
        ImVec4 ink = ImLerp(gui.text_disabled.to_vec4(1.f, false),
                            gui.text.to_vec4(1.f, false), hover[i]);
        ink = ImLerp(ink, gui.accent_color.to_vec4(1.f, false), selected[i]);
        draw->AddText(ImVec2(IM_ROUND(pos.x + (size.x - text_size.x) * 0.5f),
            IM_ROUND(pos.y + (size.y - text_size.y) * 0.5f)),
            ImGui::GetColorU32(ink), label);
        ImGui::PopID();
        pos.x += size.x + 4.f * s;
    }
    ImGui::PopID();
    window->DC.CursorPos = saved_cursor;
    window->DC.CursorMaxPos = saved_cursor_max;
}

void draw_visuals_players() {
    auto& ui = player_ui();
    load_shared_player_views(ui);
    load_shared_self_view(ui.sides[2]);
    ui.active_side = ImClamp(ui.active_side, 0, 2);

    gui.begin_grid(2);
    gui.grid_col(0);
    if (ui.active_side == 0)
        draw_enemy_side(ui.sides[0]);
    else if (ui.active_side == 1)
        draw_teammate_side(ui.sides[1]);
    else
        draw_self_side(ui.sides[2]);

    // Match the right column's final edge to the left chams stack. PLAYER ESP
    // yields exactly the space needed by the separate Overlay Models panel and
    // remains scrollable when its content is taller than the available height.
    const float left_remaining = gui.grid_remaining();
    gui.grid_col(1);
    const float right_remaining = gui.grid_remaining();
    const float left_stack_height = right_remaining - left_remaining;
    const float measured_overlay = gui.measured_box_height("OVERLAY MODELS");
    // grid_box() uses the same 80px initial height before an auto box is measured.
    const float overlay_height = measured_overlay > 0.f ? measured_overlay : 80.f;
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float player_esp_height = std::max(10.f,
        (left_stack_height - overlay_height * gui.m_scale - 2.f * spacing) / gui.m_scale);
    auto& active_side = ui.sides[ui.active_side];
    const bool self = ui.active_side == 2;
    draw_esp_settings(active_side, self, player_esp_height);
    draw_overlay_models(active_side, self);
    gui.end_grid();
    store_shared_player_views(ui);
    store_shared_self_view(ui.sides[2]);

    // Keep the legacy config fields coherent for older config consumers; the
    // active renderer reads only the independent shared per-side settings.
    auto& legacy = g_cfg->visuals.m_player_esp;
    const auto& enemy = player_visuals::settings(0);
    legacy.m_enabled = enemy.esp_enabled;
    legacy.m_teammates = player_visuals::settings(1).esp_enabled;
    legacy.m_bounding_box = enemy.main[player_visuals::main_bounding_box];
    legacy.m_name = enemy.main[player_visuals::main_name];
    legacy.m_distance = enemy.main[player_visuals::main_distance];
    legacy.m_health_bar = enemy.main[player_visuals::main_health_bar];
    legacy.m_ammo_bar = enemy.main[player_visuals::main_ammo_bar];
    legacy.m_skeleton = enemy.main[player_visuals::main_skeleton];
    legacy.m_weapon = enemy.weapon[player_visuals::weapon_text];
}

void draw_visuals_world() {
    auto& world = player_visuals::world_settings();
    static color_value box_color = make_ui_color(0.92f, 0.94f, 1.f);
    static color_value name_color = make_ui_color(0.92f, 0.94f, 1.f);
    static color_value icon_color = make_ui_color(0.92f, 0.94f, 1.f);
    static color_value ammo_color = make_ui_color(0.55f, 0.72f, 1.f);
    static color_value smoke_color = make_ui_color(0.65f, 0.65f, 0.65f);
    static color_value molotov_color = make_ui_color(1.f, 0.35f, 0.12f);
    box_color.a = to_vec4(world.box_color);
    name_color.a = to_vec4(world.name_color);
    icon_color.a = to_vec4(world.icon_color);
    ammo_color.a = to_vec4(world.ammo_color);
    smoke_color.a = to_vec4(world.smoke_color);
    molotov_color.a = to_vec4(world.molotov_color);

    gui.begin_grid(2);
    gui.grid_col(0);
    gui.grid_box("WORLD", k_auto_height);
    gui.row_begin(); ImGui::SliderInt("Exposure", &g_cfg->world.m_exposure, 0, 100);
    gui.row_begin(); ImGui::SliderFloat("Brightness", &g_cfg->world.m_brightness, 0.f, 1.f, "%.2f");
    gui.row_checkbox("Remove Fog", &g_cfg->world.m_render_fog);
    gui.end_group_box();

    gui.grid_box("WEAPON ESP", k_auto_height);
    gui.row_checkbox("Enabled", &world.enabled);
    gui.row_checkbox("Box", &world.box, &box_color);
    gui.row_checkbox("Name", &world.name, &name_color);
    gui.row_checkbox("Icon", &world.icon, &icon_color);
    gui.row_checkbox("Ammo Bar", &world.ammo_bar, &ammo_color);
    gui.row_checkbox("Smoke Range", &world.smoke_range, &smoke_color);
    gui.row_checkbox("Molotov Range", &world.molotov_range, &molotov_color);
    gui.end_group_box();

    gui.grid_col(1);
    gui.grid_box("THIRDPERSON", k_auto_height);
    gui.row_checkbox("Enabled", &g_cfg->world_esp.m_enable_thirdperson);
    gui.row_begin(); ImGui::SliderInt("Distance", &g_cfg->world_esp.m_distance, 50, 300);
    gui.end_group_box();
    gui.end_grid();

    world.box_color = to_config_color(box_color.a);
    world.name_color = to_config_color(name_color.a);
    world.icon_color = to_config_color(icon_color.a);
    world.ammo_color = to_config_color(ammo_color.a);
    world.smoke_color = to_config_color(smoke_color.a);
    world.molotov_color = to_config_color(molotov_color.a);
}
} // namespace

namespace visuals_ui {
void draw_top_bar(ImVec2 button_pos) {
    auto& ui = player_ui();
    ui.active_side = ImClamp(ui.active_side, 0, 2);
    draw_player_side_tabs(ui, button_pos);
}
} // namespace visuals_ui

void c_menu::draw_visuals() {
    gui.m_visuals_subtab = ImClamp(gui.m_visuals_subtab, 1, 2);
    if (gui.m_visuals_subtab == 2)
        draw_visuals_world();
    else
        draw_visuals_players();
}
