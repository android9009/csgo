// Darkside UI page only; no gameplay implementation.
#include "../../menu/menu.hpp"
#include "../../sdk/config_system/config_system.hpp"

void c_menu::draw_anti_aim() {
    auto& anti = g_cfg->anti_hit;

    static const char* pitch_modes[]  = { "Off", "Down", "Up", "Zero", "Custom" };
    static const char* yaw_modes[]    = { "Off", "Local View", "At Target" };
    static const char* jitter_modes[] = { "Disabled", "Offset", "Center", "3-Way", "5-Way" };

    gui.begin_grid(2);

    gui.grid_col(0);
    gui.grid_box("MAIN", k_auto_height);

    gui.row_checkbox("Enabled", &anti.m_enabled);
    gui.row_begin(); ImGui::Combo("Pitch", &anti.m_pitch_mode, pitch_modes, IM_ARRAYSIZE(pitch_modes));

    // Yaw (gear: Yaw Add)
    gui.row_begin();
    if (gui.row_options_begin("##yaw_opts", 250.f, c_gui::row_control::box)) {
        gui.row_begin(); ImGui::SliderInt("Yaw Add", &anti.m_yaw_amount, -180, 180);
        gui.row_options_end();
    }
    gui.row_begin(); ImGui::Combo("Yaw", &anti.m_yaw_mode, yaw_modes, IM_ARRAYSIZE(yaw_modes));
    gui.end_group_box();

    gui.grid_col(1);
    gui.grid_box("JITTER", k_auto_height);

    // Yaw Jitter (gear: range for offset/center/3-way/5-way)
    gui.row_begin();
    if (gui.row_options_begin("##jitter_opts", 250.f, c_gui::row_control::box)) {
        gui.row_begin(); ImGui::SliderInt("Jitter Range", &anti.m_jitter_amount, 0, 90);
        gui.row_options_end();
    }
    gui.row_begin(); ImGui::Combo("Yaw Jitter", &anti.m_yaw_jitter, jitter_modes, IM_ARRAYSIZE(jitter_modes));
    gui.end_group_box();

    gui.grid_box("MOVEMENT", k_auto_height);

    // Slow Walk (gear: speed)
    gui.row_begin();
    if (gui.row_options_begin("##slowwalk_opts", 250.f, c_gui::row_control::toggle)) {
        gui.row_begin(); ImGui::SliderInt("Speed", &anti.m_aa_slow_walk_speed, 1, 100, "%d%%");
        gui.row_options_end();
    }
    gui.row_checkbox("Slow Walk", &anti.m_aa_slow_walk);
    gui.row_checkbox("Leg Jitter", &anti.m_leg_jitter);
    gui.row_checkbox("Freestanding", &anti.m_freestanding);
    gui.end_group_box();

    gui.end_grid();
}
