// Darkside UI page only; no gameplay implementation.
#include "../../menu/menu.hpp"
#include "../../sdk/config_system/config_system.hpp"

namespace {
color_value make_ui_color(float r, float g, float b) {
    color_value value;
    value.a = ImVec4(r, g, b, 1.f);
    value.b = value.a;
    return value;
}

struct players_ui_state {
    // These options have no gameplay/config backend in the standalone shell yet.
    // Keep them in one UI state so the controls remain stable while their page is hidden.
    bool offscreen_arrow = false;
    bool sounds = false;
    int player_model = 0;
    int behind_walls_model = 0;
    int on_shot_model = 0;
    int history_model = 0;
    int ragdolls_model = 0;

    color_value offscreen_color = make_ui_color(0.92f, 0.94f, 1.f);
    color_value sounds_color = make_ui_color(1.f, 0.25f, 0.55f);
    color_value player_color = make_ui_color(0.60f, 0.65f, 1.f);
    color_value behind_walls_color = make_ui_color(0.35f, 0.90f, 1.f);
    color_value on_shot_color = make_ui_color(0.90f, 0.95f, 0.95f);
    color_value history_color = make_ui_color(0.75f, 0.75f, 0.78f);
    color_value ragdolls_color = make_ui_color(0.65f, 0.63f, 0.75f);
    color_value kill_effect_color = make_ui_color(1.f, 0.20f, 0.30f);
    color_value glow_color = make_ui_color(0.80f, 0.75f, 1.f);
};

players_ui_state& player_ui() {
    static players_ui_state state;
    return state;
}

void draw_visuals_common() {
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

void draw_visuals_players() {
    auto& esp = g_cfg->visuals.m_player_esp;
    auto& effects = g_cfg->visuals.m_effects;
    auto& glow = g_cfg->visuals.m_glow;
    auto& ui = player_ui();
    static const char* model_modes[] = { "Disabled", "Chams", "Flat", "Glow" };
    static const float weights[] = { 0.62f, 0.38f };

    // Deliberately leave the second column empty. The player model/panorama preview
    // belongs there later; it is not part of the standalone GUI yet.
    gui.begin_grid(2, weights);
    gui.grid_col(0);

    gui.grid_box("ENEMY", k_auto_height);
    gui.row_checkbox("Enabled", &esp.m_enabled);
    gui.row_checkbox("Offscreen Arrow", &ui.offscreen_arrow, &ui.offscreen_color);
    gui.row_checkbox("Sounds", &ui.sounds, &ui.sounds_color);
    gui.end_group_box();

    gui.grid_box("ENEMY MODEL", k_auto_height);
    gui.row_combo("Player", &ui.player_color, &ui.player_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_combo("Behind Walls", &ui.behind_walls_color, &ui.behind_walls_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_combo("On Shot", &ui.on_shot_color, &ui.on_shot_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_combo("History", &ui.history_color, &ui.history_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_combo("Ragdolls", &ui.ragdolls_color, &ui.ragdolls_model, model_modes, IM_ARRAYSIZE(model_modes));
    gui.row_checkbox("Kill Effect", &effects.m_kill_effect, &ui.kill_effect_color);
    gui.row_checkbox("Glow", &glow.m_enabled, &ui.glow_color);
    gui.end_group_box();

    gui.end_grid();
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
    gui.m_visuals_subtab = ImClamp(gui.m_visuals_subtab, 0, 2);
    switch (gui.m_visuals_subtab) {
        case 1: draw_visuals_players(); break;
        case 2: draw_visuals_world(); break;
        default: draw_visuals_common(); break;
    }
}
