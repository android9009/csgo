#pragma once
// Misc -> Buy Bot feature bridge.
// Maps the menu's weapon selections onto airflow's buybot enum values
// (see buy_bot.cpp switch tables) and the other_items bitmask
// { vesthelm=1, vest=2, he=4, fire=8, smoke=16, taser=32, defuser=64 }.
#include "features/airflow_bridge.hpp"
namespace Misc {
namespace Buybot {
struct Settings {
    int primary{};   // cfg_t::buybot_primary_*
    int pistol{};    // cfg_t::buybot_pistol_*
    int armor{};     // cfg_t::buybot_kevlar_*
    bool defuser{};
    bool taser{};
    bool grenades{};
};
inline int MapPrimary(int v) {
    switch (v) {
    case 1: return 6;  // ak47
    case 2: return 7;  // sg556
    case 3: return 3;  // awp
    case 4: return 2;  // ssg08
    case 5: return 1;  // scar20
    case 6: return 4;  // negev
    default: return 0;
    }
}
inline int MapPistol(int v) {
    switch (v) {
    case 1: return 4;  // deagle
    case 2: return 1;  // elites
    case 3: return 3;  // tec9
    case 4: return 2;  // p250
    case 6: return 4;  // revolver (shares the deagle group)
    default: return 0; // none / cz75 (no airflow group)
    }
}
inline void Publish(const Settings& s) {
    const int main_w = MapPrimary(s.primary);
    const int second_w = MapPistol(s.pistol);
    unsigned other = 0;
    if (s.armor == 2) other |= 1;          // kevlar + helmet
    else if (s.armor == 1) other |= 2;     // kevlar
    if (s.grenades) other |= 4;
    if (s.taser) other |= 32;
    if (s.defuser) other |= 64;
    auto& b = g_cfg.misc.buybot;
    b.main_weapon = main_w;
    b.second_weapon = second_w;
    b.other_items = other;
    b.enable = (main_w != 0 || second_w != 0 || other != 0);
}
}
}
