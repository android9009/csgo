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
    bool main_items[6] = {};
    bool flags[15] = {};
    bool weapon_items[6] = {};
    bool aimbot_items[2] = {};
};

struct player_side_ui_state {
    // Missing backends stay in per-side UI state, so Enemies, Teammates and Self never
    // overwrite one another while another page is hidden.
    bool offscreen_arrow = false;
    bool sounds = false;
    int player_model = 0;
    int behind_walls_model = 0;
    int hitbox_model = 0;
    int on_shot_model = 0;
    int history_model = 0;
    int ragdolls_model = 0;
    bool disable_model_rendering = false;
    bool glow = false;
    int local_model = 0;
    int local_weapon = 0;
    int local_transparency = 100;

    color_value offscreen_color = make_ui_color(0.92f, 0.94f, 1.f);
    color_value sounds_color = make_ui_color(1.f, 0.25f, 0.55f);
    color_value player_color = make_ui_color(0.60f, 0.65f, 1.f);
    color_value behind_walls_color = make_ui_color(0.35f, 0.90f, 1.f);
    color_value hitbox_color = make_ui_color(1.f, 0.35f, 0.55f);
    color_value on_shot_color = make_ui_color(0.90f, 0.95f, 0.95f);
    color_value history_color = make_ui_color(0.75f, 0.75f, 0.78f);
    color_value ragdolls_color = make_ui_color(0.65f, 0.63f, 0.75f);
    color_value kill_effect_color = make_ui_color(1.f, 0.20f, 0.30f);
    color_value glow_color = make_ui_color(0.80f, 0.75f, 1.f);
    char custom_sprite[128] = "models/weapons/customization/paints/custom/money";
    esp_items_ui_state esp_items;
};

