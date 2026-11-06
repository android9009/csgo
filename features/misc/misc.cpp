// Darkside UI page only; no gameplay implementation.
#include "../../menu/menu.hpp"
#include "../../sdk/config_system/config_system.hpp"
#include "../../menu/shader_bg.hpp"

namespace {
struct misc_visual_only_state {
    bool jump_bug = false;
    bool edge_quick_stop = false;
    bool standalone_quick_stop = false;
    bool strafe_assist = false;
    bool edge_jump = false;
    bool slow_walk = false;
    bool fast_ladder = false;

    bool quick_switch = false;
    bool super_toss = false;
    bool knife_bot = false;
    bool prevent_afk_kick = false;
    bool hit_sound = false;
    bool automatic_purchase = false;
    bool automatic_grenade_release = false;
    bool auto_accept_matchmaking = false;
    bool disable_profanity_filter = false;
    bool reveal_enemy_chat = false;
    bool filter_console_output = false;

    int filter_chat_messages = 0;
    int log_events = 0;
    color_value log_color{
        ImVec4(0.66f, 0.60f, 1.f, 1.f),
        ImVec4(0.66f, 0.60f, 1.f, 1.f),
        false
    };
};

void draw_visual_only_toggle(const char* label, bool& value, bool with_options = false) {
    if (with_options) {
        const std::string id = std::string("##misc_") + label + "_options";
        gui.row_begin();
        // Reference-only entries deliberately have no settings or gameplay implementation.
        if (gui.row_options_begin(id.c_str(), 216.f, c_gui::row_control::toggle))
            gui.row_options_end();
    }
    gui.row_checkbox(label, &value);
}
}

void c_menu::draw_misc() {
    static misc_visual_only_state state;
    static const char* strafe_modes[] = { "Legit", "Directional", "Rage" };
    static const char* filter_chat_modes[] = { "Select" };
    static const char* log_event_modes[] = { "Damage Dealt" };

    auto& misc = g_cfg->misc;
    gui.begin_grid(2);

    gui.grid_col(0);
    gui.grid_box("MOVEMENT", k_auto_height);
    gui.row_checkbox("Bunny Hop", &misc.m_bunny_hop);

    gui.row_begin();
    if (gui.row_options_begin("##misc_air_strafe_options", 250.f, c_gui::row_control::toggle)) {
        gui.row_begin(); ImGui::Combo("Strafe Type", &misc.m_strafe_type, strafe_modes, IM_ARRAYSIZE(strafe_modes));
        gui.row_begin(); ImGui::SliderInt("Smooth", &misc.m_strafe_smooth, 0, 100);
        gui.row_options_end();
    }
    gui.row_checkbox("Air Strafe", &misc.m_auto_strafe);

    draw_visual_only_toggle("Jump Bug", state.jump_bug, true);
    draw_visual_only_toggle("Edge Quick Stop", state.edge_quick_stop);
    draw_visual_only_toggle("Standalone Quick Stop", state.standalone_quick_stop);
    draw_visual_only_toggle("Strafe Assist", state.strafe_assist);
    draw_visual_only_toggle("Edge Jump", state.edge_jump);
    draw_visual_only_toggle("Slow Walk", state.slow_walk, true);
    draw_visual_only_toggle("Fast Ladder", state.fast_ladder);
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
    gui.grid_box("FEATURES", 0, true);
    draw_visual_only_toggle("Quick Switch", state.quick_switch);
    draw_visual_only_toggle("Super Toss", state.super_toss);
    draw_visual_only_toggle("Knife Bot", state.knife_bot, true);
    draw_visual_only_toggle("Prevent AFK Kick", state.prevent_afk_kick);
    draw_visual_only_toggle("Hit Sound", state.hit_sound, true);
    draw_visual_only_toggle("Automatic Purchase", state.automatic_purchase, true);
    draw_visual_only_toggle("Automatic Grenade Release", state.automatic_grenade_release, true);
    draw_visual_only_toggle("Auto-Accept Matchmaking", state.auto_accept_matchmaking);
    draw_visual_only_toggle("Disable Profanity Filter", state.disable_profanity_filter);
    draw_visual_only_toggle("Reveal Enemy Chat", state.reveal_enemy_chat);
    draw_visual_only_toggle("Filter Console Output", state.filter_console_output);
    gui.row_begin(); ImGui::Combo("Filter Chat Messages", &state.filter_chat_messages,
                                  filter_chat_modes, IM_ARRAYSIZE(filter_chat_modes));
    gui.row_combo("Log Events", &state.log_color, &state.log_events,
                  log_event_modes, IM_ARRAYSIZE(log_event_modes));
    gui.end_group_box();

    gui.end_grid();
}
