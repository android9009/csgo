// Dumps the injected scoreboard scripts so the JavaScript can be checked outside the game:
// the installer, one update message per player, and a clear message. Not part of the
// portable test loop (it needs node); see run.sh.
#include "features/misc/general/general.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
Misc::Scoreboard::Equipment MakePlayer(int slot, std::uint64_t xuid, const char* nickname,
                                       const char* const* weapons, int count, int selected,
                                       int armor, bool defuser, int money) {
    Misc::Scoreboard::Equipment player;
    player.slot = slot;
    player.xuid = xuid;
    player.armor = armor;
    player.defuser = defuser;
    player.money = money;
    player.selected = selected;
    for (int i = 0; i < count; ++i) {
        player.weapons[player.weapon_count] = weapons[i];
        player.weapon_types[player.weapon_count] = Misc::Scoreboard::WeaponTypeOf(weapons[i]);
        ++player.weapon_count;
    }
    std::snprintf(player.name, sizeof(player.name), "%s", nickname);
    return player;
}
} // namespace

int main(int argc, char** argv) {
    const int horizontal = argc > 1 ? std::atoi(argv[1]) : 100;
    const Misc::Scoreboard::Scales scales{horizontal, 100, 100};

    std::fwrite("/*ENGINE*/\n", 1, 11, stdout);
    const auto engine = Misc::Scoreboard::BuildEngineScript(scales);
    std::fwrite(engine.data(), 1, engine.size(), stdout);

    const char* enemy_weapons[] = {"ak47", "knife_karambit", "flashbang"};
    const char* mate_weapons[] = {"m4a1"};

    std::vector<Misc::Scoreboard::Equipment> players;
    players.push_back(MakePlayer(3, 76561198000000003ull, "enemy", enemy_weapons, 3, 0, 2, true, 4321));
    players.push_back(MakePlayer(7, 76561198000000007ull, "mate", mate_weapons, 1, 0, 1, false, -1));

    std::fwrite("/*UPDATES*/\n", 1, 12, stdout);
    for (const auto& player : players) {
        const auto update = Misc::Scoreboard::BuildUpdateScript(player, scales);
        std::fwrite(update.data(), 1, update.size(), stdout);
        std::fwrite("\n", 1, 1, stdout);
    }

    std::fwrite("/*CLEAR*/\n", 1, 10, stdout);
    const auto clear = Misc::Scoreboard::BuildClearScript(players[0].xuid, players[0].name);
    std::fwrite(clear.data(), 1, clear.size(), stdout);
    std::fwrite("\n", 1, 1, stdout);

    std::fwrite("/*RESET*/\n", 1, 10, stdout);
    const auto reset = Misc::Scoreboard::BuildResetScript();
    std::fwrite(reset.data(), 1, reset.size(), stdout);
    return 0;
}