struct players_ui_state {
    int active_side = 0; // 0 = Enemies, 1 = Teammates, 2 = Self
    player_side_ui_state sides[3];
    // Screen-independent reference height for the right column. Enemies is the
    // design reference even while the Teammates or Self tab is selected.
    float enemies_right_height = 0.f;
    bool esp_items_open = true;

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

void load_shared_player_views(players_ui_state& ui) {
    for (int side_index = 0; side_index < player_visuals::side_count; ++side_index) {
        const auto& config = player_visuals::settings(side_index);
        auto& view = ui.sides[side_index];
        view.player_model = std::clamp(config.player_material, 0, visuals_chams::mode_count - 1);
        view.behind_walls_model = std::clamp(config.behind_walls_material, 0, visuals_chams::mode_count - 1);
        view.player_color.a = to_vec4(config.player_color);
        view.player_color.b = to_vec4(config.player_secondary);
        view.player_color.gradient = config.player_gradient;
        view.behind_walls_color.a = to_vec4(config.behind_walls_color);
        view.behind_walls_color.b = to_vec4(config.behind_walls_secondary);
        view.behind_walls_color.gradient = config.behind_walls_gradient;
        view.hitbox_model = std::clamp(config.hitbox_material, 0, visuals_chams::mode_count - 1);
        view.on_shot_model = std::clamp(config.on_shot_material, 0, visuals_chams::mode_count - 1);
        view.history_model = std::clamp(config.history_material, 0, visuals_chams::mode_count - 1);
        view.ragdolls_model = std::clamp(config.ragdoll_material, 0, visuals_chams::mode_count - 1);
        view.hitbox_color.a = to_vec4(config.hitbox_color);
        view.hitbox_color.b = to_vec4(config.hitbox_secondary);
        view.hitbox_color.gradient = config.hitbox_gradient;
        view.on_shot_color.a = to_vec4(config.on_shot_color);
        view.on_shot_color.b = to_vec4(config.on_shot_secondary);
        view.on_shot_color.gradient = config.on_shot_gradient;
        view.history_color.a = to_vec4(config.history_color);
        view.history_color.b = to_vec4(config.history_secondary);
        view.history_color.gradient = config.history_gradient;
        view.ragdolls_color.a = to_vec4(config.ragdoll_color);
        view.ragdolls_color.b = to_vec4(config.ragdoll_secondary);
        view.ragdolls_color.gradient = config.ragdoll_gradient;
        std::strncpy(view.custom_sprite, config.custom_sprite.c_str(), sizeof(view.custom_sprite) - 1);
        view.custom_sprite[sizeof(view.custom_sprite) - 1] = '\0';
        std::copy(std::begin(config.main), std::end(config.main), std::begin(view.esp_items.main_items));
        std::copy(std::begin(config.flags), std::end(config.flags), std::begin(view.esp_items.flags));
        std::copy(std::begin(config.weapon), std::end(config.weapon), std::begin(view.esp_items.weapon_items));
        std::copy(std::begin(config.aimbot), std::end(config.aimbot), std::begin(view.esp_items.aimbot_items));
    }
}

void store_shared_player_views(players_ui_state& ui) {
    for (int side_index = 0; side_index < player_visuals::side_count; ++side_index) {
        auto& config = player_visuals::settings(side_index);
        const auto& view = ui.sides[side_index];
        config.player_material = view.player_model;
        config.behind_walls_material = view.behind_walls_model;
        config.player_color = to_config_color(view.player_color.a);
        config.player_secondary = to_config_color(view.player_color.b);
        config.player_gradient = view.player_color.gradient;
        config.behind_walls_color = to_config_color(view.behind_walls_color.a);
        config.behind_walls_secondary = to_config_color(view.behind_walls_color.b);
        config.behind_walls_gradient = view.behind_walls_color.gradient;
        config.hitbox_material = view.hitbox_model;
        config.on_shot_material = view.on_shot_model;
        config.history_material = view.history_model;
        config.ragdoll_material = view.ragdolls_model;
        config.hitbox_color = to_config_color(view.hitbox_color.a);
        config.hitbox_secondary = to_config_color(view.hitbox_color.b);
        config.hitbox_gradient = view.hitbox_color.gradient;
        config.on_shot_color = to_config_color(view.on_shot_color.a);
        config.on_shot_secondary = to_config_color(view.on_shot_color.b);
        config.on_shot_gradient = view.on_shot_color.gradient;
        config.history_color = to_config_color(view.history_color.a);
        config.history_secondary = to_config_color(view.history_color.b);
        config.history_gradient = view.history_color.gradient;
        config.ragdoll_color = to_config_color(view.ragdolls_color.a);
        config.ragdoll_secondary = to_config_color(view.ragdolls_color.b);
        config.ragdoll_gradient = view.ragdolls_color.gradient;
        config.custom_sprite = view.custom_sprite;
        std::copy(std::begin(view.esp_items.main_items), std::end(view.esp_items.main_items), std::begin(config.main));
        std::copy(std::begin(view.esp_items.flags), std::end(view.esp_items.flags), std::begin(config.flags));
        std::copy(std::begin(view.esp_items.weapon_items), std::end(view.esp_items.weapon_items), std::begin(config.weapon));
        std::copy(std::begin(view.esp_items.aimbot_items), std::end(view.esp_items.aimbot_items), std::begin(config.aimbot));
    }
}

void draw_custom_sprite_row(player_side_ui_state& side) {
    if (side.player_model != visuals_chams::custom_sprite &&
        side.behind_walls_model != visuals_chams::custom_sprite &&
        side.hitbox_model != visuals_chams::custom_sprite &&
        side.on_shot_model != visuals_chams::custom_sprite &&
        side.history_model != visuals_chams::custom_sprite &&
        side.ragdolls_model != visuals_chams::custom_sprite) {
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

void draw_esp_checkbox_section(const char* id, const char* title, const char* const* labels,
                               bool* values, int count) {
    gui.popup_section_begin(id, true, 10.f);
    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    ImGui::PushFont(fonts->Fonts.Size > 4 ? fonts->Fonts[4] : ImGui::GetFont());
    ImGui::TextUnformatted(gui.tr(title));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.f, 5.f * gui.m_scale));
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        gui.popup_toggle(nullptr, labels[i], &values[i]);
        ImGui::PopID();
    }
    gui.popup_section_end();
}

void draw_esp_effects_section() {
    auto& effects = g_cfg->visuals.m_effects;
    gui.popup_section_begin("Effects", true, 10.f);
    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    ImGui::PushFont(fonts->Fonts.Size > 4 ? fonts->Fonts[4] : ImGui::GetFont());
    ImGui::TextUnformatted(gui.tr("Effects"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.f, 5.f * gui.m_scale));
    gui.popup_toggle(nullptr, "Bullet Tracers", &effects.m_bullet_tracers);
    gui.popup_toggle(nullptr, "Bullet Impacts", &effects.m_bullet_impacts);
    gui.popup_toggle(nullptr, "Hit Marker", &effects.m_hitmarker);
    gui.popup_toggle(nullptr, "Hit Marker Sound", &effects.m_hitmarker_sound);
    gui.popup_section_end();
}

