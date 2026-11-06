// Darkside UI page only; no gameplay implementation.
#include "../../menu/menu.hpp"
#include "../../sdk/config_system/config_system.hpp"
#include "../../menu/shader_bg.hpp"
#include "../../menu/hashes.hpp"

void c_menu::draw_misc() {
    gui.begin_grid(2);

    gui.grid_col(0);
    gui.grid_box("MOVEMENT", k_auto_height);
    gui.row_checkbox("Bunny Hop", &g_cfg->misc.m_bunny_hop);
    gui.row_begin();
    // Visual placeholder from the Neverlose reference: the card is intentionally empty.
    if (gui.row_options_begin("##auto_strafe_arrow", 216.f, c_gui::row_control::toggle,
                              ICON_FA_CHEVRON_RIGHT))
        gui.row_options_end();
    gui.row_checkbox("Automatic Strafe", &g_cfg->misc.m_auto_strafe);
    static const char* strafe_modes[] = { "Legit", "Directional", "Rage" };
    gui.row_begin(); ImGui::Combo("Strafe Type", &g_cfg->misc.m_strafe_type, strafe_modes, IM_ARRAYSIZE(strafe_modes));
    gui.row_begin(); ImGui::SliderInt("Smooth", &g_cfg->misc.m_strafe_smooth, 0, 100);
    gui.end_group_box();

    gui.grid_col(1);
    gui.grid_box("OTHER", k_auto_height);
    gui.row_begin();
    // Same visual-only disclosure arrow; no new shader behaviour is attached to it.
    if (gui.row_options_begin("##menu_shader_arrow", 216.f, c_gui::row_control::toggle,
                              ICON_FA_CHEVRON_RIGHT))
        gui.row_options_end();
    gui.row_checkbox("Menu Shader", &shader_bg::state().enabled);
    gui.row_begin(); ImGui::SliderFloat("Shader Opacity", &shader_bg::state().opacity, 0.f, 1.f, "%.2f");
    gui.end_group_box();

    gui.end_grid();
}
