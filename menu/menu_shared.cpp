#include "menu_shared.hpp"

#include "binds.hpp"
#include "../sdk/config_system/config_system.hpp"

#include <algorithm>

namespace menu_shared {
std::string selected_config;

void refresh_config_selection() {
    g_config_system->refresh();
    const auto found = std::find(g_config_system->config_files.begin(),
        g_config_system->config_files.end(), selected_config);
    if (found == g_config_system->config_files.end()) {
        selected_config = g_config_system->config_files.empty()
            ? std::string{} : g_config_system->config_files.front();
    }
}

void save_config(const std::string& name) {
    menu_binds::before_config_save();
    g_config_system->save(name);
    menu_binds::after_config_save();
}

void load_config(const std::string& name) {
    menu_binds::before_config_save();
    g_config_system->read(name);
    menu_binds::on_config_loaded();
}
} // namespace menu_shared
