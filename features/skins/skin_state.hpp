#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string_view>

// Menu and frame hook share only copied values; never expose GUI-owned strings.
namespace SkinState {
struct Choice {
    // -2 keeps the original item's counter, -1 disables it, >=0 is static.
    int paint_kit{}, seed{}, stattrak{-2};
    float wear{};
    bool souvenir{};
    std::array<char, 32> name{};
    std::uint64_t revision{};
};
inline std::mutex mutex;
inline std::map<int, Choice> teams[2];
inline std::uint64_t version{};
inline int Definition(std::string_view name) {
    struct Item { std::string_view name; int id; };
    static constexpr Item items[] = {
        {"weapon_deagle",1},{"weapon_elite",2},{"weapon_fiveseven",3},{"weapon_glock",4},
        {"weapon_ak47",7},{"weapon_aug",8},{"weapon_awp",9},{"weapon_famas",10},
        {"weapon_g3sg1",11},{"weapon_galilar",13},{"weapon_m249",14},{"weapon_m4a1",16},
        {"weapon_mac10",17},{"weapon_p90",19},{"weapon_mp5sd",23},{"weapon_ump45",24},
        {"weapon_xm1014",25},{"weapon_bizon",26},{"weapon_mag7",27},{"weapon_negev",28},
        {"weapon_sawedoff",29},{"weapon_tec9",30},{"weapon_hkp2000",32},{"weapon_mp7",33},
        {"weapon_mp9",34},{"weapon_nova",35},{"weapon_p250",36},{"weapon_scar20",38},
        {"weapon_sg556",39},{"weapon_ssg08",40},{"weapon_m4a1_silencer",60},
        {"weapon_usp_silencer",61},{"weapon_cz75a",63},{"weapon_revolver",64}
    };
    for (const auto& item : items) if (item.name == name) return item.id;
    return 0;
}
inline bool Set(bool ct, int definition, Choice choice) {
    if (definition <= 0 || choice.paint_kit <= 0 || choice.seed < 0 || choice.seed > 1000 ||
        !(choice.wear >= 0.f && choice.wear <= 1.f) || choice.stattrak < -2 || choice.stattrak > 99999 ||
        (choice.souvenir && choice.stattrak >= 0)) return false;
    choice.name.back() = '\0';
    std::lock_guard<std::mutex> lock(mutex);
    choice.revision = ++version;
    teams[ct ? 0 : 1][definition] = choice;
    return true;
}
inline void Remove(bool ct, int definition) {
    std::lock_guard<std::mutex> lock(mutex);
    teams[ct ? 0 : 1].erase(definition); ++version;
}
inline bool Empty() {
    std::lock_guard<std::mutex> lock(mutex);
    return teams[0].empty() && teams[1].empty();
}
inline bool Get(bool ct, int definition, Choice& out) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto& choices = teams[ct ? 0 : 1];
    const auto it = choices.find(definition);
    if (it == choices.end()) return false;
    out = it->second; return true;
}
}
