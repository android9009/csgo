// Visuals menu page only. Gameplay ESP/rendering code belongs under features/visuals.
#include "menu.hpp"
#include "hashes.hpp"
#include "agent_preview.hpp"
#include "../sdk/config_system/config_system.hpp"

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
    // Missing backends stay in per-side UI state, so Enemies and Teammates never overwrite
    // one another while the other page is hidden.
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

    color_value offscreen_color = make_ui_color(0.92f, 0.94f, 1.f);
    color_value sounds_color = make_ui_color(1.f, 0.25f, 0.55f);
    color_value player_color = make_ui_color(0.60f, 0.65f, 1.f);
    color_value behind_walls_color = make_ui_color(0.35f, 0.90f, 1.f);
    color_value on_shot_color = make_ui_color(0.90f, 0.95f, 0.95f);
    color_value history_color = make_ui_color(0.75f, 0.75f, 0.78f);
    color_value ragdolls_color = make_ui_color(0.65f, 0.63f, 0.75f);
    color_value kill_effect_color = make_ui_color(1.f, 0.20f, 0.30f);
    color_value glow_color = make_ui_color(0.80f, 0.75f, 1.f);
    esp_items_ui_state esp_items;
};

struct players_ui_state {
    int active_side = 0; // 0 = Enemies, 1 = Teammates
    player_side_ui_state sides[2];

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

void draw_esp_chip_section(const char* id, const char* title, const char* const* labels,
                           bool* values, int count) {
    gui.popup_section_begin(id, true, 10.f);
    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    ImGui::PushFont(fonts->Fonts.Size > 4 ? fonts->Fonts[4] : ImGui::GetFont());
    ImGui::TextUnformatted(gui.tr(title));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.f, 5.f * gui.m_scale));
    gui.chips(id, labels, values, count);
    gui.popup_section_end();
}

void draw_esp_items_popup(player_side_ui_state& side, bool clicked,
                          ImVec2 content_origin, float content_width, float content_bottom) {
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

    // A content-width card directly under the header, not a cursor-relative popup. Its top edge
    // stays fixed while the opening fade runs; its bottom is aligned with the left column below.
    bool wants_open = false;
    const float anim = gui.popup_animation("ESP ItemsPopup", clicked, wants_open);
    const ImVec2 anchor = content_origin + ImVec2(0.f, 28.f * gui.m_scale);
    if (!gui.begin_popup_card_at("ESP ItemsPopup", wants_open, anim, anchor, ImVec2(0.f, 0.f),
                                 content_width / gui.m_scale))
        return;

    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    ImGui::PushFont(fonts->Fonts.Size > 4 ? fonts->Fonts[4] : ImGui::GetFont());
    ImGui::TextColored(gui.text.to_vec4(1.f, false), "%s", gui.tr("ESP Items"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.f, 4.f * gui.m_scale));

    // Match the visible bottom of the left column exactly. The child keeps all chip sections
    // available through scrolling instead of letting the auto-height card protrude below Glow.
    const float sections_y = ImGui::GetCursorScreenPos().y;
    const float sections_h = ImMax(60.f * gui.m_scale,
        content_bottom - sections_y - 10.f * gui.m_scale); // outer card's bottom padding
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::BeginChild("##esp_items_sections", ImVec2(0.f, sections_h), false,
                      ImGuiWindowFlags_NoBackground);
    draw_esp_chip_section("Main", "Main", main_items, side.esp_items.main_items, IM_ARRAYSIZE(main_items));
    draw_esp_chip_section("Flags", "Flags", flags, side.esp_items.flags, IM_ARRAYSIZE(flags));
    draw_esp_chip_section("Weapon", "Weapon", weapon_items, side.esp_items.weapon_items, IM_ARRAYSIZE(weapon_items));
    draw_esp_chip_section("Aimbot", "Aimbot", aimbot_items, side.esp_items.aimbot_items, IM_ARRAYSIZE(aimbot_items));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    gui.end_popup_card();
}

