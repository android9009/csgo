// Darkside UI page only; no gameplay implementation.
#include "../../menu/menu.hpp"
#include "../../sdk/config_system/config_system.hpp"

void c_menu::draw_rage() {
    static const char* hitbox_names[] = { "Head", "Chest", "Stomach", "Arms", "Legs", "Feet" };
    static const char* auto_stop_names[] = { "Early", "Full Stop", "Between Shots", "Air", "Duck" };
    static const char* safety_modes[] = { "Off", "Prefer", "Force" };

    auto& rage = g_cfg->rage_bot;
    rage.m_selected_group = ImClamp(rage.m_selected_group, 0, RAGE_WEAPON_PRESET_MAX - 1);
    if (rage.m_selected_group != 0 && !rage.m_weapon_config[rage.m_selected_group].m_override_general)
        rage.m_selected_group = 0;
    auto& cfg = rage.m_weapon_config[rage.m_selected_group];

    // Neverlose layout: MAIN + ACCURACY on the left, SELECTION + SAFETY on the right. Anti-Aim
    // has its own tab. The options gear sits in the slot left of the control it belongs to; its card holds the
    // settings that have no row of their own. The preset being edited is chosen from the
    // Weapon Presets selector in the top bar.
    gui.begin_grid(2);

    // ========== LEFT COLUMN ==========
    gui.grid_col(0);

    gui.grid_box("MAIN", k_auto_height);

    // Enabled (gear: Silent / Extrapolate / Backtrack)
    gui.row_begin();
    if (gui.row_options_begin("##enable_opts", 250.f, c_gui::row_control::toggle)) {
        gui.row_checkbox("Silent", &rage.m_silent);
        gui.row_checkbox("Backtrack", &cfg.m_backtrack);
        gui.row_begin(); ImGui::SliderInt("Extrapolate", &cfg.m_extrapolate, 0, 16, "%d ticks");
        gui.row_options_end();
    }
    gui.row_checkbox("Enabled", &rage.m_enabled);

    // Peek Assist (gear: Style / Auto Stop / Retreat Mode / Max Distance)
    static const char* peek_style[]    = { "Static", "Gradient", "Dynamic" };
    static const char* peek_autostop[] = { "Select", "Early", "Full Stop" };
    static const char* peek_retreat[]  = { "Off", "On Shot", "On Hurt" };
    gui.row_begin();
    if (gui.row_options_begin("##peek_opts", 250.f, c_gui::row_control::toggle)) {
        gui.row_begin(); ImGui::Combo("Style", &rage.m_peek_assist_style, peek_style, IM_ARRAYSIZE(peek_style));
        gui.row_begin(); ImGui::Combo("Auto Stop", &rage.m_peek_assist_auto_stop, peek_autostop, IM_ARRAYSIZE(peek_autostop));
        gui.row_begin(); ImGui::Combo("Retreat Mode", &rage.m_peek_assist_retreat, peek_retreat, IM_ARRAYSIZE(peek_retreat));
        gui.row_begin(); ImGui::SliderInt("Max Distance", &rage.m_peek_assist_max_distance, 0, 100,
                                          rage.m_peek_assist_max_distance >= 100 ? "Inf." : "%d m");
        gui.row_options_end();
    }
    gui.row_checkbox("Peek Assist", &rage.m_peek_assist);

    // New gears are intentionally empty for now.
    gui.row_begin();
    if (gui.row_options_begin("##hide_shots_opts", 72.f, c_gui::row_control::toggle))
        gui.row_options_end();
    gui.row_checkbox("Hide Shots", &rage.m_hide_shots);

    gui.row_begin();
    if (gui.row_options_begin("##double_tap_opts", 72.f, c_gui::row_control::toggle))
        gui.row_options_end();
    gui.row_checkbox("Double Tap", &rage.m_double_tap);

    gui.row_begin(); ImGui::SliderInt("Field of View", &rage.m_fov, 0, 180, "%d\xc2\xb0");
    gui.end_group_box();

    gui.grid_box("ACCURACY", k_auto_height);

    gui.row_checkbox("Auto Scope", &cfg.m_auto_scope);

    // Auto Stop (gear: Modes multibox)
    bool auto_stop_modes[5] = { cfg.m_auto_stop_early, cfg.m_auto_stop_full, cfg.m_auto_stop_between,
                                cfg.m_auto_stop_air, cfg.m_auto_stop_duck };
    gui.row_begin();
    if (gui.row_options_begin("##autostop_opts", 250.f, c_gui::row_control::toggle)) {
        gui.row_multibox("Modes", auto_stop_names, auto_stop_modes, 5, "Select auto stop modes.");
        gui.row_options_end();
    }
    gui.row_checkbox("Auto Stop", &cfg.m_auto_stop);
    cfg.m_auto_stop_early = auto_stop_modes[0];
    cfg.m_auto_stop_full = auto_stop_modes[1];
    cfg.m_auto_stop_between = auto_stop_modes[2];
    cfg.m_auto_stop_air = auto_stop_modes[3];
    cfg.m_auto_stop_duck = auto_stop_modes[4];
    gui.end_group_box();

    // ========== RIGHT COLUMN ==========
    gui.grid_col(1);

    gui.grid_box("SELECTION", k_auto_height);

    gui.row_multibox("Hitboxes", hitbox_names, cfg.m_hitboxes, 6, "Select target hitboxes.");

    // Multipoint (gear: Head Scale / Body Scale)
    gui.row_begin();
    if (gui.row_options_begin("##multipoint_opts", 250.f, c_gui::row_control::box)) {
        gui.row_begin(); ImGui::SliderInt("Head Scale", &cfg.m_head_scale, -1, 100, 
            cfg.m_head_scale == -1 ? "Auto" : "%d%%");
        gui.row_begin(); ImGui::SliderInt("Body Scale", &cfg.m_body_scale, -1, 100,
            cfg.m_body_scale == -1 ? "Auto" : "%d%%");
        gui.row_options_end();
    }
    gui.row_multibox("Multipoint", hitbox_names, cfg.m_multi_points, 6, "Select multipoint locations.");

    // Hit Chance (gear: DT / extrapolation chance and Force Shot)
    gui.row_begin();
    if (gui.row_options_begin("##hitchance_opts", 300.f, c_gui::row_control::slider)) {
        gui.row_begin(); ImGui::SliderInt("DT HitChance", &cfg.m_dt_hit_chance, 0, 100, "%d%%");
        gui.row_begin(); ImGui::SliderInt("Extrapolation HitChance", &cfg.m_extrapolation_hit_chance, 0, 100, "%d%%");
        gui.row_checkbox("Force Shot", &cfg.m_force_shot);
        gui.animated_row("##force_shot_minimum", cfg.m_force_shot, [&] {
            ImGui::SliderInt("Minimum HitChance", &cfg.m_minimum_hit_chance, 0, 100, "%d%%");
        });
        gui.row_options_end();
    }
    gui.row_begin(); ImGui::SliderInt("Hit Chance", &cfg.m_hit_chance, -1, 100, 
        cfg.m_hit_chance == -1 ? "Auto" : "%d%%");

    // Min. Damage (gear: Delay Shot)
    gui.row_begin();
    if (gui.row_options_begin("##mindmg_opts", 216.f, c_gui::row_control::slider)) {
        gui.row_checkbox("Delay Shot", &cfg.m_delay_shot);
        gui.row_options_end();
    }
    gui.row_begin(); ImGui::SliderInt("Min. Damage", &cfg.m_minimum_damage, -1, 120,
        cfg.m_minimum_damage == -1 ? "Auto" : "%d");

    gui.row_checkbox("Penetrate Walls", &cfg.m_penetration);
    gui.end_group_box();

    gui.grid_box("SAFETY", k_auto_height);

    gui.row_begin();
    if (gui.row_options_begin("##body_aim_opts", 72.f, c_gui::row_control::box))
        gui.row_options_end();
    gui.row_begin(); ImGui::Combo("Body Aim", &cfg.m_body_aim, safety_modes, IM_ARRAYSIZE(safety_modes));

    gui.row_begin();
    if (gui.row_options_begin("##safe_points_opts", 72.f, c_gui::row_control::box))
        gui.row_options_end();
    gui.row_begin(); ImGui::Combo("Safe Points", &cfg.m_safe_points, safety_modes, IM_ARRAYSIZE(safety_modes));

    gui.row_multibox("Force Safe Point Hitbox", hitbox_names, cfg.m_hitbox_safety, 6,
                     "Select hitboxes that require safe points.");
    gui.end_group_box();

    gui.end_grid();
}
