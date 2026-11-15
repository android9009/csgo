#pragma once
#include <cstddef>
#include <cstring>
namespace PreviewCatalog {
struct Agent { const char* name; const char* model; const char* icon; };
inline constexpr Agent ct[] = {
 {"Default", "models/player/custom_player/legacy/ctm_fbi_variantb.mdl", ""},
 {"Seal Team 6 E", "models/player/custom_player/legacy/ctm_st6_variante.mdl", "customplayer_ctm_st6_variante"},
 {"Seal Team 6 K", "models/player/custom_player/legacy/ctm_st6_variantk.mdl", "customplayer_ctm_st6_variantk"},
 {"FBI F", "models/player/custom_player/legacy/ctm_fbi_variantf.mdl", "customplayer_ctm_fbi_variantf"},
 {"SAS F", "models/player/custom_player/legacy/ctm_sas_variantf.mdl", "customplayer_ctm_sas_variantf"},
 {"FBI G", "models/player/custom_player/legacy/ctm_fbi_variantg.mdl", "customplayer_ctm_fbi_variantg"},
 {"Seal Team 6 G", "models/player/custom_player/legacy/ctm_st6_variantg.mdl", "customplayer_ctm_st6_variantg"},
 {"Seal Team 6 M", "models/player/custom_player/legacy/ctm_st6_variantm.mdl", "customplayer_ctm_st6_variantm"},
 {"Seal Team 6 I", "models/player/custom_player/legacy/ctm_st6_varianti.mdl", "customplayer_ctm_st6_varianti"},
};
inline constexpr Agent t[] = {
 {"Default", "models/player/custom_player/legacy/tm_phoenix_varianta.mdl", ""},
 {"Phoenix F", "models/player/custom_player/legacy/tm_phoenix_variantf.mdl", "customplayer_tm_phoenix_variantf"},
 {"Phoenix H", "models/player/custom_player/legacy/tm_phoenix_varianth.mdl", "customplayer_tm_phoenix_varianth"},
 {"Elite Crew G", "models/player/custom_player/legacy/tm_leet_variantg.mdl", "customplayer_tm_leet_variantg"},
 {"Balkan I", "models/player/custom_player/legacy/tm_balkan_varianti.mdl", "customplayer_tm_balkan_varianti"},
 {"Elite Crew H", "models/player/custom_player/legacy/tm_leet_varianth.mdl", "customplayer_tm_leet_varianth"},
 {"Phoenix G", "models/player/custom_player/legacy/tm_phoenix_variantg.mdl", "customplayer_tm_phoenix_variantg"},
 {"Balkan F", "models/player/custom_player/legacy/tm_balkan_variantf.mdl", "customplayer_tm_balkan_variantf"},
 {"Balkan J", "models/player/custom_player/legacy/tm_balkan_variantj.mdl", "customplayer_tm_balkan_variantj"},
 {"Elite Crew I", "models/player/custom_player/legacy/tm_leet_varianti.mdl", "customplayer_tm_leet_varianti"},
 {"Balkan G", "models/player/custom_player/legacy/tm_balkan_variantg.mdl", "customplayer_tm_balkan_variantg"},
 {"Balkan H", "models/player/custom_player/legacy/tm_balkan_varianth.mdl", "customplayer_tm_balkan_varianth"},
 {"Elite Crew F", "models/player/custom_player/legacy/tm_leet_variantf.mdl", "customplayer_tm_leet_variantf"},
};
inline std::size_t AgentCount(bool is_ct) { return is_ct ? sizeof(ct)/sizeof(*ct) : sizeof(t)/sizeof(*t); }
inline const Agent& GetAgent(bool is_ct, std::size_t index) { return is_ct ? ct[index < AgentCount(true) ? index : 0] : t[index < AgentCount(false) ? index : 0]; }
struct Weapon { const char* id; const char* model; };
inline constexpr Weapon weapons[] = {
 {"weapon_ssg08","models/weapons/w_snip_ssg08.mdl"}, {"weapon_ak47","models/weapons/w_rif_ak47.mdl"},
 {"weapon_m4a1","models/weapons/w_rif_m4a1.mdl"}, {"weapon_m4a1_silencer","models/weapons/w_rif_m4a1_s.mdl"},
 {"weapon_awp","models/weapons/w_snip_awp.mdl"}, {"weapon_aug","models/weapons/w_rif_aug.mdl"},
 {"weapon_famas","models/weapons/w_rif_famas.mdl"}, {"weapon_galilar","models/weapons/w_rif_galilar.mdl"},
 {"weapon_sg556","models/weapons/w_rif_sg556.mdl"}, {"weapon_scar20","models/weapons/w_snip_scar20.mdl"},
 {"weapon_g3sg1","models/weapons/w_snip_g3sg1.mdl"}, {"weapon_deagle","models/weapons/w_pist_deagle.mdl"},
 {"weapon_elite","models/weapons/w_pist_elite.mdl"}, {"weapon_fiveseven","models/weapons/w_pist_fiveseven.mdl"},
 {"weapon_glock","models/weapons/w_pist_glock18.mdl"}, {"weapon_hkp2000","models/weapons/w_pist_hkp2000.mdl"},
 {"weapon_p250","models/weapons/w_pist_p250.mdl"}, {"weapon_tec9","models/weapons/w_pist_tec9.mdl"},
 {"weapon_usp_silencer","models/weapons/w_pist_223.mdl"}, {"weapon_cz75a","models/weapons/w_pist_cz_75.mdl"},
 {"weapon_revolver","models/weapons/w_pist_revolver.mdl"}, {"weapon_mac10","models/weapons/w_smg_mac10.mdl"},
 {"weapon_mp9","models/weapons/w_smg_mp9.mdl"}, {"weapon_mp7","models/weapons/w_smg_mp7.mdl"},
 {"weapon_mp5sd","models/weapons/w_smg_mp5sd.mdl"}, {"weapon_ump45","models/weapons/w_smg_ump45.mdl"},
 {"weapon_p90","models/weapons/w_smg_p90.mdl"}, {"weapon_bizon","models/weapons/w_smg_bizon.mdl"},
 {"weapon_nova","models/weapons/w_shot_nova.mdl"}, {"weapon_xm1014","models/weapons/w_shot_xm1014.mdl"},
 {"weapon_mag7","models/weapons/w_shot_mag7.mdl"}, {"weapon_sawedoff","models/weapons/w_shot_sawedoff.mdl"},
 {"weapon_m249","models/weapons/w_mach_m249para.mdl"}, {"weapon_negev","models/weapons/w_mach_negev.mdl"},
 {"weapon_knife","models/weapons/w_knife_default_ct.mdl"}, {"weapon_knife_t","models/weapons/w_knife_default_t.mdl"},
 {"weapon_bayonet","models/weapons/w_knife_bayonet.mdl"}, {"weapon_knife_flip","models/weapons/w_knife_flip.mdl"},
 {"weapon_knife_gut","models/weapons/w_knife_gut.mdl"}, {"weapon_knife_karambit","models/weapons/w_knife_karam.mdl"},
 {"weapon_knife_m9_bayonet","models/weapons/w_knife_m9_bay.mdl"}, {"weapon_knife_tactical","models/weapons/w_knife_tactical.mdl"},
 {"weapon_knife_falchion","models/weapons/w_knife_falchion_advanced.mdl"}, {"weapon_knife_survival_bowie","models/weapons/w_knife_survival_bowie.mdl"},
 {"weapon_knife_butterfly","models/weapons/w_knife_butterfly.mdl"}, {"weapon_knife_push","models/weapons/w_knife_push.mdl"},
 {"weapon_knife_ursus","models/weapons/w_knife_ursus.mdl"}, {"weapon_knife_gypsy_jackknife","models/weapons/w_knife_gypsy_jackknife.mdl"},
 {"weapon_knife_stiletto","models/weapons/w_knife_stiletto.mdl"}, {"weapon_knife_widowmaker","models/weapons/w_knife_widowmaker.mdl"},
 {"weapon_knife_skeleton","models/weapons/w_knife_skeleton.mdl"}, {"weapon_knife_canis","models/weapons/w_knife_canis.mdl"},
 {"weapon_knife_cord","models/weapons/w_knife_cord.mdl"}, {"weapon_knife_outdoor","models/weapons/w_knife_outdoor.mdl"},
 {"weapon_knife_css","models/weapons/w_knife_css.mdl"}
};
inline int WeaponIndex(const char* id) { if (!id) return 0; for (std::size_t i=0; i<sizeof(weapons)/sizeof(*weapons); ++i) if (!std::strcmp(id,weapons[i].id)) return static_cast<int>(i); return 0; }
inline std::size_t WeaponCount() { return sizeof(weapons)/sizeof(*weapons); }
}
