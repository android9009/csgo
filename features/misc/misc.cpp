#include "dependencies/gameplay.hpp"

namespace misc_gameplay {
std::string purchase_command(const c_config_system::misc_t& misc) {
    std::string command;
    const bool configured = misc.m_buy_bot_primary != 0 || misc.m_buy_bot_secondary != 0 ||
        misc.m_buy_bot_armor || misc.m_buy_bot_defuser || misc.m_buy_bot_grenades;
    if (!configured) {
        return "buy ak47; buy m4a1_silencer; buy deagle; buy vesthelm; buy hegrenade; buy smokegrenade; buy defuser";
    }

    switch (misc.m_buy_bot_primary) {
    case 1: command += "buy scar20; buy g3sg1; "; break;
    case 2: command += "buy ssg08; "; break;
    case 3: command += "buy awp; "; break;
    case 4: command += "buy negev; "; break;
    case 5: command += "buy m249; "; break;
    case 6: command += "buy ak47; buy m4a1; buy m4a1_silencer; "; break;
    case 7: command += "buy aug; buy sg556; "; break;
    default: break;
    }
    switch (misc.m_buy_bot_secondary) {
    case 1: command += "buy elite; "; break;
    case 2: command += "buy p250; "; break;
    case 3: command += "buy tec9; buy fn57; "; break;
    case 4: command += "buy deagle; buy revolver; "; break;
    default: break;
    }
    if (misc.m_buy_bot_armor)
        command += "buy vesthelm; buy vest; ";
    if (misc.m_buy_bot_grenades)
        command += "buy hegrenade; buy molotov; buy incgrenade; buy smokegrenade; buy flashbang; ";
    if (misc.m_buy_bot_defuser)
        command += "buy defuser; ";
    return command;
}

bool quick_switch_weapon(std::string_view weapon) {
    return weapon == "weapon_awp" || weapon == "weapon_ssg08" ||
        weapon == "weapon_scar20" || weapon == "weapon_g3sg1" || weapon == "weapon_taser";
}
} // namespace misc_gameplay
