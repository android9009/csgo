// Portable checks for the injected scoreboard script: scale clamping, the icon table,
// and the shape of the payload the feed script carries. No Windows headers, no engine.
#include "features/misc/general/general.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace Misc::Scoreboard;

int Count(const std::string& haystack, const std::string& needle) {
    int count = 0;
    for (auto at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size()))
        ++count;
    return count;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

Equipment MakePlayer(int slot, const char* a, const char* b, int selected, int armor, bool defuser,
                     int money = -1) {
    Equipment equipment;
    equipment.slot = slot;
    equipment.armor = armor;
    equipment.defuser = defuser;
    equipment.money = money;
    equipment.selected = selected;
    if (a) {
        equipment.weapon_types[equipment.weapon_count] = WeaponTypeOf(a);
        equipment.weapons[equipment.weapon_count++] = a;
    }
    if (b) {
        equipment.weapon_types[equipment.weapon_count] = WeaponTypeOf(b);
        equipment.weapons[equipment.weapon_count++] = b;
    }
    return equipment;
}

void TestScales() {
    assert(ClampScale(0) == kScaleMin);
    assert(ClampScale(49) == kScaleMin);
    assert(ClampScale(50) == 50);
    assert(ClampScale(150) == 150);
    assert(ClampScale(200) == 200);
    assert(ClampScale(201) == kScaleMax);

    // Every scale is baked into the emitted layout: the panel width, the icon height and
    // the per-weapon widths all move with the sliders, and never below one pixel.
    const std::string base = BuildEngineScript({100, 100, 100});
    assert(Contains(base, "minWidth: 30"));
    assert(Contains(base, "iconHeight: 16"));
    assert(Contains(base, "armorHeight: 17"));
    assert(Contains(base, "helmetHeight: 22"));
    assert(Contains(base, "labelSize: 13"));

    const std::string big = BuildEngineScript({200, 200, 200});
    assert(Contains(big, "minWidth: 60"));
    assert(Contains(big, "iconHeight: 32"));
    assert(Contains(big, "helmetHeight: 44"));

    const std::string small = BuildEngineScript({0, 0, 0});
    assert(Contains(small, "minWidth: 15"));
    assert(Contains(small, "iconHeight: 8")); // 16 * 50%, never 0

    // A different layout tag forces the script to reinstall instead of returning early,
    // which is how a slider move takes effect without a restart.
    assert(base.find("100x100x100") != std::string::npos);
    assert(big.find("200x200x200") != std::string::npos);
    assert(small.find("50x50x50") != std::string::npos);
}

void TestIcons() {
    assert(std::strcmp(WeaponIcon(7), "ak47") == 0);
    assert(std::strcmp(WeaponIcon(61), "usp_silencer") == 0);
    assert(std::strcmp(WeaponIcon(507), "knife_karambit") == 0);
    assert(std::strcmp(WeaponIcon(49), "c4") == 0);
    assert(WeaponIcon(9999) == nullptr);
    assert(WeaponIcon(0) == nullptr);

    assert(WeaponBaseWidth("p250") == 31);
    assert(WeaponBaseWidth("knife") == 45);
    assert(WeaponBaseWidth("smokegrenade") == 9);
    assert(WeaponBaseWidth("ak47") == 0);
    assert(WeaponBaseWidth(nullptr) == 0);
    assert(WeaponBaseWidth("not_a_weapon") == 0);

    // Weapon classes drive the icon width in the script.
    assert(WeaponTypeOf("ak47") == 3);      // rifle
    assert(WeaponTypeOf("mac10") == 2);     // smg
    assert(WeaponTypeOf("awp") == 5);       // sniper
    assert(WeaponTypeOf("p250") == 1);      // pistol
    assert(WeaponTypeOf("knife_karambit") == 0);
    assert(WeaponTypeOf("flashbang") == 9);
    assert(WeaponTypeOf("c4") == 7);
    assert(WeaponTypeOf("taser") == 8);
    assert(WeaponTypeOf("not_a_weapon") == -1);
    assert(WeaponTypeOf(nullptr) == -1);
}

