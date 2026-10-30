// Adapted for the standalone D3D9 GUI shell; CS2 umbrella dependencies removed.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "menu.hpp"
#include "menu_shared.hpp"
#include "hashes.hpp"
#include "notification.hpp"
#include "weapon_presets.hpp"
#include "shader_bg.hpp"
#include "binds.hpp"
#include <cmath>
#include "../sdk/config_system/config_system.hpp"
#include "../sdk/includes/imgui/imgui_internal.h"
#include "../runtime/steam_avatar.hpp"

namespace {
void switch_tab(int tab) {
    if (gui.m_tab == tab)
        return;
    gui.m_tab = tab;
    gui.m_anim = 0.f;
}

void register_bind_targets() {
    auto& rage = g_cfg->rage_bot;
    menu_binds::register_bool("rage.enabled", "Enabled", &rage.m_enabled);
    menu_binds::register_bool("rage.silent", "Silent", &rage.m_silent);
    
    // Register binds for ALL weapon groups
    for (int g = 0; g < RAGE_WEAPON_PRESET_MAX; ++g) {
        auto& weapon = rage.m_weapon_config[g];
        const std::string group_name = g < WEAPON_GROUP_MAX
            ? std::string(weapon_group_names[g]) : "Preset " + std::to_string(g);
        const std::string prefix = "rage." + group_name + ".";
        
        menu_binds::register_bool((prefix + "auto_stop").c_str(), "Auto Stop", &weapon.m_auto_stop);
        menu_binds::register_bool((prefix + "auto_scope").c_str(), "Auto Scope", &weapon.m_auto_scope);
        menu_binds::register_bool((prefix + "penetration").c_str(), "Penetration", &weapon.m_penetration);
        menu_binds::register_bool((prefix + "delay_shot").c_str(), "Delay Shot", &weapon.m_delay_shot);
        menu_binds::register_bool((prefix + "backtrack").c_str(), "Backtrack", &weapon.m_backtrack);
        
        for (int i = 0; i < 6; ++i) {
            static const char* hitbox_names[] = { "Head", "Chest", "Stomach", "Arms", "Legs", "Feet" };
            menu_binds::register_bool((prefix + "hitbox." + std::to_string(i)).c_str(), hitbox_names[i], &weapon.m_hitboxes[i]);
            menu_binds::register_bool((prefix + "multipoint." + std::to_string(i)).c_str(), hitbox_names[i], &weapon.m_multi_points[i]);
        }
        
        menu_binds::register_int((prefix + "hit_chance").c_str(), "Hit Chance", &weapon.m_hit_chance, -1, 100);
        menu_binds::register_int((prefix + "hit_chance_seed").c_str(), "Hit Chance Seed", &weapon.m_hit_chance_seed, 0, 256);
        menu_binds::register_int((prefix + "dt_hit_chance").c_str(), "DT HitChance", &weapon.m_dt_hit_chance, 0, 100);
        menu_binds::register_int((prefix + "extrapolation_hit_chance").c_str(), "Extrapolation HitChance", &weapon.m_extrapolation_hit_chance, 0, 100);
        menu_binds::register_bool((prefix + "force_shot").c_str(), "Force Shot", &weapon.m_force_shot);
        menu_binds::register_int((prefix + "minimum_hit_chance").c_str(), "Minimum HitChance", &weapon.m_minimum_hit_chance, 0, 100);
        menu_binds::register_int((prefix + "extrapolate").c_str(), "Extrapolate", &weapon.m_extrapolate, 0, 16);
        menu_binds::register_int((prefix + "minimum_damage").c_str(), "Min Damage", &weapon.m_minimum_damage, -1, 120);
        menu_binds::register_int((prefix + "head_scale").c_str(), "Head Scale", &weapon.m_head_scale, -1, 100);
        menu_binds::register_int((prefix + "body_scale").c_str(), "Body Scale", &weapon.m_body_scale, -1, 100);
    }

    auto& legit = g_cfg->legit_bot;
    menu_binds::register_bool("legit.enabled", "Enabled", &legit.m_enabled);
    menu_binds::register_float("legit.fov", "Field Of View", &legit.m_fov, 0.f, 180.f);
    menu_binds::register_float("legit.smooth", "Smooth", &legit.m_smooth, 1.f, 20.f);
    menu_binds::register_bool("legit.triggerbot", "Triggerbot", &legit.m_triggerbot_enabled);
    menu_binds::register_bool("legit.backtrack", "Backtrack", &legit.m_backtrack_enabled);
    menu_binds::register_int("legit.backtrack_ticks", "Backtrack Ticks", &legit.m_backtrack_ticks, 0, 15);

    auto& anti = g_cfg->anti_hit;
    menu_binds::register_int("antiaim.pitch_mode", "Pitch", &anti.m_pitch_mode, 0, 4);
    menu_binds::register_int("antiaim.pitch_amount", "Pitch Amount", &anti.m_pitch_amount, -89, 89);
    menu_binds::register_int("antiaim.yaw_mode", "Yaw", &anti.m_yaw_mode, 0, 4);
    menu_binds::register_int("antiaim.yaw_amount", "Yaw Add", &anti.m_yaw_amount, -180, 180);
    menu_binds::register_bool("antiaim.at_target", "At Target", &anti.m_at_target);

    auto& esp = g_cfg->visuals.m_player_esp;
    menu_binds::register_bool("visuals.enabled", "Player ESP", &esp.m_enabled);
    menu_binds::register_bool("visuals.teammates", "Teammates", &esp.m_teammates);
    menu_binds::register_bool("visuals.box", "Bounding Box", &esp.m_bounding_box);
    menu_binds::register_bool("visuals.name", "Name", &esp.m_name);
    menu_binds::register_bool("visuals.health", "Health", &esp.m_health_bar);
    menu_binds::register_bool("visuals.weapon", "Weapon", &esp.m_weapon);
    menu_binds::register_bool("visuals.skeleton", "Skeleton", &esp.m_skeleton);
    menu_binds::register_int("world.exposure", "Exposure", &g_cfg->world.m_exposure, 0, 100);
    menu_binds::register_float("world.brightness", "Brightness", &g_cfg->world.m_brightness, 0.f, 1.f);
    menu_binds::register_bool("world.remove_fog", "Remove Fog", &g_cfg->world.m_render_fog);
    menu_binds::register_bool("thirdperson.enabled", "Thirdperson", &g_cfg->world_esp.m_enable_thirdperson);
    menu_binds::register_int("thirdperson.distance", "Thirdperson Distance", &g_cfg->world_esp.m_distance, 50, 300);

    menu_binds::register_bool("misc.bunny_hop", "Bunny Hop", &g_cfg->misc.m_bunny_hop);
    menu_binds::register_bool("misc.auto_strafe", "Automatic Strafe", &g_cfg->misc.m_auto_strafe);
    menu_binds::register_int("misc.strafe_type", "Strafe Type", &g_cfg->misc.m_strafe_type, 0, 2);
    menu_binds::register_int("misc.strafe_smooth", "Strafe Smooth", &g_cfg->misc.m_strafe_smooth, 0, 100);
    menu_binds::register_bool("menu.shader", "Menu Shader", &shader_bg::state().enabled);
    menu_binds::register_float("menu.shader_opacity", "Shader Opacity", &shader_bg::state().opacity, 0.f, 1.f);

    menu_binds::register_int("skins.knife", "Knife Model", &g_cfg->skins.m_knives.m_selected, 0, 20);
    menu_binds::register_int("skins.gloves", "Glove Model", &g_cfg->skins.m_gloves.m_selected, 0, 20);
    menu_binds::register_int("skins.agent_ct", "Counter-Terrorist", &g_cfg->skins.m_agents.m_selected_ct, 0, 20);
    menu_binds::register_int("skins.agent_t", "Terrorist", &g_cfg->skins.m_agents.m_selected, 0, 20);
}

void draw_top_bar(ImGuiWindow* window) {
    const ImVec2 pos = window->Pos;
    const ImVec2 size = window->Size;

    ImGui::SetCursorPos(ImVec2(190.f, 15.f));
    const ImVec2 button_pos = ImGui::GetCursorScreenPos();

    if (gui.m_tab == 0)
        weapon_presets_ui::draw(button_pos);

    const int search_tab = gui.search_bar(pos + ImVec2(size.x - 15.f * gui.m_scale, 15.f));
    if (search_tab >= 0)
        switch_tab(search_tab);
}
}

