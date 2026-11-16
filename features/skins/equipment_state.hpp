#pragma once
#include "skin_state.hpp"
#include <string>
#include <string_view>
namespace EquipmentState {
struct Item { std::string_view name; int definition; const char* view; const char* world; };
inline constexpr Item items[]={
 {"weapon_bayonet",500,"models/weapons/v_knife_bayonet.mdl","models/weapons/w_knife_bayonet.mdl"},
 {"weapon_knife_css",503,"models/weapons/v_knife_css.mdl","models/weapons/w_knife_css.mdl"},
 {"weapon_knife_flip",505,"models/weapons/v_knife_flip.mdl","models/weapons/w_knife_flip.mdl"},
 {"weapon_knife_gut",506,"models/weapons/v_knife_gut.mdl","models/weapons/w_knife_gut.mdl"},
 {"weapon_knife_karambit",507,"models/weapons/v_knife_karam.mdl","models/weapons/w_knife_karam.mdl"},
 {"weapon_knife_m9_bayonet",508,"models/weapons/v_knife_m9_bay.mdl","models/weapons/w_knife_m9_bay.mdl"},
 {"weapon_knife_tactical",509,"models/weapons/v_knife_tactical.mdl","models/weapons/w_knife_tactical.mdl"},
 {"weapon_knife_falchion",512,"models/weapons/v_knife_falchion_advanced.mdl","models/weapons/w_knife_falchion_advanced.mdl"},
 {"weapon_knife_survival_bowie",514,"models/weapons/v_knife_survival_bowie.mdl","models/weapons/w_knife_survival_bowie.mdl"},
 {"weapon_knife_butterfly",515,"models/weapons/v_knife_butterfly.mdl","models/weapons/w_knife_butterfly.mdl"},
 {"weapon_knife_push",516,"models/weapons/v_knife_push.mdl","models/weapons/w_knife_push.mdl"},
 {"weapon_knife_cord",517,"models/weapons/v_knife_cord.mdl","models/weapons/w_knife_cord.mdl"},
 {"weapon_knife_canis",518,"models/weapons/v_knife_canis.mdl","models/weapons/w_knife_canis.mdl"},
 {"weapon_knife_ursus",519,"models/weapons/v_knife_ursus.mdl","models/weapons/w_knife_ursus.mdl"},
 {"weapon_knife_gypsy_jackknife",520,"models/weapons/v_knife_gypsy_jackknife.mdl","models/weapons/w_knife_gypsy_jackknife.mdl"},
 {"weapon_knife_outdoor",521,"models/weapons/v_knife_outdoor.mdl","models/weapons/w_knife_outdoor.mdl"},
 {"weapon_knife_stiletto",522,"models/weapons/v_knife_stiletto.mdl","models/weapons/w_knife_stiletto.mdl"},
 {"weapon_knife_widowmaker",523,"models/weapons/v_knife_widowmaker.mdl","models/weapons/w_knife_widowmaker.mdl"},
 {"weapon_knife_skeleton",525,"models/weapons/v_knife_skeleton.mdl","models/weapons/w_knife_skeleton.mdl"},
 {"studded_bloodhound_gloves",5027,"models/weapons/v_models/arms/glove_bloodhound/v_glove_bloodhound.mdl","models/weapons/w_models/arms/w_glove_bloodhound.mdl"},
 {"sporty_gloves",5030,"models/weapons/v_models/arms/glove_sporty/v_glove_sporty.mdl","models/weapons/w_models/arms/w_glove_sporty.mdl"},
 {"slick_gloves",5031,"models/weapons/v_models/arms/glove_slick/v_glove_slick.mdl","models/weapons/w_models/arms/w_glove_slick.mdl"},
 {"leather_handwraps",5032,"models/weapons/v_models/arms/glove_handwrap_leathery/v_glove_handwrap_leathery.mdl","models/weapons/w_models/arms/w_glove_handwrap_leathery.mdl"},
 {"motorcycle_gloves",5033,"models/weapons/v_models/arms/glove_motorcycle/v_glove_motorcycle.mdl","models/weapons/w_models/arms/w_glove_motorcycle.mdl"},
 {"specialist_gloves",5034,"models/weapons/v_models/arms/glove_specialist/v_glove_specialist.mdl","models/weapons/w_models/arms/w_glove_specialist.mdl"},
 {"studded_hydra_gloves",5035,"models/weapons/v_models/arms/glove_bloodhound/v_glove_bloodhound_hydra.mdl","models/weapons/w_models/arms/w_glove_bloodhound_hydra.mdl"},
 {"studded_brokenfang_gloves",4725,"models/weapons/v_models/arms/glove_brokenfang/v_glove_brokenfang.mdl","models/weapons/w_models/arms/w_glove_brokenfang.mdl"}
};
inline const Item* Find(std::string_view name){for(const auto& i:items)if(i.name==name)return &i;return nullptr;}
inline const Item* Find(int def){for(const auto& i:items)if(i.definition==def)return &i;return nullptr;}
inline bool Knife(int def){return def==42 || def==59 || (def>=500 && def<=525);}
struct Selection { int definition{}; SkinState::Choice finish{}; std::uint64_t revision{}; };
struct Team { std::string agent; Selection knife,gloves; int inventory_agent{},inventory_medal{},inventory_music{}; };
inline std::mutex mutex;
inline Team teams[2];
inline std::uint64_t revision{};
inline Team Get(bool ct){std::lock_guard<std::mutex> lock(mutex);return teams[ct?0:1];}
inline void Agent(bool ct,std::string path){std::lock_guard<std::mutex> lock(mutex);auto& value=teams[ct?0:1].agent;if(value!=path){value=std::move(path);++revision;}}
inline std::uint64_t Revision(){std::lock_guard<std::mutex> lock(mutex);return revision;}
// Numeric IDs resolved from this installation's items_game, never UI indices.
inline void InventorySelection(bool ct,int category,int id) {
 std::lock_guard<std::mutex> lock(mutex);auto& t=teams[ct?0:1];
 auto& value=category==0?t.inventory_agent:category==1?t.inventory_medal:t.inventory_music;
 value=id>0?id:0;
}
inline bool Select(bool ct,bool gloves,std::string_view name,SkinState::Choice finish={}){
 const auto* item=Find(name);
 if(!name.empty() && (!item || Knife(item->definition)==gloves))return false;
 if(finish.paint_kit<0 || finish.seed<0 || finish.seed>1000 || !(finish.wear>=0 && finish.wear<=1))return false;
 finish.name.back()='\0';
 std::lock_guard<std::mutex> lock(mutex);auto& value=gloves?teams[ct?0:1].gloves:teams[ct?0:1].knife;
 const int definition=item?item->definition:0;
 // Reopening a catalog or committing unchanged values is not a settings change.
 if(value.definition==definition && (!definition || SkinState::Equal(value.finish,finish)))return true;
 value={definition,finish,++revision};return true;
}
}
