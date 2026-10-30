#pragma once

#include <string>

namespace menu_shared {
extern std::string selected_config;

void refresh_config_selection();
void save_config(const std::string& name);
void load_config(const std::string& name);
}
