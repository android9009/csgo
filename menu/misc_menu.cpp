#include "../features/misc/misc.hpp"
#include "menu.hpp"
#include "../sdk/config_system/config_system.hpp"
#include "shader_bg.hpp"

namespace {
void draw_feature_toggle(const char* label, bool& value, bool with_options = false) {
    if (with_options) {
        const std::string id = std::string("##misc_") + label + "_options";
        gui.row_begin();
        if (gui.row_options_begin(id.c_str(), 216.f, c_gui::row_control::toggle))
            gui.row_options_end();
    }
    gui.row_checkbox(label, &value);
}
}


void c_menu::draw_misc() {
    auto& state = misc_features::settings();
    static const char* strafe_modes[] = { "Legit", "Directional" };
    static const char* filter_chat_modes[] = { "Select" };

    auto& misc = g_cfg->misc;
    gui.begin_grid(2);

    gui.grid_col(0);
    gui.grid_box("MOVEMENT", k_auto_height);
    gui.row_checkbox("Bunny Hop", &misc.m_bunny_hop);
    misc.m_strafe_type = ImClamp(misc.m_strafe_type, 0, IM_ARRAYSIZE(strafe_modes) - 1);

    gui.row_begin();
    if (gui.row_options_begin("##misc_air_strafe_options", 250.f, c_gui::row_control::toggle)) {
        gui.row_begin(); ImGui::Combo("Strafe Type", &misc.m_strafe_type, strafe_modes, IM_ARRAYSIZE(strafe_modes));
        gui.row_begin(); ImGui::SliderInt("Smooth", &misc.m_strafe_smooth, 0, 100);
        gui.row_options_end();
    }
    gui.row_checkbox("Air Strafe", &misc.m_auto_strafe);

    draw_feature_toggle("No Fall Damage", state.no_fall_damage);
    draw_feature_toggle("Edge Quick Stop", state.edge_quick_stop);
    draw_feature_toggle("Standalone Quick Stop", state.standalone_quick_stop);
    draw_feature_toggle("Edge Jump", state.edge_jump);
    gui.row_begin();
    if (gui.row_options_begin("##misc_slow_walk_options", 216.f, c_gui::row_control::toggle)) {
        gui.row_begin(); ImGui::SliderInt("Units", &state.slow_walk_speed, 0, 250, "%d units");
        gui.row_options_end();
    }
    gui.row_checkbox("Slow Walk", &state.slow_walk);
    draw_feature_toggle("Fast Ladder", state.fast_ladder);
    gui.end_group_box();

    // Keep the existing menu effect reachable without adding a row to the reference Features list.
    gui.grid_box("MENU", k_auto_height);
    gui.row_begin();
    if (gui.row_options_begin("##misc_menu_shader_options", 216.f, c_gui::row_control::toggle)) {
        gui.row_begin(); ImGui::SliderFloat("Shader Opacity", &shader_bg::state().opacity, 0.f, 1.f, "%.2f");
        gui.row_options_end();
    }
    gui.row_checkbox("Menu Shader", &shader_bg::state().enabled);
    gui.end_group_box();

    gui.grid_col(1);
    // Wrap the content instead of stretching the box to the bottom of the tab: a half-empty box
    // running the whole height read as "the right column is longer than the left one".
    gui.grid_box("FEATURES", k_auto_height);
    draw_feature_toggle("Quick Switch", state.quick_switch);
    draw_feature_toggle("Super Toss", state.super_toss);
    draw_feature_toggle("Hit Sound", state.hit_sound, true);
    draw_feature_toggle("Automatic Purchase", state.automatic_purchase, true);
    gui.row_begin();
    if (gui.row_options_begin("##misc_grenade_release_options", 250.f, c_gui::row_control::toggle)) {
        gui.row_begin(); ImGui::SliderInt("Damage", &state.grenade_release_damage, 1, 50, "%d hp");
        gui.row_begin(); ImGui::SliderInt("Molly Radius", &state.grenade_release_molly_radius, 1, 200, "%d units");
        gui.row_checkbox("Disable in Air", &state.grenade_release_disable_in_air);
        gui.row_options_end();
    }
    gui.row_checkbox("Automatic Grenade Release", &state.automatic_grenade_release,
        "Release HE when predicted enemy damage meets Damage; release Molotov/Incendiary when an enemy is within Molly Radius. Other grenades remain manual.");
    draw_feature_toggle("Auto-Accept Matchmaking", state.auto_accept_matchmaking);
    draw_feature_toggle("Reveal Enemy Chat", state.reveal_enemy_chat);
    draw_feature_toggle("Filter Console Output", state.filter_console_output);
    gui.row_multibox("Filter Chat Messages", filter_chat_modes, &state.filter_chat_messages,
                     IM_ARRAYSIZE(filter_chat_modes));
    gui.end_group_box();

    gui.end_grid();
}
