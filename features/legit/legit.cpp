// Darkside UI page only; no gameplay implementation.
#include "../../menu/menu.hpp"
#include "../../sdk/config_system/config_system.hpp"

void c_menu::draw_legit() {
    gui.begin_grid(2);

    gui.grid_col(0);
    gui.grid_box("AIMBOT", k_auto_height);
    gui.row_checkbox("Enabled", &g_cfg->legit_bot.m_enabled);
    gui.row_begin(); ImGui::SliderFloat("Field Of View", &g_cfg->legit_bot.m_fov, 0.f, 180.f, "%.1f");
    gui.row_begin(); ImGui::SliderFloat("Smooth", &g_cfg->legit_bot.m_smooth, 1.f, 20.f, "%.1f");
    gui.end_group_box();

    gui.grid_col(1);
    gui.grid_box("TRIGGERBOT", k_auto_height);
    gui.row_checkbox("Enabled", &g_cfg->legit_bot.m_triggerbot_enabled);
    gui.end_group_box();

    gui.grid_box("BACKTRACK", k_auto_height);
    gui.row_checkbox("Enabled", &g_cfg->legit_bot.m_backtrack_enabled);
    gui.row_begin(); ImGui::SliderInt("Ticks", &g_cfg->legit_bot.m_backtrack_ticks, 0, 15);
    gui.end_group_box();

    gui.end_grid();
}