void TestMessages() {
    std::vector<Equipment> players;
    Equipment armed = MakePlayer(3, "ak47", "knife", 0, 2, true);
    std::snprintf(armed.name, sizeof(armed.name), "armed");
    players.push_back(armed);
    Equipment empty = MakePlayer(9, nullptr, nullptr, -1, 0, false); // no equipment: skipped
    std::snprintf(empty.name, sizeof(empty.name), "empty");
    players.push_back(empty);

    // One message per player, shaped like the reference client's ("updateWeapons").
    Equipment enemy = MakePlayer(3, "ak47", "knife", 0, 2, true);
    enemy.xuid = 76561198000000003ull;
    std::snprintf(enemy.name, sizeof(enemy.name), "enemy");
    const std::string update = BuildUpdateScript(enemy, {100, 100, 100});
    assert(Contains(update, "SClient.receive"));
    assert(Contains(update, "type:\"updateWeapons\""));
    assert(Contains(update, "\"xuid\":\"76561198000000003\""));
    assert(Contains(update, "\"nickname\":\"enemy\""));
    assert(Contains(update, "{\"path\":\"ak47\",\"type\":3}"));
    assert(Contains(update, "\"active\":\"ak47\""));
    assert(Contains(update, "\"armor\":2,\"defuser\":1"));
    assert(Count(update, "{") == Count(update, "}"));
    // The message must do nothing until the engine exists: sending it first is harmless.
    assert(update.find("typeof SClient !== \"undefined\"") != std::string::npos);

    // A player with no xuid (a bot) travels with the nickname instead; with neither there
    // is nothing to address, so no message is built at all.
    Equipment bot = MakePlayer(4, "glock", nullptr, 0, 0, false);
    std::snprintf(bot.name, sizeof(bot.name), "bot_alice");
    const std::string bot_update = BuildUpdateScript(bot, {100, 100, 100});
    assert(Contains(bot_update, "\"xuid\":null"));
    assert(Contains(bot_update, "\"nickname\":\"bot_alice\""));
    Equipment anonymous;
    anonymous.slot = 5;
    assert(BuildUpdateScript(anonymous, {100, 100, 100}).empty());

    // Money is optional: only a value the game reported ends up in the payload.
    {
        Equipment paid = MakePlayer(4, "ak47", nullptr, 0, 2, true);
        paid.xuid = 42;
        paid.money = 6500;
        assert(Contains(BuildUpdateScript(paid, {100, 100, 100}), "\"money\":6500"));
        Equipment broke = MakePlayer(4, "ak47", nullptr, 0, 2, true);
        broke.xuid = 42;
        assert(!Contains(BuildUpdateScript(broke, {100, 100, 100}), "\"money\""));
    }

    // The clear message addresses the same key and removes only that row.
    const std::string clear = BuildClearScript(76561198000000003ull, "enemy");
    assert(Contains(clear, "type:\"clearWeapons\""));
    assert(Contains(clear, "\"xuid\":\"76561198000000003\""));
    assert(Count(clear, "{") == Count(clear, "}"));
    assert(BuildClearScript(0, nullptr).empty());

    // A batch is the engine followed by one message per player.
    const std::string batch = BuildFeedScript(players, {100, 100, 100});
    assert(Count(batch, "type:\"updateWeapons\"") == 1); // only the player with equipment
    assert(Contains(batch, "SClient"));

    // Icon names are emitted into JavaScript, so every one of them has to be a plain
    // identifier: that is what makes injection through the payload impossible.
    for (int id = 0; id <= 600; ++id) {
        const char* name = WeaponIcon(id);
        if (!name) continue;
        for (const char* c = name; *c; ++c)
            assert((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_');
    }
}

void TestReset() {
    const std::string reset = BuildResetScript();
    assert(Contains(reset, "SWeaponManager.clear()"));
    assert(Contains(reset, "SClient = undefined"));
    assert(!Contains(reset, "updateWeapons")); // reset must not push stale data
    assert(Count(reset, "{") == Count(reset, "}"));
}

void TestEngineIsSelfContained() {
    const std::string script = BuildEngineScript({120, 90, 110});
    // Guard against a half-substituted template: every placeholder must be gone.
    assert(!Contains(script, "@"));
    // The installer must be idempotent, otherwise every reinstall would stack handlers.
    assert(Contains(script, "__sbHandlers"));
    // UnregisterForUnhandledEvent takes the id the client returned, not the callback; the
    // callback form throws and used to abort the rest of the installer.
    assert(Contains(script, "UnregisterForUnhandledEvent"));
    assert(!Contains(script, "__sbHandlers[j].handler"));
    assert(Contains(script, "typeof __sbHandlers[i].id !== \"number\""));
    assert(Contains(script, "var id = $.RegisterForUnhandledEvent"));
    // Re-sending the installer in a live context is a no-op, and a slider change carries the
    // players over instead of blanking every row.
    assert(Contains(script, "SWeaponManager.layout === layout"));
    assert(Contains(script, "SWeaponManager.snapshot()"));
    assert(Contains(script, "SWeaponManager.restore(previous)"));
    // Panels are removed on destroy so disabling the feature leaves no orphan rows.
    assert(Contains(script, "DeleteAsync"));
    // Equipment icons come from the client's own icon set.
    assert(Contains(script, "file://{images}/icons/equipment/"));
    assert(Contains(script, "kevlar"));
    // Reveal money renders through the same panel, and the label only appears when a
    // value arrived.
    assert(Contains(script, "money.text = \"$\" + entry.money"));
    assert(Contains(script, "\"money\""));
    // Armour is drawn the way the reference does it: kevlar plus a separate helmet image,
    // both from the client's own equipment folder.
    assert(Contains(script, "icons/equipment/\" + path + \".svg\""));
    assert(Contains(script, "addArmorIcon(content, CFG.armorHeight, \"kevlar\""));
    assert(Contains(script, "addArmorIcon(content, CFG.helmetHeight, \"helmet\""));
    assert(Contains(script, "\"defuser\""));
    // A row that does not exist yet must not lose the player: the entry is stored before
    // the lookup and retried when the board rebuilds.
    assert(Contains(script, "entries[key] = content;"));
    assert(Contains(script, "delete reportedMissing[key];"));
    // Names are compared the way the client shows them, and the diagnostic lists the rows
    // that do exist - otherwise a missing row and a differently-named row look the same.
    assert(Contains(script, "rowNames(list[j]).indexOf(wanted)"));
    assert(Contains(script, "describeRows(sb)"));
    // The panel is moved to the first column (appended it would be clipped by the row) and
    // the row is re-created from the scoreboard's own events.
    assert(Contains(script, "MoveChildBefore"));
    assert(Contains(script, "Scoreboard_UpdateEverything"));
    // The row is rebuilt from the stored state when the game recreates it.
    assert(Contains(script, "redraw"));
}
} // namespace

int main() {
    TestScales();
    TestIcons();
    TestMessages();
    TestReset();
    TestEngineIsSelfContained();
    return 0;
}
