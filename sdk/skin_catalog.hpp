#pragma once
#include "skin_icon_path.hpp"
#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// CS:GO item sets identify weapon finishes as "[paint_name]weapon_name".
// Build a per-weapon catalogue from the installed game's schema, not a fixed
// online list for a potentially different version of the game.
namespace SkinCatalog {
using Catalog = std::map<std::string, std::vector<std::string>>;
inline bool NameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}
inline Catalog Parse(std::string_view schema) {
    Catalog out;
    std::size_t pos = 0;
    while ((pos = schema.find('[', pos)) != std::string_view::npos) {
        const auto begin = ++pos;
        while (pos < schema.size() && NameChar(schema[pos]) && pos - begin <= 128) ++pos;
        if (pos == begin || pos >= schema.size() || schema[pos] != ']') continue;
        const auto paint = schema.substr(begin, pos - begin);
        const auto weapon_begin = ++pos;
        constexpr std::string_view prefix = "weapon_";
        if (schema.substr(pos, prefix.size()) != prefix) continue;
        while (pos < schema.size() && NameChar(schema[pos]) && pos - weapon_begin <= 128) ++pos;
        const auto weapon = schema.substr(weapon_begin, pos - weapon_begin);
        if (paint.size() <= 128 && weapon.size() <= 128 && SkinIconPath::ValidName(paint) && SkinIconPath::ValidName(weapon))
            out[std::string(weapon)].emplace_back(paint);
    }
    for (auto& [weapon, paints] : out) {
        std::sort(paints.begin(), paints.end());
        paints.erase(std::unique(paints.begin(), paints.end()), paints.end());
    }
    return out;
}
}