void c_menu::init() {
    gui.apply_theme(0);
    gui.apply_style_colors();
    gui.user_name = "PASTAWARE";
    gui.user_status = "Till: Lifetime";
    gui.user_till = "Till: Lifetime";
    menu_shared::refresh_config_selection();
    register_bind_targets();

    m_initialization_overlay_active = true;
    m_initialization_overlay_time = 0.f;
}

void c_menu::draw_initialization_overlay() {
    if (!m_initialization_overlay_active)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.f || io.DisplaySize.y <= 0.f)
        return;

    m_initialization_overlay_time += ImMin(io.DeltaTime, 0.05f);
    const float elapsed = m_initialization_overlay_time;
    constexpr float fade_in_duration = 0.20f;
    constexpr float load_duration = 2.80f;
    constexpr float hold_duration = 0.55f;
    constexpr float fade_out_duration = 0.45f;
    constexpr float fade_out_start = load_duration + hold_duration;
    constexpr float total_duration = fade_out_start + fade_out_duration;
    if (elapsed >= total_duration) {
        m_initialization_overlay_active = false;
        return;
    }

    const auto smoothstep = [](float value) {
        value = ImClamp(value, 0.f, 1.f);
        return value * value * (3.f - 2.f * value);
    };

    float alpha = smoothstep(elapsed / fade_in_duration);
    if (elapsed > fade_out_start)
        alpha *= 1.f - smoothstep((elapsed - fade_out_start) / fade_out_duration);

    const float progress = ImClamp(elapsed / load_duration, 0.f, 1.f);
    const int stage = ImClamp(1 + static_cast<int>(progress * 10.f), 1, 10);
    const bool complete = progress >= 1.f;
    const std::string counter = std::to_string(stage) + " of 10";
    const char* status = complete ? "Initialization complete" : "Initialization in progress";

    constexpr float card_w = 330.f;
    constexpr float card_h = 70.f;
    constexpr float margin = 12.f;
    const float slide = (1.f - alpha) * 28.f;
    const ImVec2 card_min(io.DisplaySize.x - card_w - margin + slide, margin);
    const ImVec2 card_max = card_min + ImVec2(card_w, card_h);
    const int text_alpha = static_cast<int>(255.f * alpha);

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(card_min + ImVec2(0.f, 4.f), card_max + ImVec2(0.f, 4.f),
                        IM_COL32(0, 0, 0, static_cast<int>(90.f * alpha)), 16.f);
    draw->AddRectFilled(card_min, card_max,
                        IM_COL32(16, 18, 28, static_cast<int>(245.f * alpha)), 16.f);
    draw->AddRect(card_min, card_max,
                  IM_COL32(122, 132, 255, static_cast<int>(48.f * alpha)), 16.f);

    ImFontAtlas* atlas = io.Fonts;
    ImFont* body = atlas && atlas->Fonts.Size > 0 ? atlas->Fonts[0] : ImGui::GetFont();
    ImFont* bold = atlas && atlas->Fonts.Size > 4 ? atlas->Fonts[4] : body;
    ImFont* small = atlas && atlas->Fonts.Size > 7 ? atlas->Fonts[7] : body;

    constexpr float logo_size = 40.f;
    const ImVec2 logo_min = card_min + ImVec2(12.f, 15.f);
    const ImVec2 logo_max = logo_min + ImVec2(logo_size, logo_size);
    draw->AddRectFilled(logo_min, logo_max, IM_COL32(4, 6, 12, text_alpha), 10.f);
    draw->AddRect(logo_min, logo_max, IM_COL32(255, 255, 255, static_cast<int>(14.f * alpha)), 10.f);
    const ImVec2 logo_text_size = bold->CalcTextSizeA(14.f, FLT_MAX, 0.f, "PW");
    draw->AddText(bold, 14.f, logo_min + (ImVec2(logo_size, logo_size) - logo_text_size) * 0.5f,
                  IM_COL32(240, 242, 250, text_alpha), "PW");

    const float text_x = card_min.x + 64.f;
    draw->AddText(bold, 14.f, ImVec2(IM_ROUND(text_x), IM_ROUND(card_min.y + 10.f)),
                  IM_COL32(238, 240, 248, text_alpha), "PASTAWARE");
    const ImVec2 counter_size = small->CalcTextSizeA(12.f, FLT_MAX, 0.f, counter.c_str());
    draw->AddText(small, 12.f,
                  ImVec2(IM_ROUND(card_max.x - 12.f - counter_size.x), IM_ROUND(card_min.y + 12.f)),
                  IM_COL32(164, 168, 184, text_alpha), counter.c_str());
    draw->AddText(small, 13.f, ImVec2(IM_ROUND(text_x), IM_ROUND(card_min.y + 33.f)),
                  IM_COL32(137, 141, 158, text_alpha), status);

    const ImVec2 bar_min = card_min + ImVec2(12.f, card_h - 6.f);
    const ImVec2 bar_max = card_max - ImVec2(12.f, 3.f);
    draw->AddRectFilled(bar_min, bar_max, IM_COL32(58, 62, 80, static_cast<int>(140.f * alpha)), 1.5f);
    if (progress > 0.f) {
        const ImVec2 fill_max(ImLerp(bar_min.x, bar_max.x, progress), bar_max.y);
        draw->AddRectFilled(bar_min, fill_max, gui.accent_color.to_im_color(alpha, false), 1.5f);
    }
}

