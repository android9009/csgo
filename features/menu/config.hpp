#pragma once
#include "../../math/color.hpp"
struct config_t {
    bool player_esp = true;
    bool player_names = true;
    bool bunnyhop = false;
    bool show_menu = false;
    Color enemy_color{235, 80, 80, 255};
    Color friendly_color{80, 170, 235, 255};
    int menu_key = 0x2D; // VK_INSERT
};
namespace Config { extern config_t settings; }
