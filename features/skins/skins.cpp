// Darkside UI page only; no gameplay implementation.
#include "../../menu/menu.hpp"
#include "../../sdk/config_system/config_system.hpp"

void c_menu::draw_skins() {
    gui.begin_grid(2);

    gui.grid_col(0);
    gui.grid_box("KNIFE", k_auto_height);
    gui.row_begin(); ImGui::SliderInt("Model", &g_cfg->skins.m_knives.m_selected, 0, 20);
    gui.end_group_box();

    gui.grid_box("GLOVES", k_auto_height);
    gui.row_begin(); ImGui::SliderInt("Model", &g_cfg->skins.m_gloves.m_selected, 0, 20);
    gui.end_group_box();

    gui.grid_col(1);
    gui.grid_box("AGENTS", k_auto_height);
    gui.row_begin(); ImGui::SliderInt("Counter-Terrorist", &g_cfg->skins.m_agents.m_selected_ct, 0, 20);
    gui.row_begin(); ImGui::SliderInt("Terrorist", &g_cfg->skins.m_agents.m_selected, 0, 20);
    gui.end_group_box();

    gui.end_grid();
}
