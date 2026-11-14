#pragma once

// UI weapon IDs. Their order is also the order of glyphs in weapon_icons_atlas.hpp.
enum weapon_id_t : int {
    UI_WEAPON_P2000 = 0,
    UI_WEAPON_USP_S,
    UI_WEAPON_GLOCK_18,
    UI_WEAPON_P250,
    UI_WEAPON_CZ75_AUTO,
    UI_WEAPON_FIVE_SEVEN,
    UI_WEAPON_TEC_9,
    UI_WEAPON_DUAL_BERETTAS,
    UI_WEAPON_DESERT_EAGLE,
    UI_WEAPON_R8_REVOLVER,
    UI_WEAPON_GALIL_AR,
    UI_WEAPON_FAMAS,
    UI_WEAPON_AK_47,
    UI_WEAPON_M4A4,
    UI_WEAPON_M4A1_S,
    UI_WEAPON_SG_553,
    UI_WEAPON_AUG,
    UI_WEAPON_SSG_08,
    UI_WEAPON_AWP,
    UI_WEAPON_G3SG1,
    UI_WEAPON_SCAR_20,
    UI_WEAPON_MP7,
    UI_WEAPON_MP9,
    UI_WEAPON_MAC_10,
    UI_WEAPON_UMP_45,
    UI_WEAPON_MP5_SD,
    UI_WEAPON_P90,
    UI_WEAPON_PP_BIZON,
    UI_WEAPON_NOVA,
    UI_WEAPON_XM1014,
    UI_WEAPON_MAG_7,
    UI_WEAPON_SAWED_OFF,
    UI_WEAPON_M249,
    UI_WEAPON_NEGEV,
    UI_WEAPON_ZEUS_X27,
    UI_WEAPON_COUNT
};

inline constexpr const char* weapon_names[UI_WEAPON_COUNT] = {
    "P2000", "USP-S", "Glock-18", "P250", "CZ75-Auto", "Five-SeveN", "Tec-9",
    "Dual Berettas", "Desert Eagle", "R8 Revolver",
    "Galil AR", "FAMAS", "AK-47", "M4A4", "M4A1-S", "SG 553", "AUG", "SSG 08",
    "AWP", "G3SG1", "SCAR-20",
    "MP7", "MP9", "MAC-10", "UMP-45", "MP5-SD", "P90", "PP-Bizon", "Nova",
    "XM1014", "MAG-7", "Sawed-Off", "M249", "Negev", "Zeus x27"
};

enum weapon_class_t : int {
    UI_WEAPON_CLASS_PISTOLS = 0,
    UI_WEAPON_CLASS_RIFLES,
    UI_WEAPON_CLASS_MID_TIER,
    UI_WEAPON_CLASS_OTHER,
    UI_WEAPON_CLASS_COUNT
};

inline constexpr const char* weapon_class_names[UI_WEAPON_CLASS_COUNT] = {
    "Pistols", "Rifles", "Mid-Tier", "Other"
};

inline constexpr int weapon_class[UI_WEAPON_COUNT] = {
    UI_WEAPON_CLASS_PISTOLS, UI_WEAPON_CLASS_PISTOLS, UI_WEAPON_CLASS_PISTOLS, UI_WEAPON_CLASS_PISTOLS,
    UI_WEAPON_CLASS_PISTOLS, UI_WEAPON_CLASS_PISTOLS, UI_WEAPON_CLASS_PISTOLS, UI_WEAPON_CLASS_PISTOLS,
    UI_WEAPON_CLASS_PISTOLS, UI_WEAPON_CLASS_PISTOLS,
    UI_WEAPON_CLASS_RIFLES, UI_WEAPON_CLASS_RIFLES, UI_WEAPON_CLASS_RIFLES, UI_WEAPON_CLASS_RIFLES,
    UI_WEAPON_CLASS_RIFLES, UI_WEAPON_CLASS_RIFLES, UI_WEAPON_CLASS_RIFLES, UI_WEAPON_CLASS_RIFLES,
    UI_WEAPON_CLASS_RIFLES, UI_WEAPON_CLASS_RIFLES, UI_WEAPON_CLASS_RIFLES,
    UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_MID_TIER,
    UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_MID_TIER,
    UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_MID_TIER,
    UI_WEAPON_CLASS_MID_TIER, UI_WEAPON_CLASS_OTHER
};

