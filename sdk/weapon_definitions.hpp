#pragma once

// UI weapon IDs. Their order is also the order of glyphs in weapon_icons_atlas.hpp.
enum weapon_id_t : int {
    WEAPON_P2000 = 0,
    WEAPON_USP_S,
    WEAPON_GLOCK_18,
    WEAPON_P250,
    WEAPON_CZ75_AUTO,
    WEAPON_FIVE_SEVEN,
    WEAPON_TEC_9,
    WEAPON_DUAL_BERETTAS,
    WEAPON_DESERT_EAGLE,
    WEAPON_R8_REVOLVER,
    WEAPON_GALIL_AR,
    WEAPON_FAMAS,
    WEAPON_AK_47,
    WEAPON_M4A4,
    WEAPON_M4A1_S,
    WEAPON_SG_553,
    WEAPON_AUG,
    WEAPON_SSG_08,
    WEAPON_AWP,
    WEAPON_G3SG1,
    WEAPON_SCAR_20,
    WEAPON_MP7,
    WEAPON_MP9,
    WEAPON_MAC_10,
    WEAPON_UMP_45,
    WEAPON_MP5_SD,
    WEAPON_P90,
    WEAPON_PP_BIZON,
    WEAPON_NOVA,
    WEAPON_XM1014,
    WEAPON_MAG_7,
    WEAPON_SAWED_OFF,
    WEAPON_M249,
    WEAPON_NEGEV,
    WEAPON_ZEUS_X27,
    WEAPON_COUNT
};

inline constexpr const char* weapon_names[WEAPON_COUNT] = {
    "P2000", "USP-S", "Glock-18", "P250", "CZ75-Auto", "Five-SeveN", "Tec-9",
    "Dual Berettas", "Desert Eagle", "R8 Revolver",
    "Galil AR", "FAMAS", "AK-47", "M4A4", "M4A1-S", "SG 553", "AUG", "SSG 08",
    "AWP", "G3SG1", "SCAR-20",
    "MP7", "MP9", "MAC-10", "UMP-45", "MP5-SD", "P90", "PP-Bizon", "Nova",
    "XM1014", "MAG-7", "Sawed-Off", "M249", "Negev", "Zeus x27"
};

enum weapon_class_t : int {
    WEAPON_CLASS_PISTOLS = 0,
    WEAPON_CLASS_RIFLES,
    WEAPON_CLASS_MID_TIER,
    WEAPON_CLASS_OTHER,
    WEAPON_CLASS_COUNT
};

inline constexpr const char* weapon_class_names[WEAPON_CLASS_COUNT] = {
    "Pistols", "Rifles", "Mid-Tier", "Other"
};

inline constexpr int weapon_class[WEAPON_COUNT] = {
    WEAPON_CLASS_PISTOLS, WEAPON_CLASS_PISTOLS, WEAPON_CLASS_PISTOLS, WEAPON_CLASS_PISTOLS,
    WEAPON_CLASS_PISTOLS, WEAPON_CLASS_PISTOLS, WEAPON_CLASS_PISTOLS, WEAPON_CLASS_PISTOLS,
    WEAPON_CLASS_PISTOLS, WEAPON_CLASS_PISTOLS,
    WEAPON_CLASS_RIFLES, WEAPON_CLASS_RIFLES, WEAPON_CLASS_RIFLES, WEAPON_CLASS_RIFLES,
    WEAPON_CLASS_RIFLES, WEAPON_CLASS_RIFLES, WEAPON_CLASS_RIFLES, WEAPON_CLASS_RIFLES,
    WEAPON_CLASS_RIFLES, WEAPON_CLASS_RIFLES, WEAPON_CLASS_RIFLES,
    WEAPON_CLASS_MID_TIER, WEAPON_CLASS_MID_TIER, WEAPON_CLASS_MID_TIER, WEAPON_CLASS_MID_TIER,
    WEAPON_CLASS_MID_TIER, WEAPON_CLASS_MID_TIER, WEAPON_CLASS_MID_TIER, WEAPON_CLASS_MID_TIER,
    WEAPON_CLASS_MID_TIER, WEAPON_CLASS_MID_TIER, WEAPON_CLASS_MID_TIER, WEAPON_CLASS_MID_TIER,
    WEAPON_CLASS_MID_TIER, WEAPON_CLASS_OTHER
};