void c_menu::draw() {
    menu_binds::update();
    render();
    menu_binds::draw_overlay(m_opened);
    menu_binds::flush();
    draw_initialization_overlay();
}

void c_menu::render() {
    if (!m_opened)
        return;

    ImGuiIO& io = ImGui::GetIO();
    gui.apply_style_colors();

    static bool menu_pos_ready = false;
    static bool menu_dragging = false;
    static ImVec2 menu_pos{};
    static ImVec2 menu_target{};
    static ImVec2 menu_grab{};

    if (menu_pos_ready) {
        if (menu_dragging) {
            if (io.MouseDown[0])
                menu_target = io.MousePos - menu_grab;
            else
                menu_dragging = false;
        }
        menu_pos = ImLerp(menu_pos, menu_target,
                          1.f - expf(-36.f * ImMin(io.DeltaTime, 0.05f)));
        if (ImLengthSqr(menu_target - menu_pos) < 0.25f)
            menu_pos = menu_target;
        ImGui::SetNextWindowPos(menu_pos, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    }

    ImGui::SetNextWindowSize(ImVec2(800.f * gui.m_scale, 600.f * gui.m_scale), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    const bool visible = ImGui::Begin("DARKSIDE##neverlose", &m_opened, flags);
    if (visible) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImDrawList* draw = window->DrawList;
        const ImVec2 pos = window->Pos;
        const ImVec2 size = window->Size;

        gui.m_anim = ImLerp(gui.m_anim, 1.f, 0.045f);
        gui.name_plate(pos, 170.f);
        shader_bg::update(size, pos);

        draw->AddLine(pos + ImVec2(185.f, 60.f), pos + ImVec2(size.x - 15.f, 60.f), gui.border.to_im_color());
        draw->AddLine(pos + ImVec2(10.f, 60.f), pos + ImVec2(160.f, 60.f), gui.border.to_im_color());
        gui.user_profile(pos + ImVec2(10.f, size.y - 48.f), pos + ImVec2(160.f, size.y - 8.f),
                         steam_avatar::texture());

        ImGui::SetCursorPos(ImVec2(10.f, 70.f));
        ImGui::BeginChild("##tabs", ImVec2(150.f, size.y - 120.f), false);
        gui.group_title("AIMBOT");
        if (gui.tab(ICON_FA_CROSSHAIRS, "Rage", gui.m_tab == 0)) switch_tab(0);
        if (gui.tab(ICON_FA_SHIELD_ALT, "Anti-Aim", gui.m_tab == 1)) switch_tab(1);
        if (gui.tab(ICON_FA_MOUSE, "Legit", gui.m_tab == 2)) switch_tab(2);

        ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();
        gui.group_title("COMMON");
        if (gui.tab(ICON_FA_IMAGE, "Visuals", gui.m_tab == 3)) switch_tab(3);
        if (gui.tab(ICON_FA_SLIDERS_H, "Miscellaneous", gui.m_tab == 4)) switch_tab(4);

        ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();
        gui.group_title("CUSTOMIZATION");
        if (gui.tab(ICON_FA_PAINT_BRUSH, "Skins", gui.m_tab == 5)) switch_tab(5);
        if (gui.tab(ICON_FA_COG, "Config", gui.m_tab == 6)) switch_tab(6);
        ImGui::EndChild();

        draw_top_bar(window);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, gui.m_anim);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 8.f));
        ImGui::SetCursorPos(ImVec2(185.f, 81.f - 5.f * gui.m_anim));
        ImGui::BeginChild("##content", ImVec2(size.x - 200.f, size.y - 91.f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        static const char* tab_names[] = {
            "Rage", "Anti-Aim", "Legit", "Visuals", "Miscellaneous", "Skins", "Config"
        };
        gui.search_scope_begin(gui.m_tab, tab_names[gui.m_tab]);
        switch (gui.m_tab) {
            case 0: draw_rage(); break;
            case 1: draw_anti_aim(); break;
            case 2: draw_legit(); break;
            case 3: draw_visuals(); break;
            case 4: draw_misc(); break;
            case 5: draw_skins(); break;
            case 6: draw_configs(); break;
            default: switch_tab(0); break;
        }
        gui.search_scope_end();

        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        if (!menu_pos_ready) {
            menu_pos = menu_target = window->Pos;
            menu_pos_ready = true;
        }
        if (!menu_dragging && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_NoPopupHierarchy)) {
            menu_dragging = true;
            menu_grab = io.MousePos - window->Pos;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    gui.spotify_widget_draw();
    DrawNotifications();
}