enum weapon_preset_category_t : int {
    UI_WEAPON_PRESET_CUSTOM = 0,
    UI_WEAPON_PRESET_AUTOSNIPERS,
    UI_WEAPON_PRESET_SSG_08,
    UI_WEAPON_PRESET_AWP,
    UI_WEAPON_PRESET_DESERT_EAGLE,
    UI_WEAPON_PRESET_R8_REVOLVER,
    UI_WEAPON_PRESET_PISTOLS,
    UI_WEAPON_PRESET_RIFLES,
    UI_WEAPON_PRESET_SMGS,
    UI_WEAPON_PRESET_SHOTGUNS,
    UI_WEAPON_PRESET_MACHINEGUNS,
    UI_WEAPON_PRESET_TASER,
    // Migration label for the old fixed Deagle/R8 group; not offered by Create Group.
    UI_WEAPON_PRESET_HEAVY_PISTOL,
    UI_WEAPON_PRESET_CATEGORY_COUNT
};

inline constexpr const char* weapon_preset_category_names[UI_WEAPON_PRESET_CATEGORY_COUNT] = {
    "Unnamed", "Autosnipers", "SSG-08", "AWP", "Desert Eagle", "R8 Revolver", "Pistols",
    "Rifles", "SMGs", "Shotguns", "Machineguns", "Taser", "Heavy Pistol"
};

inline constexpr int weapon_preset_category_icons[UI_WEAPON_PRESET_CATEGORY_COUNT] = {
    UI_WEAPON_P2000, UI_WEAPON_G3SG1, UI_WEAPON_SSG_08, UI_WEAPON_AWP, UI_WEAPON_DESERT_EAGLE,
    UI_WEAPON_R8_REVOLVER, UI_WEAPON_P2000, UI_WEAPON_AK_47, UI_WEAPON_MP9, UI_WEAPON_NOVA,
    UI_WEAPON_NEGEV, UI_WEAPON_ZEUS_X27, UI_WEAPON_DESERT_EAGLE
};

inline bool weapon_preset_category_has_weapon(int category, int weapon) {
    if (weapon < 0 || weapon >= UI_WEAPON_COUNT)
        return false;
    switch (category) {
        case UI_WEAPON_PRESET_AUTOSNIPERS: return weapon == UI_WEAPON_G3SG1 || weapon == UI_WEAPON_SCAR_20;
        case UI_WEAPON_PRESET_SSG_08: return weapon == UI_WEAPON_SSG_08;
        case UI_WEAPON_PRESET_AWP: return weapon == UI_WEAPON_AWP;
        case UI_WEAPON_PRESET_DESERT_EAGLE: return weapon == UI_WEAPON_DESERT_EAGLE;
        case UI_WEAPON_PRESET_R8_REVOLVER: return weapon == UI_WEAPON_R8_REVOLVER;
        case UI_WEAPON_PRESET_PISTOLS: return weapon >= UI_WEAPON_P2000 && weapon <= UI_WEAPON_R8_REVOLVER;
        case UI_WEAPON_PRESET_RIFLES: return weapon >= UI_WEAPON_GALIL_AR && weapon <= UI_WEAPON_AUG;
        case UI_WEAPON_PRESET_SMGS: return weapon >= UI_WEAPON_MP7 && weapon <= UI_WEAPON_PP_BIZON;
        case UI_WEAPON_PRESET_SHOTGUNS: return weapon >= UI_WEAPON_NOVA && weapon <= UI_WEAPON_SAWED_OFF;
        case UI_WEAPON_PRESET_MACHINEGUNS: return weapon == UI_WEAPON_M249 || weapon == UI_WEAPON_NEGEV;
        case UI_WEAPON_PRESET_TASER: return weapon == UI_WEAPON_ZEUS_X27;
        case UI_WEAPON_PRESET_HEAVY_PISTOL:
            return weapon == UI_WEAPON_DESERT_EAGLE || weapon == UI_WEAPON_R8_REVOLVER;
        default: return false;
    }
}