enum weapon_preset_category_t : int {
    WEAPON_PRESET_CUSTOM = 0,
    WEAPON_PRESET_AUTOSNIPERS,
    WEAPON_PRESET_SSG_08,
    WEAPON_PRESET_AWP,
    WEAPON_PRESET_DESERT_EAGLE,
    WEAPON_PRESET_R8_REVOLVER,
    WEAPON_PRESET_PISTOLS,
    WEAPON_PRESET_RIFLES,
    WEAPON_PRESET_SMGS,
    WEAPON_PRESET_SHOTGUNS,
    WEAPON_PRESET_MACHINEGUNS,
    WEAPON_PRESET_TASER,
    // Migration label for the old fixed Deagle/R8 group; not offered by Create Group.
    WEAPON_PRESET_HEAVY_PISTOL,
    WEAPON_PRESET_CATEGORY_COUNT
};

inline constexpr const char* weapon_preset_category_names[WEAPON_PRESET_CATEGORY_COUNT] = {
    "Unnamed", "Autosnipers", "SSG-08", "AWP", "Desert Eagle", "R8 Revolver", "Pistols",
    "Rifles", "SMGs", "Shotguns", "Machineguns", "Taser", "Heavy Pistol"
};

inline constexpr int weapon_preset_category_icons[WEAPON_PRESET_CATEGORY_COUNT] = {
    WEAPON_P2000, WEAPON_G3SG1, WEAPON_SSG_08, WEAPON_AWP, WEAPON_DESERT_EAGLE,
    WEAPON_R8_REVOLVER, WEAPON_P2000, WEAPON_AK_47, WEAPON_MP9, WEAPON_NOVA,
    WEAPON_NEGEV, WEAPON_ZEUS_X27, WEAPON_DESERT_EAGLE
};

inline bool weapon_preset_category_has_weapon(int category, int weapon) {
    if (weapon < 0 || weapon >= WEAPON_COUNT)
        return false;
    switch (category) {
        case WEAPON_PRESET_AUTOSNIPERS: return weapon == WEAPON_G3SG1 || weapon == WEAPON_SCAR_20;
        case WEAPON_PRESET_SSG_08: return weapon == WEAPON_SSG_08;
        case WEAPON_PRESET_AWP: return weapon == WEAPON_AWP;
        case WEAPON_PRESET_DESERT_EAGLE: return weapon == WEAPON_DESERT_EAGLE;
        case WEAPON_PRESET_R8_REVOLVER: return weapon == WEAPON_R8_REVOLVER;
        case WEAPON_PRESET_PISTOLS: return weapon >= WEAPON_P2000 && weapon <= WEAPON_R8_REVOLVER;
        case WEAPON_PRESET_RIFLES: return weapon >= WEAPON_GALIL_AR && weapon <= WEAPON_AUG;
        case WEAPON_PRESET_SMGS: return weapon >= WEAPON_MP7 && weapon <= WEAPON_PP_BIZON;
        case WEAPON_PRESET_SHOTGUNS: return weapon >= WEAPON_NOVA && weapon <= WEAPON_SAWED_OFF;
        case WEAPON_PRESET_MACHINEGUNS: return weapon == WEAPON_M249 || weapon == WEAPON_NEGEV;
        case WEAPON_PRESET_TASER: return weapon == WEAPON_ZEUS_X27;
        case WEAPON_PRESET_HEAVY_PISTOL:
            return weapon == WEAPON_DESERT_EAGLE || weapon == WEAPON_R8_REVOLVER;
        default: return false;
    }
}
