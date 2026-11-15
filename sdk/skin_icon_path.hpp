#pragma once
#include <string>
#include <string_view>

// Inventory icons are supplied by CS:GO's GAME filesystem (including VPKs).
// Names are internal item/schema names, not translated display strings.
namespace SkinIconPath {
inline bool ValidName(std::string_view name) {
    if (name.empty()) return false;
    for (const char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}
inline std::string Base(std::string_view weapon) {
    if (!ValidName(weapon)) return {};
    return "resource/flash/econ/weapons/base_weapons/" + std::string(weapon) + ".png";
}
inline std::string Generated(std::string_view weapon, std::string_view paint) {
    if (!ValidName(weapon) || !ValidName(paint)) return {};
    return "resource/flash/econ/default_generated/" + std::string(weapon) + "_" + std::string(paint) + "_light_large.png";
}
}
