// Portable checks for the injected scoreboard script: scale clamping, the icon table,
// and the shape of the payload the feed script carries. No Windows headers, no engine.
#include "features/misc/general/general.hpp"
#include <cassert>
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

Equipment MakePlayer(int slot, const char* a, const char* b, int selected, int armor, bool defuser) {
    Equipment equipment;
    equipment.slot = slot;
    equipment.armor = armor;
    equipment.defuser = defuser;
    equipment.selected = selected;
    if (a) equipment.weapons[equipment.weapon_count++] = a;
    if (b) equipment.weapons[equipment.weapon_count++] = b;
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
    assert(Contains(base, "PANEL_WIDTH = 30"));
    assert(Contains(base, "textureheight=\"18\""));
    // "p250" is a 31px pistol; at the minimum scale it must stay positive.
    assert(Contains(base, "\"p250\":31"));

    const std::string big = BuildEngineScript({200, 200, 200});
    assert(Contains(big, "PANEL_WIDTH = 60"));
    assert(Contains(big, "textureheight=\"36\""));
    assert(Contains(big, "\"p250\":62"));

    const std::string small = BuildEngineScript({0, 0, 0});
    assert(Contains(small, "PANEL_WIDTH = 15"));
    assert(Contains(small, "\"p250\":16")); // 31 * 50% rounds up, never 0

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
}

void TestFeedPayload() {
    std::vector<Equipment> players;
    players.push_back(MakePlayer(3, "ak47", "knife", 0, 2, true));
    players.push_back(MakePlayer(9, nullptr, nullptr, -1, 0, false)); // no weapons: skipped
    players.push_back(MakePlayer(0, "glock", nullptr, 0, 1, false));  // slot 0: skipped

    const std::string script = BuildFeedScript(players, {100, 100, 100});
    assert(Contains(script, "__sb.feed({"));
    assert(Contains(script, "\"3\":{\"w\":[\"ak47\",\"knife\"],\"s\":0,\"a\":2,\"d\":1}"));
    assert(!Contains(script, "\"9\":"));   // players without equipment are not sent
    assert(!Contains(script, "\"0\":"));
    assert(Count(script, "__sb.feed(") == 1);
    // Balanced braces: the payload must not break the script's syntax.
    assert(Count(script, "{") == Count(script, "}"));

    // The engine is part of every feed so a HUD reload re-creates the rows.
    assert(Contains(script, "RegisterForUnhandledEvent"));
    assert(Contains(script, "OnOpenScoreboard"));
    // Icon names are emitted into a JavaScript array, so every one of them has to be a
    // plain identifier: that is what makes injection through the payload impossible.
    for (int id = 0; id <= 600; ++id) {
        const char* name = WeaponIcon(id);
        if (!name) continue;
        for (const char* c = name; *c; ++c)
            assert((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_');
    }
}

void TestReset() {
    const std::string reset = BuildResetScript();
    assert(Contains(reset, "__sb.destroy"));
    assert(Contains(reset, "__sb.destroy()"));
    assert(!Contains(reset, "feed"));  // reset must not push stale data
    assert(Count(reset, "{") == Count(reset, "}"));
}

void TestEngineIsSelfContained() {
    const std::string script = BuildEngineScript({120, 90, 110});
    // Guard against a half-substituted template: every placeholder must be gone.
    assert(!Contains(script, "@"));
    // The installer must be idempotent, otherwise every feed would stack handlers.
    assert(Contains(script, "__sb.layout === \"120x90x110\""));
    assert(Contains(script, "__sb.destroy()"));
    // Panels are removed on destroy so disabling the feature leaves no orphan rows.
    assert(Contains(script, "DeleteAsync"));
    // Equipment icons come from the client's own icon set.
    assert(Contains(script, "file://{images}/icons/equipment/"));
    assert(Contains(script, "kevlar"));
    assert(Contains(script, "assaultsuit"));
    assert(Contains(script, "defuser"));
}
} // namespace

int main() {
    TestScales();
    TestIcons();
    TestFeedPayload();
    TestReset();
    TestEngineIsSelfContained();
    return 0;
}