void draw_esp_items_panel(player_side_ui_state& side, ImVec2 origin, float width, float height) {
    static const char* main_items[] = {
        "Bounding Box", "Name", "Distance", "Health Bar", "Ammo Bar", "Skeleton"
    };
    static const char* flags[] = {
        "Unarmored", "Defuser", "Blind", "Scoped", "Reload", "Immunity", "Slowed",
        "Vulnerable", "Bomb", "Hostage", "Defuse", "Pin Pulled", "Money", "Order Priority", "Delay"
    };
    static const char* weapon_items[] = {
        "Text", "Icon", "Readiness Bar", "Bomb", "Defuser", "Taser"
    };
    static const char* aimbot_items[] = { "Hit Chance", "Hitboxes" };
    const float s = gui.m_scale;
    if (width < 80.f * s || height < 40.f * s)
        return;

    // This is an ordinary in-window panel, not a popup over a preview. It is
    // visible immediately and its child supplies scrolling for all sections.
    ImGuiWindow* outer = ImGui::GetCurrentWindow();
    const ImVec2 saved_cursor = outer->DC.CursorPos;
    const ImVec2 saved_cursor_max = outer->DC.CursorMaxPos;
    gui.popup_surface(ImGui::GetWindowDrawList(), origin, origin + ImVec2(width, height),
                      15.f * s, 1.f, true);
    ImGui::SetCursorScreenPos(origin + ImVec2(10.f * s, 10.f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    const bool visible = ImGui::BeginChild("##esp_items_panel", ImVec2(width - 20.f * s, height - 20.f * s),
                                           false, ImGuiWindowFlags_NoBackground);
    if (visible) {
        draw_esp_checkbox_section("Main", "Main", main_items, side.esp_items.main_items, IM_ARRAYSIZE(main_items));
        draw_esp_checkbox_section("Flags", "Flags", flags, side.esp_items.flags, IM_ARRAYSIZE(flags));
        draw_esp_checkbox_section("Weapon", "Weapon", weapon_items, side.esp_items.weapon_items, IM_ARRAYSIZE(weapon_items));
        draw_esp_checkbox_section("Aimbot", "Aimbot", aimbot_items, side.esp_items.aimbot_items, IM_ARRAYSIZE(aimbot_items));
        draw_esp_effects_section();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    outer->DC.CursorPos = saved_cursor;
    outer->DC.CursorMaxPos = saved_cursor_max;
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
    gui.row_combo("Player", &side.player_color, &side.player_model, visuals_chams::mode_names, visuals_chams::mode_count);
    gui.row_combo("Behind Walls", &side.behind_walls_color, &side.behind_walls_model, visuals_chams::mode_names, visuals_chams::mode_count);
    draw_custom_sprite_row(side);
    gui.row_combo("Hitboxes", &side.hitbox_color, &side.hitbox_model, visuals_chams::mode_names, visuals_chams::mode_count);
    gui.row_combo("On Shot", &side.on_shot_color, &side.on_shot_model, visuals_chams::mode_names, visuals_chams::mode_count);
    gui.row_combo("History", &side.history_color, &side.history_model, visuals_chams::mode_names, visuals_chams::mode_count);
    gui.row_combo("Ragdolls", &side.ragdolls_color, &side.ragdolls_model, visuals_chams::mode_names, visuals_chams::mode_count);
    gui.row_checkbox("Kill Effect", &effects.m_kill_effect, &side.kill_effect_color);
    gui.row_checkbox("Glow", &glow.m_enabled, &side.glow_color);
    gui.end_group_box();
}

void draw_teammate_side(player_side_ui_state& side) {
    gui.grid_box("TEAMMATE", k_auto_height);
    gui.row_checkbox("Enabled", &player_visuals::settings(1).esp_enabled);
    gui.end_group_box();

    gui.grid_box("TEAMMATE MODEL", k_auto_height);
    gui.row_combo("Player", &side.player_color, &side.player_model, visuals_chams::mode_names, visuals_chams::mode_count);
    gui.row_combo("Behind Walls", &side.behind_walls_color, &side.behind_walls_model, visuals_chams::mode_names, visuals_chams::mode_count);
    draw_custom_sprite_row(side);
    gui.row_checkbox("Disable Model Rendering", &side.disable_model_rendering);
    gui.row_checkbox("Glow", &side.glow, &side.glow_color);
    gui.end_group_box();
}

void draw_self_side(player_side_ui_state& side) {
    static const char* local_models[] = { "Default", "Counter-Terrorist", "Terrorist" };
    static const char* local_weapons[] = { "Default", "Current Weapon" };

    gui.grid_box("LOCAL PLAYER MODEL", k_auto_height);
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

bool draw_esp_items_header_button(ImVec2 center, bool held) {
    static float hover = 0.f;
    const float s = gui.m_scale;
    const float font_size = ImGui::GetFontSize();
    ImFont* font = ImGui::GetFont();
    const ImVec2 icon_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, ICON_FA_LIST_ALT);
    const ImVec2 text_size = ImGui::CalcTextSize("ESP Items");
    const ImVec2 size(icon_size.x + text_size.x + 31.f * s, 26.f * s);
    const ImVec2 pos(center.x - size.x * 0.5f, center.y);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 saved_cursor = window->DC.CursorPos;
    const ImVec2 saved_cursor_max = window->DC.CursorMaxPos;
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton("##players_esp_items_header", size);
    hover = ImLerp(hover, ImGui::IsItemHovered() ? 1.f : 0.f,
                   ImClamp(ImGui::GetIO().DeltaTime * 15.f, 0.f, 1.f));
    const float plate = ImMax(hover, held ? 1.f : 0.f);
    ImDrawList* draw = window->DrawList;
    if (plate > 0.01f)
        draw->AddRectFilled(pos, pos + size, gui.frame_inactive.to_im_color(0.45f + 0.45f * plate), 8.f * s);
    const ImU32 ink = held ? gui.accent_color.to_im_color() : gui.text.to_im_color();
    const ImVec2 icon_pos = pos + ImVec2(9.f * s,
        (size.y - 2.f * TextInkCenterY(font, font_size, ICON_FA_LIST_ALT)) * 0.5f);
    draw->AddText(font, font_size, icon_pos, ink, ICON_FA_LIST_ALT);
    draw->AddText(ImVec2(IM_ROUND(icon_pos.x + icon_size.x + 7.f * s),
        IM_ROUND(pos.y + (size.y - text_size.y) * 0.5f)), ink, "ESP Items");
    window->DC.CursorPos = saved_cursor;
    window->DC.CursorMaxPos = saved_cursor_max;
    return clicked;
}

void draw_visuals_players() {
    auto& ui = player_ui();
    load_shared_player_views(ui);

    const ImVec2 content_origin = ImGui::GetCursorScreenPos();
    const float content_width = ImGui::GetContentRegionAvail().x;
    const float column_width = (content_width - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ui.active_side = ImClamp(ui.active_side, 0, 2);
    const bool has_esp_items = ui.active_side < 2;
    const ImVec2 header_center = content_origin + ImVec2(
        column_width + ImGui::GetStyle().ItemSpacing.x + column_width * 0.5f, -4.f * gui.m_scale);
    const bool customize_clicked = has_esp_items &&
        draw_esp_items_header_button(header_center, ui.esp_items_open);

    gui.begin_grid(2);
    gui.grid_col(0);
    if (ui.active_side == 0)
        draw_enemy_side(ui.sides[0]);
    else if (ui.active_side == 1)
        draw_teammate_side(ui.sides[1]);
    else
        draw_self_side(ui.sides[2]);
    const float active_left_bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;

    // The right column is a regular ESP Items panel. It does not depend on an
    // agent/ESP preview or on a popup being opened before its controls appear.
    gui.end_grid();

    const ImVec2 right_column_origin = content_origin + ImVec2(
        column_width + ImGui::GetStyle().ItemSpacing.x, 0.f);
    const ImVec2 panel_origin = right_column_origin + ImVec2(0.f, 28.f * gui.m_scale);
    const float active_right_height = ImMax(1.f, active_left_bottom - panel_origin.y);
    if (ui.active_side == 0)
        ui.enemies_right_height = active_right_height / gui.m_scale;
    // Do not let the shorter Teammates or Self controls shrink the right column.
    // Keep the Enemies reference in design units so Menu Scale changes still work.
    const float right_column_height = ui.enemies_right_height > 0.f
        ? ui.enemies_right_height * gui.m_scale
        : active_right_height;
    if (customize_clicked)
        ui.esp_items_open = !ui.esp_items_open;
    if (has_esp_items && ui.esp_items_open)
        draw_esp_items_panel(ui.sides[ui.active_side], panel_origin, column_width, right_column_height);
    store_shared_player_views(ui);

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
    gui.begin_grid(2);
    gui.grid_col(0);
    gui.grid_box("WORLD", k_auto_height);
    gui.row_begin(); ImGui::SliderInt("Exposure", &g_cfg->world.m_exposure, 0, 100);
    gui.row_begin(); ImGui::SliderFloat("Brightness", &g_cfg->world.m_brightness, 0.f, 1.f, "%.2f");
    gui.row_checkbox("Remove Fog", &g_cfg->world.m_render_fog);
    gui.end_group_box();

    gui.grid_col(1);
    gui.grid_box("THIRDPERSON", k_auto_height);
    gui.row_checkbox("Enabled", &g_cfg->world_esp.m_enable_thirdperson);
    gui.row_begin(); ImGui::SliderInt("Distance", &g_cfg->world_esp.m_distance, 50, 300);
    gui.end_group_box();
    gui.end_grid();
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
