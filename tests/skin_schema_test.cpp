#include "sdk/skin_schema.hpp"
#include <cassert>
int main(){auto x=SkinSchema::Parse(R"kv("items_game" { // comments and repeated sections
"paint_kits_rarity" { "paint_a" "rare" } "sticker_kits" { "5" { "name" "a" "sticker_material" "set/a" } "6" { "name" "bad" "sticker_material" "../../hack" } } "paint_kits_rarity" { "paint_b" "ancient" } "sticker_kits" { "7" { "name" "patch_foo" "patch_material" "set/foo" } } })kv");assert(x.rarity.size()==2);assert(x.decals.size()==2);assert(x.decals[0].name=="a");assert(x.decals[1].patch);}
