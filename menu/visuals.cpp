// Visuals menu page only. Gameplay ESP/rendering code belongs under features/visuals.
#include "menu.hpp"
#include "hashes.hpp"
#include "../features/visuals/chams/chams.hpp"
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
    // Self > Transparency. The multibox picks when the local model fades at all; the two sliders
    // are the fade amount for the standing view and for the second (x2) zoom, where 0 means the
    // second scope keeps the normal look - the slider's own "Off" position.
    bool transparency_conditions[2] = {};
    float transparency_main = 0.f;
    float transparency_second_scope = 0.f;
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
        view.sounds = config.sounds;
        view.sounds_color.a = to_vec4(config.sounds_color);
        view.sounds_color.b = view.sounds_color.a;
        view.sounds_color.gradient = false;
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
        config.sounds = view.sounds;
        config.sounds_color = to_config_color(view.sounds_color.a);
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
    view.disable_model_rendering = config.disable_model_rendering;
    view.transparency_conditions[0] = config.transparency_conditions[0];
    view.transparency_conditions[1] = config.transparency_conditions[1];
    view.transparency_main = std::clamp(config.transparency, 0.f, 1.f);
    view.transparency_second_scope = std::clamp(config.transparency_second_scope, 0.f, 1.f);
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
    config.disable_model_rendering = view.disable_model_rendering;
    config.transparency_conditions[0] = view.transparency_conditions[0];
    config.transparency_conditions[1] = view.transparency_conditions[1];
    config.transparency = std::clamp(view.transparency_main, 0.f, 1.f);
    config.transparency_second_scope = std::clamp(view.transparency_second_scope, 0.f, 1.f);
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
    gui.row_checkbox("Sounds", &side.sounds, &side.sounds_color,
        "Show a fading world ping when an enemy emits a footstep, landing, or weapon sound.");
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
    gui.grid_box("LOCAL PLAYER MODEL", k_auto_height);
    gui.row_checkbox("ESP Enabled", &player_visuals::self_esp_settings().esp_enabled);
    gui.end_group_box();

    // The model switches get their own panel right under ESP Enabled. It costs exactly what the
    // skin-owned Weapon row used to (its header + the row it moved down), so the column keeps the
    // same height it had before.
    gui.grid_box("MODELS", k_auto_height);
    gui.row_checkbox("Disable Model Rendering", &side.disable_model_rendering);
    if (gui.row_popup("Transparency", nullptr, 280.f)) {
        static const char* transparency_items[] = { "In Scope", "Grenade" };
        gui.popup_section_begin("##self_transparency");
        gui.row_multibox("Conditions", transparency_items, side.transparency_conditions,
                         IM_ARRAYSIZE(transparency_items));
        gui.row_begin(); ImGui::SliderFloat("Transparency", &side.transparency_main, 0.f, 1.f, "%.2f");
        // The x2 fade only applies while scoped, so it has nothing to control until the In Scope
        // condition is on - until then the row is not drawn at all.
        if (side.transparency_conditions[0]) {
            gui.row_begin(); ImGui::SliderFloat("Second Scope", &side.transparency_second_scope, 0.f, 1.f,
                side.transparency_second_scope <= 0.f ? "Off" : "%.2f");
        }
        gui.popup_section_end();
        gui.popup_end();
    }
    gui.end_group_box();

    gui.grid_box("LOCAL CHAMS", k_auto_height);
    draw_chams_row("Local Player", &side.local_chams_model, side.local_overlay, &side.player_color,
        side.overlay_chams, self_chams_mode_names());
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
    static color_value inaccuracy_color = make_ui_color(0.92f, 0.92f, 0.92f);
    static color_value scope_main_color = make_ui_color(1.f, 1.f, 1.f);
    static color_value scope_edge_color = make_ui_color(1.f, 1.f, 1.f);
    static color_value bomb_color = make_ui_color(0.45f, 0.95f, 0.52f);
    static color_value bomb_defused_color = make_ui_color(0.70f, 0.82f, 0.94f);
    static color_value weapon_color = make_ui_color(0.92f, 0.94f, 1.f);
    static color_value hostage_color = make_ui_color(1.f, 0.35f, 0.56f);
    static color_value rescue_color = make_ui_color(0.60f, 0.78f, 0.95f);
    static color_value hostage_progress_bg_color = make_ui_color(0.f, 0.f, 0.f);
    static color_value grenade_color = make_ui_color(0.92f, 0.94f, 1.f);
    static color_value smoke_radius_color = make_ui_color(0.35f, 0.62f, 0.95f);
    static color_value inferno_radius_color = make_ui_color(0.95f, 0.42f, 0.27f);
    static color_value projectile_inferno_color = make_ui_color(1.f, 0.76f, 0.50f);
    static color_value smoke_color = make_ui_color(0.55f, 0.61f, 0.42f);
    static color_value molotov_color = make_ui_color(0.95f, 0.35f, 0.27f);
    static color_value explosion_color = make_ui_color(0.48f, 0.63f, 1.f);
    static color_value effects_color = make_ui_color(1.f, 0.30f, 0.67f);
    static color_value trajectory_color = make_ui_color(1.f, 0.25f, 0.42f);
    static color_value windows_color = make_ui_color(0.35f, 0.95f, 0.60f);
    static color_value event_log_hit_color = make_ui_color(0.62f, 0.79f, 0.17f);
    static color_value event_log_miss_color = make_ui_color(1.f, 0.31f, 0.31f);
    static color_value event_log_evaded_color = make_ui_color(1.f, 0.78f, 0.25f);
    static color_value penetration_color = make_ui_color(0.75f, 0.45f, 0.95f);
    static color_value hit2d_color = make_ui_color(1.f, 1.f, 1.f);
    static color_value hit3d_color = make_ui_color(1.f, 1.f, 1.f);
    static color_value damage_color = make_ui_color(1.f, 1.f, 1.f);
    static color_value tracer_enemy_color = make_ui_color(1.f, 0.35f, 0.45f);
    static color_value tracer_teammate_color = make_ui_color(1.f, 0.72f, 0.48f);
    static color_value tracer_local_color = make_ui_color(0.60f, 0.55f, 0.95f);
    static color_value impact_color = make_ui_color(0.90f, 0.60f, 0.90f);
    static color_value spark_color = make_ui_color(1.f, 1.f, 1.f);
    inaccuracy_color.a = to_vec4(world.inaccuracy_color);
    scope_main_color.a = to_vec4(world.scope_color_main);
    scope_edge_color.a = to_vec4(world.scope_color_edge);
    bomb_color.a = to_vec4(world.bomb.color);
    bomb_defused_color.a = to_vec4(world.bomb.secondary_color);
    weapon_color.a = to_vec4(world.weapon.color);
    hostage_color.a = to_vec4(world.hostage.color);
    rescue_color.a = to_vec4(world.hostage.secondary_color);
    hostage_progress_bg_color.a = to_vec4(world.hostage.progress_background);
    grenade_color.a = to_vec4(world.grenade.color);
    trajectory_color.a = to_vec4(world.trajectory_color);
    smoke_radius_color.a = to_vec4(world.grenade.secondary_color);
    inferno_radius_color.a = to_vec4(world.grenade.progress_background);
    projectile_inferno_color.a = to_vec4(world.grenade.glow_color);
    smoke_color.a = to_vec4(world.smoke_color);
    molotov_color.a = to_vec4(world.molotov_color);
    explosion_color.a = to_vec4(world.explosion_color);
    effects_color.a = to_vec4(world.effects_color);
    windows_color.a = to_vec4(world.windows_color);
    event_log_hit_color.a = to_vec4(world.event_log_hit_color);
    event_log_miss_color.a = to_vec4(world.event_log_miss_color);
    event_log_evaded_color.a = to_vec4(world.event_log_evaded_color);
    penetration_color.a = to_vec4(world.penetration_color);
    hit2d_color.a = to_vec4(world.hitmarker_2d_color);
    hit3d_color.a = to_vec4(world.hitmarker_3d_color);
    damage_color.a = to_vec4(world.damage_marker_color);
    tracer_enemy_color.a = to_vec4(world.tracer_enemy_color);
    tracer_teammate_color.a = to_vec4(world.tracer_teammate_color);
    tracer_local_color.a = to_vec4(world.tracer_local_color);
    impact_color.a = to_vec4(world.bullet_impact_color);
    spark_color.a = to_vec4(world.impact_sparks_color);

    static const char* unlock_modes[] = { "Select", "Roaming", "All Observer Modes" };
    static const char* radar_items[] = { "Reveal Enemies", "Reveal Hostages", "Reveal Bomb" };
    static const char* event_log_items[] = { "Miss", "Hit", "Evaded" };
    static const char* scope_modes[] = { "Default", "Lines", "Custom" };
    static const char* scope_styles[] = { "Default", "Rotated" };
    static const char* toggle_modes[] = { "Disabled", "Enabled" };
    static const char* aspect_modes[] = { "Horizontal", "Vertical" };
    static const char* window_item_names[] = { "Time", "Username", "Ping", "FPS" };
    static const char* window_positions[] = { "Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right" };
    static const char* density_modes[] = { "Default", "Compact", "Expanded" };
    static const char* hotkey_style_modes[] = { "Default", "Minimal", "Detailed" };
    static const char* notice_modes[] = { "Off", "All", "Kills Only" };
    static const char* equipment_modes[] = { "Off", "Default", "All" };
    static const char* shimmer_modes[] = { "Select", "None", "Pulse", "Shimmer" };
    static const char* ragdoll_modes[] = { "Default", "Remove", "Client Side" };
    static const char* equipment_names[] = { "Primary", "Secondary", "Knife", "Taser", "Grenades", "Bomb", "Defuse Kit", "Armor", "Other", "Extra" };

    const auto open_item_visuals = [&](player_visuals::world_item_settings& item, const char* id, bool hostage_options) {
        gui.row_begin();
        if (gui.row_options_begin(id, 260.f, c_gui::row_control::toggle)) {
            gui.popup_section_begin("##item_visuals");
            gui.row_checkbox("Bounding Box", &item.box);
            if (hostage_options) {
                gui.row_begin();
                if (gui.row_options_begin("##hostage_progress_options", 260.f, c_gui::row_control::toggle)) {
                    gui.popup_section_begin("##hostage_progress_style");
                    gui.row_color("Background", &hostage_progress_bg_color);
                    gui.popup_section_end();
                    gui.row_options_end();
                }
            }
            gui.row_checkbox("Progress Bar", &item.progress_bar);
            gui.row_checkbox("Text", &item.text);
            gui.row_checkbox("Distance", &item.distance);
            gui.row_checkbox("Glow", &item.glow);
            gui.popup_section_end();
            gui.row_options_end();
        }
    };

    gui.begin_grid(2);
    gui.grid_col(0);
    gui.column_title("VIEW");
    gui.grid_box("##world_view", k_auto_height);
    if (gui.row_popup("View Options", nullptr, 280.f)) {
        gui.popup_section_begin("##view_fov");
        gui.row_checkbox("Override FOV", &g_cfg->world_esp.m_enable_override_fov);
        gui.row_begin(); ImGui::SliderInt("Value", &g_cfg->world_esp.m_override_fov, 60, 130, "%d°");
        gui.popup_section_end();
        gui.popup_section_begin("##view_aspect");
        gui.row_checkbox("Override Aspect Ratio", &world.aspect_ratio_override);
        gui.row_begin(); ImGui::Combo("Scaling", &world.aspect_scaling, aspect_modes, IM_ARRAYSIZE(aspect_modes));
        gui.row_begin(); ImGui::SliderFloat("Value", &g_cfg->world_esp.m_aspect_ratio, 0.f, 2.5f, "%.2f");
        gui.popup_section_end();
        gui.row_checkbox("Disable Landing Bob", &g_cfg->world.m_remove_landing_bob);
        gui.popup_end();
    }
    if (gui.row_popup("Scope Options", nullptr, 280.f)) {
        gui.popup_section_begin("##scope_fov");
        gui.row_checkbox("Override Scope", &world.scope_fov_override);
        // Both sliders are the actual projected FOV, so the second scope has its own
        // value instead of being derived from whatever setup->fov the game passed in.
        gui.row_begin(); ImGui::SliderInt("FOV (x1)", &world.scope_fov_x1, 1, 90, "%d");
        gui.row_begin(); ImGui::SliderInt("FOV (x2)", &world.scope_fov_x2, 1, 90, "%d");
        gui.popup_section_end();
        gui.popup_section_begin("##scope_misc");
        gui.row_checkbox("Instant Scope", &world.instant_scope);
        gui.row_checkbox("Show Viewmodel", &g_cfg->world.m_show_viewmodel_in_scope);
        gui.row_checkbox("Force Viewmodel FOV", &world.scope_force_viewmodel_fov);
        gui.popup_section_end();
        gui.popup_end();
    }
    if (gui.row_popup("Viewmodel Options", "Configure viewmodel FOV, position, angles, and movement modifiers.", 300.f)) {
        gui.popup_section_begin("##viewmodel_fov");
        gui.row_checkbox("Override FOV", &world.viewmodel_fov_override);
        gui.row_begin(); ImGui::SliderInt("Value", &world.viewmodel_fov_value, 54, 120, "%d");
        gui.popup_section_end();
        gui.popup_section_begin("##viewmodel_position");
        gui.row_checkbox("Override Position", &world.viewmodel_position_override);
        // Applied in CalcViewModelView, so the engine's +-2 cvar clamp is gone.
        gui.row_begin(); ImGui::SliderFloat("X", &g_cfg->world.m_viewmodel_offset_x, -20.f, 20.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Y", &g_cfg->world.m_viewmodel_offset_y, -20.f, 20.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Z", &g_cfg->world.m_viewmodel_offset_z, -20.f, 20.f, "%.2f");
        gui.popup_section_end();
        gui.popup_section_begin("##viewmodel_angles");
        gui.row_checkbox("Override Angles", &world.viewmodel_angles_override);
        gui.row_begin(); ImGui::SliderFloat("Pitch", &world.viewmodel_pitch, -45.f, 45.f, "%.1f°");
        gui.row_begin(); ImGui::SliderFloat("Yaw", &world.viewmodel_yaw, -45.f, 45.f, "%.1f°");
        gui.row_begin(); ImGui::SliderFloat("Roll", &world.viewmodel_roll, -45.f, 45.f, "%.1f°");
        gui.popup_section_end();
        gui.popup_section_begin("##viewmodel_modifiers");
        gui.row_checkbox("Override Modifiers", &world.viewmodel_modifiers_override);
        gui.row_begin(); ImGui::SliderFloat("Bobbing", &world.modifier_bobbing, 0.f, 1.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Rotation Sway", &world.modifier_rotation_sway, 0.f, 1.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Velocity Sway", &world.modifier_velocity_sway, 0.f, 1.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("In Air Offset", &world.modifier_in_air_offset, 0.f, 1.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Lower Weapon", &world.modifier_lower_weapon, 0.f, 1.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Ladder Bob", &world.modifier_ladder_bob, 0.f, 1.f, "%.2f");
        gui.popup_section_end();
        gui.popup_end();
    }
    if (gui.row_popup("Perspective Options", nullptr, 280.f)) {
        gui.popup_section_begin("##perspective");
        gui.row_checkbox("Force Third Person", &g_cfg->world_esp.m_enable_thirdperson);
        gui.row_begin(); ImGui::SliderInt("Distance", &g_cfg->world_esp.m_distance, 50, 300, "%d");
        gui.popup_section_end();
        // The two disablers are plain rows of the same card. They used to hang off a stray
        // "Disablers" caption glued to the same line, which pushed the checkbox label under it,
        // and one of them was drawn outside the outlined block it belongs to.
        gui.popup_section_begin("##perspective_disablers", false, 0.f);
        gui.row_checkbox("Close to Wall", &world.thirdperson_close_to_wall);
        gui.row_checkbox("Disable Overhead", &world.thirdperson_disable_overhead);
        gui.popup_section_end();
        gui.popup_end();
    }
    gui.row_begin(); ImGui::Combo("Unlock Spectating", &world.unlock_spectating_mode, unlock_modes, IM_ARRAYSIZE(unlock_modes));
    // Visual Recoil is an enable/disable combobox, not a checkbox.
    int recoil_toggle = g_cfg->world.m_visual_recoil ? 1 : 0;
    gui.row_begin(); ImGui::Combo("Visual Recoil", &recoil_toggle, toggle_modes, IM_ARRAYSIZE(toggle_modes));
    g_cfg->world.m_visual_recoil = recoil_toggle == 1;
    gui.end_group_box();

    gui.grid_box("WORLD ESP", k_auto_height);
    if (gui.row_popup("Bomb", nullptr, 280.f)) {
        gui.popup_section_begin("##bomb");
        open_item_visuals(world.bomb, "##bomb_visuals", false);
        gui.row_checkbox("Enabled", &world.bomb.enabled, &bomb_color);
        gui.row_color("Bomb Defused", &bomb_defused_color);
        gui.row_checkbox("Colorize Effects", &world.bomb.colorize_effects, &bomb_color);
        gui.row_checkbox("Bomb Indicator", &world.bomb.bomb_indicator, &bomb_color);
        gui.row_checkbox("Defuse Indicator", &world.bomb.defuse_indicator, &bomb_color);
        gui.row_checkbox("Defuser", &world.bomb.defuser, &bomb_color);
        gui.popup_section_end();
        gui.popup_end();
    }
    if (gui.row_popup("Hostages", nullptr, 280.f)) {
        gui.popup_section_begin("##hostages");
        open_item_visuals(world.hostage, "##hostages_visuals", true);
        gui.row_checkbox("Enabled", &world.hostage.enabled, &hostage_color);
        gui.row_checkbox("Rescue Indicator", &world.hostage.rescue_indicator, &rescue_color);
        gui.popup_section_end();
        gui.popup_end();
    }
    if (gui.row_popup("Grenades", nullptr, 280.f)) {
        gui.popup_section_begin("##grenades");
        gui.row_checkbox("Enabled", &world.grenade.enabled, &grenade_color);
        gui.row_checkbox("Smoke Radius", &world.grenade.smoke_radius, &smoke_radius_color);
        gui.row_checkbox("Inferno Radius", &world.grenade.inferno_radius, &inferno_radius_color);
        gui.row_checkbox("Projectile Inferno", &world.grenade.projectile_inferno, &projectile_inferno_color);
        gui.row_checkbox("Colorize Smoke", &world.grenade.colorize_smoke, &smoke_color);
        gui.row_checkbox("Colorize Molotov", &world.grenade.colorize_molotov, &molotov_color);
        gui.row_checkbox("Colorize Explosion", &world.grenade.colorize_explosion, &explosion_color);
        gui.row_checkbox("Effects", &world.grenade.colorize_effects, &effects_color);
        gui.popup_section_end();
        gui.popup_end();
    }
    gui.row_begin();
    if (gui.row_options_begin("##trajectory_options", 300.f, c_gui::row_control::toggle)) {
        gui.popup_section_begin("##trajectory_settings");
        gui.row_color("Color", &trajectory_color);
        gui.row_begin(); ImGui::SliderFloat("Exponent", &world.trajectory_exponent, 0.f, 2.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Thickness", &world.trajectory_thickness, 0.f, 5.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Points Thickness", &world.trajectory_points_thickness, 0.f, 5.f, "%.2f");
        gui.row_begin(); ImGui::Combo("Shimmer", &world.trajectory_shimmer, shimmer_modes, IM_ARRAYSIZE(shimmer_modes));
        gui.popup_section_end();
        gui.row_checkbox("Thrown Grenades", &world.trajectory_thrown_grenades);
        gui.row_checkbox("Pin Pulled Only", &world.trajectory_pin_pulled_only);
        gui.row_checkbox("Precise Trajectory", &world.trajectory_precise);
        gui.row_options_end();
    }
    gui.row_checkbox("Grenade Trajectory", &world.grenade_trajectory);
    // Proximity warning is its own row again: it fires for any nearby grenade and
    // does not depend on the trajectory being enabled.
    gui.row_checkbox("Proximity Warnings", &world.grenade_proximity_warnings);
    gui.end_group_box();

    gui.grid_col(1);
    gui.column_title("HUD");
    gui.grid_box("##world_hud", k_auto_height);
    gui.row_multibox("Radar", radar_items, world.radar_options.data(), IM_ARRAYSIZE(radar_items));
    gui.row_begin();
    // Custom scope settings hang off the row itself: a gear appears in the gap left
    // of the combo (same control every other row uses) only while the custom mode is
    // selected. The extra "Custom Scope" row with its arrow is gone.
    if (world.scope_overlay_mode == 2 &&
        gui.row_options_begin("##custom_scope_options", 280.f, c_gui::row_control::box)) {
        gui.popup_section_begin("##custom_scope");
        gui.row_begin(); ImGui::Combo("Style", &world.scope_style, scope_styles, IM_ARRAYSIZE(scope_styles));
        gui.row_begin(); ImGui::SliderFloat("Offset", &world.scope_offset, 0.f, 500.f, "%.0f px");
        gui.row_begin(); ImGui::SliderFloat("Position", &world.scope_position, 0.f, 500.f, "%.0f px");
        gui.row_begin(); ImGui::SliderFloat("Thickness", &world.scope_thickness, 1.f, 10.f, "%.1f px");
        gui.row_color("Main", &scope_main_color);
        gui.row_color("Edge", &scope_edge_color);
        gui.popup_section_end();
        gui.row_options_end();
    }
    ImGui::Combo("Scope Overlay", &world.scope_overlay_mode, scope_modes, IM_ARRAYSIZE(scope_modes));
    gui.row_checkbox("Inaccuracy Overlay", &g_cfg->world.m_inaccuracy_overlay, &inaccuracy_color);
    if (gui.row_popup("Death Notices", nullptr, 280.f)) {
        gui.popup_section_begin("##death_notices");
        gui.row_begin(); ImGui::Combo("Preserve Notices", &world.preserve_death_notices_mode, notice_modes, IM_ARRAYSIZE(notice_modes));
        gui.row_checkbox("Hide Teammate Assists", &world.hide_teammate_assists);
        gui.popup_section_end();
        gui.popup_end();
    }
    if (gui.row_popup("Scoreboard", nullptr, 280.f)) {
        gui.popup_section_begin("##scoreboard_options");
        gui.row_begin(); ImGui::SliderFloat("Opacity", &world.scoreboard_opacity, 0.f, 1.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Width", &world.scoreboard_width, 0.f, 1.f, "%.2f");
        gui.row_checkbox("Reveal Money", &world.scoreboard_reveal_money);
        gui.row_begin(); ImGui::Combo("Equipment", &world.scoreboard_equipment_mode, equipment_modes, IM_ARRAYSIZE(equipment_modes));
        gui.popup_section_end();
        gui.popup_section_begin("##scoreboard_equipment", false, 4.f);
        for (int i = 0; i < static_cast<int>(world.scoreboard_equipment.size()); ++i)
            gui.row_checkbox(equipment_names[i], &world.scoreboard_equipment[i]);
        gui.popup_section_end();
        gui.popup_end();
    }
    if (gui.row_popup("Crosshairs", nullptr, 280.f)) {
        gui.popup_section_begin("##crosshairs");
        gui.row_checkbox("Force Sniper Crosshair", &world.force_sniper_crosshair);
        gui.row_checkbox("Force While Scoped", &world.force_crosshair_scoped);
        gui.row_checkbox("Force While Holding C4", &world.force_crosshair_c4);
        gui.row_checkbox("Hide While Holding Knife", &world.hide_crosshair_knife);
        gui.row_checkbox("Hide While Holding Grenade", &world.hide_crosshair_grenade);
        gui.row_checkbox("Penetration", &world.penetration_crosshair, &penetration_color);
        gui.popup_section_end();
        gui.popup_end();
    }
    gui.end_group_box();

    gui.grid_box("MISCELLANEOUS", k_auto_height);
    if (gui.row_popup("Windows", "Configure the overlay windows and their styling.", 300.f)) {
        gui.popup_section_begin("##window_toggles");
        gui.row_begin();
        if (gui.row_options_begin("##watermark_options", 280.f, c_gui::row_control::toggle)) {
            gui.popup_section_begin("##watermark_items");
            gui.row_multibox("Items", window_item_names, world.watermark_items.data(), IM_ARRAYSIZE(window_item_names));
            gui.row_begin(); ImGui::Combo("Position", &world.watermark_position, window_positions, IM_ARRAYSIZE(window_positions));
            gui.row_begin(); ImGui::Combo("Density", &world.watermark_density, density_modes, IM_ARRAYSIZE(density_modes));
            gui.row_checkbox("Additional Colors", &world.watermark_additional_colors);
            gui.row_checkbox("Display Avatar", &world.watermark_display_avatar);
            gui.popup_section_end();
            gui.row_options_end();
        }
        gui.row_checkbox("Watermark", &g_cfg->misc.m_watermark);
        gui.row_begin();
        if (gui.row_options_begin("##hotkeys_options", 280.f, c_gui::row_control::toggle)) {
            gui.popup_section_begin("##hotkeys_settings");
            gui.row_begin(); ImGui::Combo("Style", &world.hotkeys_style, hotkey_style_modes, IM_ARRAYSIZE(hotkey_style_modes));
            gui.row_checkbox("Right-Aligned", &world.hotkeys_right_aligned);
            gui.row_checkbox("Dynamic Header", &world.hotkeys_dynamic_header);
            gui.popup_section_end();
            gui.row_options_end();
        }
        gui.row_checkbox("Hotkeys", &g_cfg->misc.m_keybinds_list);
        gui.row_checkbox("Spectators", &g_cfg->misc.m_spectator_list);
        gui.row_checkbox("Event Logs", &world.window_event_logs);
        if (world.window_event_logs) {
            gui.row_multibox("Events", event_log_items, world.event_log_filter,
                IM_ARRAYSIZE(event_log_items));
            gui.row_begin();
            ImGui::SliderFloat("Lifetime##event_log", &world.event_log_lifetime, 0.5f, 20.f, "%.1fs");
            gui.row_color("Hit##event_log", &event_log_hit_color);
            gui.row_color("Miss##event_log", &event_log_miss_color);
            gui.row_color("Evaded##event_log", &event_log_evaded_color);
        }
        gui.popup_section_end();
        gui.row_color("Color", &windows_color);
        gui.row_begin(); ImGui::SliderFloat("Line Opacity", &world.windows_line_opacity, 0.f, 1.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Background", &world.windows_background_opacity, 0.f, 1.f, "%.2f");
        gui.row_checkbox("Hit Chance Override", &world.windows_hit_chance_override);
        gui.row_checkbox("Min Damage Override", &world.windows_min_damage_override);
        gui.row_checkbox("Double Tap", &world.windows_double_tap);
        gui.row_checkbox("Duck Peek Assist", &world.windows_duck_peek);
        gui.row_checkbox("Slowdown", &world.windows_slowdown);
        gui.row_checkbox("Jump Bug", &world.windows_jump_bug);
        gui.popup_end();
    }
    if (gui.row_popup("Removals", "Toggle individual client-side render and HUD removals.", 300.f)) {
        gui.popup_section_begin("##removal_hud");
        static const char* hud_removal_labels[] = { "Team Intro", "HUD Panorama Dots", "HUD Bottom Gradient", "HUD Damage Indicator", "HUD Server Advertisements" };
        for (int i = 0; i < 5; ++i) gui.row_checkbox(hud_removal_labels[i], &world.removals[i]);
        gui.popup_section_end();
        gui.popup_section_begin("##removal_effects");
        static const char* effect_removal_labels[] = { "Flashbang Effect", "Smoke Rendering", "First Person Legs", "Carried Hostage", "Post Processing", "Water Effects", "Blood Effects", "Decals" };
        for (int i = 5; i < 13; ++i) gui.row_checkbox(effect_removal_labels[i - 5], &world.removals[i]);
        gui.popup_section_end();
        gui.row_begin(); ImGui::Combo("Ragdolls", &world.ragdoll_mode, ragdoll_modes, IM_ARRAYSIZE(ragdoll_modes));
        gui.row_begin(); ImGui::SliderFloat("Gravity", &world.ragdoll_gravity, 0.f, 1.f, "%.2f");
        gui.row_begin(); ImGui::SliderFloat("Friction", &world.ragdoll_friction, 0.f, 1.f, "%.2f");
        gui.popup_end();
    }
    if (gui.row_popup("Hit Marker", nullptr, 280.f)) {
        gui.popup_section_begin("##hitmarker_2d");
        gui.row_checkbox("2D Marker", &world.hitmarker_2d, &hit2d_color);
        gui.row_begin(); ImGui::SliderFloat("Duration", &world.hitmarker_2d_duration, 0.05f, 2.f, "%.2fs");
        gui.popup_section_end();
        gui.popup_section_begin("##hitmarker_3d");
        gui.row_checkbox("3D Marker", &world.hitmarker_3d, &hit3d_color);
        gui.row_begin(); ImGui::SliderFloat("Duration", &world.hitmarker_3d_duration, 0.05f, 2.f, "%.2fs");
        gui.popup_section_end();
        gui.popup_section_begin("##damage_marker");
        gui.row_checkbox("Damage Marker", &world.damage_marker, &damage_color);
        gui.row_begin(); ImGui::SliderFloat("Duration", &world.damage_marker_duration, 0.05f, 2.f, "%.2fs");
        gui.popup_section_end();
        gui.popup_end();
    }
    if (gui.row_popup("Bullet Tracers", nullptr, 280.f)) {
        gui.popup_section_begin("##tracer_enemy");
        gui.row_checkbox("Enemies", &world.tracer_enemies, &tracer_enemy_color);
        gui.row_begin(); ImGui::SliderFloat("Duration", &world.tracer_enemy_duration, 0.05f, 2.f, "%.2fs");
        gui.popup_section_end();
        gui.popup_section_begin("##tracer_teammate");
        gui.row_checkbox("Teammates", &world.tracer_teammates, &tracer_teammate_color);
        gui.row_begin(); ImGui::SliderFloat("Duration", &world.tracer_teammate_duration, 0.05f, 2.f, "%.2fs");
        gui.popup_section_end();
        gui.popup_section_begin("##tracer_local");
        gui.row_checkbox("Local Player", &world.tracer_local_player, &tracer_local_color);
        gui.row_begin(); ImGui::SliderFloat("Duration", &world.tracer_local_duration, 0.05f, 2.f, "%.2fs");
        gui.popup_section_end();
        gui.row_checkbox("Use Shared Settings", &world.tracer_use_shared_settings);
        gui.popup_end();
    }
    if (gui.row_popup("Bullet Impacts", nullptr, 280.f)) {
        gui.popup_section_begin("##bullet_impacts");
        gui.row_checkbox("Bullet Impacts", &world.bullet_impacts, &impact_color);
        gui.row_checkbox("Impact Sparks", &world.impact_sparks, &spark_color);
        gui.popup_section_end();
        gui.popup_end();
    }
    gui.end_group_box();
    gui.end_grid();

    world.enabled = world.bomb.enabled || world.weapon.enabled || world.hostage.enabled ||
        world.grenade.enabled || world.grenade_trajectory || world.grenade_proximity_warnings ||
        world.grenade.smoke_radius || world.grenade.inferno_radius;
    world.bombs = world.bomb.enabled;
    world.weapons = world.weapon.enabled;
    world.hostages = world.hostage.enabled;
    world.grenades = world.grenade.enabled;
    world.smoke_range = world.grenade.smoke_radius;
    world.molotov_range = world.grenade.inferno_radius;
    world.windows = false;
    world.box = world.bomb.box || world.weapon.box || world.hostage.box || world.grenade.box;
    world.name = world.bomb.text || world.weapon.text || world.hostage.text || world.grenade.text;
    world.icon = world.bomb.icon || world.weapon.icon || world.hostage.icon || world.grenade.icon;
    world.ammo_bar = world.weapon.progress_bar;
    g_cfg->world.m_draw_radar = std::any_of(world.radar_options.begin(), world.radar_options.end(), [](bool value) { return value; });
    g_cfg->world.m_unlock_spectating = world.unlock_spectating_mode > 0;
    g_cfg->world.m_remove_scope_overlay = world.scope_overlay_mode != 0;
    g_cfg->world.m_custom_crosshair = world.force_sniper_crosshair || world.force_crosshair_scoped ||
        world.force_crosshair_c4 || world.penetration_crosshair;
    g_cfg->visuals.m_effects.m_hitmarker = world.hitmarker_2d || world.hitmarker_3d || world.damage_marker;
    g_cfg->visuals.m_effects.m_bullet_tracers = world.tracer_enemies || world.tracer_teammates || world.tracer_local_player;
    g_cfg->visuals.m_effects.m_tracer_color = to_config_color(tracer_enemy_color.a);
    g_cfg->visuals.m_effects.m_bullet_impacts = world.bullet_impacts || world.impact_sparks;
    world.trajectory_color = to_config_color(trajectory_color.a);
    world.inaccuracy_color = to_config_color(inaccuracy_color.a);
    world.scope_color_main = to_config_color(scope_main_color.a);
    world.scope_color_edge = to_config_color(scope_edge_color.a);
    world.bomb.color = to_config_color(bomb_color.a);
    world.bomb.secondary_color = to_config_color(bomb_defused_color.a);
    world.weapon.color = to_config_color(weapon_color.a);
    world.hostage.color = to_config_color(hostage_color.a);
    world.hostage.secondary_color = to_config_color(rescue_color.a);
    world.hostage.progress_background = to_config_color(hostage_progress_bg_color.a);
    world.grenade.color = to_config_color(grenade_color.a);
    world.grenade.secondary_color = to_config_color(smoke_radius_color.a);
    world.grenade.progress_background = to_config_color(inferno_radius_color.a);
    world.grenade.glow_color = to_config_color(projectile_inferno_color.a);
    world.smoke_color = to_config_color(smoke_color.a);
    world.molotov_color = to_config_color(molotov_color.a);
    world.explosion_color = to_config_color(explosion_color.a);
    world.effects_color = to_config_color(effects_color.a);
    world.windows_color = to_config_color(windows_color.a);
    world.event_log_hit_color = to_config_color(event_log_hit_color.a);
    world.event_log_miss_color = to_config_color(event_log_miss_color.a);
    world.event_log_evaded_color = to_config_color(event_log_evaded_color.a);
    world.penetration_color = to_config_color(penetration_color.a);
    world.hitmarker_2d_color = to_config_color(hit2d_color.a);
    world.hitmarker_3d_color = to_config_color(hit3d_color.a);
    world.damage_marker_color = to_config_color(damage_color.a);
    world.tracer_enemy_color = to_config_color(tracer_enemy_color.a);
    world.tracer_teammate_color = to_config_color(tracer_teammate_color.a);
    world.tracer_local_color = to_config_color(tracer_local_color.a);
    world.bullet_impact_color = to_config_color(impact_color.a);
    world.impact_sparks_color = to_config_color(spark_color.a);
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
