// Darkside UI page only; no gameplay implementation.
#include "../../menu/menu.hpp"
#include "../../sdk/config_system/config_system.hpp"
#include "../../menu/menu_shared.hpp"
#include <cstring>

void c_menu::draw_configs() {
    static char config_name[64] = "default";

    menu_shared::refresh_config_selection();

    gui.begin_grid(2);
    gui.grid_col(0);
    gui.grid_box("CONFIGS", 340.f, true);
    for (const auto& file : g_config_system->config_files) {
        if (ImGui::Selectable(file.c_str(), file == menu_shared::selected_config)) {
            menu_shared::selected_config = file;
            strncpy_s(config_name, file.c_str(), sizeof(config_name) - 1);
        }
    }
    gui.end_group_box();

    gui.grid_col(1);
    gui.grid_box("ACTIONS", k_auto_height);
    gui.row_begin(); ImGui::InputText("Name", config_name, sizeof(config_name));
    gui.row_begin();
    if (ImGui::Button("Create", ImVec2(-1.f, 30.f)) && config_name[0]) {
        menu_shared::save_config(config_name);
        menu_shared::refresh_config_selection();
        menu_shared::selected_config = config_name;
    }
    gui.row_begin();
    if (ImGui::Button("Load", ImVec2(-1.f, 30.f)) && config_name[0]) {
        menu_shared::load_config(config_name);
        menu_shared::selected_config = config_name;
    }
    gui.row_begin();
    if (ImGui::Button("Save", ImVec2(-1.f, 30.f)) && config_name[0]) {
        menu_shared::save_config(config_name);
        menu_shared::selected_config = config_name;
    }
    gui.row_begin();
    if (ImGui::Button("Delete", ImVec2(-1.f, 30.f)) && config_name[0]) {
        g_config_system->remove(config_name);
        if (menu_shared::selected_config == config_name)
            menu_shared::selected_config.clear();
        menu_shared::refresh_config_selection();
        config_name[0] = '\0';
    }
    gui.end_group_box();
    gui.end_grid();
}
