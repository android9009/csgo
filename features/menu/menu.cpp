#include "menu.hpp"
#include "config.hpp"
#include "../../utils/render.hpp"
#include "../../utils/config.hpp"
#include "../../math/color.hpp"
#include <windows.h>
namespace Menu {
static bool open = false;
void Register() { Config::Load(); }
void Toggle() { open = !open; Config::settings.show_menu = open; }
bool IsOpen() { return open; }
void OnKey(int key) {
    if (key == Config::settings.menu_key) Toggle();
    if (!open) return;
    if (key == VK_F1) Config::settings.player_esp = !Config::settings.player_esp;
    if (key == VK_F2) Config::settings.player_names = !Config::settings.player_names;
    if (key == VK_F3) Config::settings.bunnyhop = !Config::settings.bunnyhop;
    if (key == VK_F5) Config::Save();
    if (key == VK_F6) Config::Load();
}
void Render() {
    if (!open) return;
    ::Render::FilledRect(40, 50, 300, 200, Color(20, 23, 30, 235));
    ::Render::OutlinedRect(40, 50, 300, 200, Color(115, 145, 210, 255));
    ::Render::Text(54, 60, Color(235, 240, 255), "CS:GO clean base");
    ::Render::Text(54, 90, Color(220, 220, 230), Config::settings.player_esp ? "F1  Player ESP: ON" : "F1  Player ESP: OFF");
    ::Render::Text(54, 112, Color(220, 220, 230), Config::settings.player_names ? "F2  Player labels: ON" : "F2  Player labels: OFF");
    ::Render::Text(54, 134, Color(220, 220, 230), Config::settings.bunnyhop ? "F3  Bunnyhop: ON" : "F3  Bunnyhop: OFF");
    ::Render::Text(54, 170, Color(170, 185, 210), "F5 save config  |  F6 load config");
    ::Render::Text(54, 215, Color(150, 155, 170), "INSERT close");
}
}