void draw_enemy_side(player_side_ui_state& side) {
    auto& esp = g_cfg->visuals.m_player_esp;
    auto& effects = g_cfg->visuals.m_effects;
    auto& glow = g_cfg->visuals.m_glow;
    static const char* model_modes[] = { "Disabled", "Chams", "Flat", "Glow" };

    gui.grid_box("ENEMY", k_auto_height);
    gui.row_checkbox("Enabled", &esp.m_enabled);
    gui.row_checkbox("Offscreen Arrow", &side.offscreen_arrow, &side.offscreen_color);
    gui.row_checkbox("Sounds", &side.sounds, &side.sounds_color);
    gui.end_group_box();

    gui.grid_box("ENEMY MODEL", k_auto_height);
    gui.row_combo("Player", &side.player_color, &side.player_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_combo("Behind Walls", &side.behind_walls_color, &side.behind_walls_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_combo("On Shot", &side.on_shot_color, &side.on_shot_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_combo("History", &side.history_color, &side.history_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_combo("Ragdolls", &side.ragdolls_color, &side.ragdolls_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_checkbox("Kill Effect", &effects.m_kill_effect, &side.kill_effect_color);
    gui.row_checkbox("Glow", &glow.m_enabled, &side.glow_color);
    gui.end_group_box();
}

void draw_teammate_side(player_side_ui_state& side) {
    static const char* model_modes[] = { "Disabled", "Chams", "Flat", "Glow" };
    static const char* local_models[] = { "Default", "Counter-Terrorist", "Terrorist" };
    static const char* local_weapons[] = { "Default", "Current Weapon" };

    gui.grid_box("TEAMMATE", k_auto_height);
    gui.row_checkbox("Enabled", &g_cfg->visuals.m_player_esp.m_teammates);
    gui.end_group_box();

    gui.grid_box("TEAMMATE MODEL", k_auto_height);
    gui.row_combo("Player", &side.player_color, &side.player_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_combo("Behind Walls", &side.behind_walls_color, &side.behind_walls_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_checkbox("Disable Model Rendering", &side.disable_model_rendering);
    gui.row_checkbox("Glow", &side.glow, &side.glow_color);
    gui.end_group_box();

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

void draw_visuals_players() {
    auto& esp = g_cfg->visuals.m_player_esp;
    auto& ui = player_ui();
    static const char* header_icons[] = { ICON_FA_USER, ICON_FA_USER_MINUS, ICON_FA_LIST_ALT };
    static const char* header_labels[] = { "Enemies", "Teammates", "ESP Items" };

    const ImVec2 content_origin = ImGui::GetCursorScreenPos();
    const float content_width = ImGui::GetContentRegionAvail().x;
    const float column_width = (content_width - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ui.active_side = ImClamp(ui.active_side, 0, 1);
    const int previous_side = ui.active_side;
    const ImGuiID customize_id = ImGui::GetID("ESP ItemsPopup");
    const bool customize_visible = ImGui::IsPopupOpen(customize_id, ImGuiPopupFlags_None) ||
                                   gui.popup_fade_visible(customize_id);
    const ImVec2 header_center = content_origin + ImVec2(
        column_width + ImGui::GetStyle().ItemSpacing.x + column_width * 0.5f, -4.f * gui.m_scale);
    const int header_clicked = gui.icon_tabs("##players_header", header_icons, header_labels,
        IM_ARRAYSIZE(header_icons), &ui.active_side, header_center, nullptr,
        customize_visible ? 2 : -1, true, 2);
    const bool customize_clicked = header_clicked == 2;
    if (customize_clicked)
        ui.active_side = previous_side; // ESP Items is an action, not a third player side
    else
        ui.active_side = ImClamp(ui.active_side, 0, 1);

    // The original config has one enemy ESP set. Keep its existing controls live while the
    // additional per-side customize options remain UI state in the standalone shell.
    auto& enemy_items = ui.sides[0].esp_items;
    if (ui.active_side == 0) {
        enemy_items.main_items[0] = esp.m_bounding_box;
        enemy_items.main_items[1] = esp.m_name;
        enemy_items.main_items[2] = esp.m_distance;
        enemy_items.main_items[3] = esp.m_health_bar;
        enemy_items.main_items[4] = esp.m_ammo_bar;
        enemy_items.main_items[5] = esp.m_skeleton;
        enemy_items.weapon_items[0] = esp.m_weapon;
    }

    gui.begin_grid(2);
    gui.grid_col(0);
    if (ui.active_side == 0)
        draw_enemy_side(ui.sides[0]);
    else
        draw_teammate_side(ui.sides[1]);
    const float left_content_bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;

    // The right column displays the active side's VPK character portrait,
    // not a separate CT/T settings panel. ESP Items remains a card above the same column.
    gui.end_grid();

    const ImVec2 right_column_origin = content_origin + ImVec2(
        column_width + ImGui::GetStyle().ItemSpacing.x, 0.f);
    const ImVec2 preview_origin = right_column_origin + ImVec2(0.f, 28.f * gui.m_scale);
    agent_preview::draw(preview_origin,
                        ImVec2(right_column_origin.x + column_width, left_content_bottom),
                        ui.active_side);
    draw_esp_items_popup(ui.sides[ui.active_side], customize_clicked,
                         right_column_origin, column_width, left_content_bottom);
    if (ui.active_side == 0) {
        esp.m_bounding_box = enemy_items.main_items[0];
        esp.m_name = enemy_items.main_items[1];
        esp.m_distance = enemy_items.main_items[2];
        esp.m_health_bar = enemy_items.main_items[3];
        esp.m_ammo_bar = enemy_items.main_items[4];
        esp.m_skeleton = enemy_items.main_items[5];
        esp.m_weapon = enemy_items.weapon_items[0];
    }
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

void c_menu::draw_visuals() {
    gui.m_visuals_subtab = ImClamp(gui.m_visuals_subtab, 1, 2);
    if (gui.m_visuals_subtab == 2)
        draw_visuals_world();
    else
        draw_visuals_players();
}
