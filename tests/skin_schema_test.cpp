#include "sdk/skin_schema.hpp"
#include <cassert>
int main() {
    const auto data = SkinSchema::Parse(R"kv("items_game" { // repeated sections
      "paint_kits_rarity" { "paint_a" "rare" }
      "paint_kits" {
        "17" { "name" "paint_a" "nested" { "name" "ignore" } }
        "99999999999" { "name" "overflow" }
        "invalid" { "name" "bad_id" }
      }
      "sticker_kits" {
        "5" { "name" "a" "sticker_material" "set/a" }
        "6" { "name" "bad" "sticker_material" "../../hack" }
      }
      "paint_kits_rarity" { "paint_b" "ancient" }
      "paint_kits" { "18" { "name" "paint_b" } }
      "sticker_kits" { "7" { "name" "patch_foo" "patch_material" "set/foo" } }
    })kv");
    assert(data.rarity.size() == 2);
    assert(data.paint_ids.size() == 2);
    assert(data.paint_ids.at("paint_a") == 17 && data.paint_ids.at("paint_b") == 18);
    assert(data.decals.size() == 2 && data.decals[0].name == "a" && data.decals[1].patch);
}
