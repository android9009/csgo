// Dumps the injected scoreboard script and one feed call, so the JavaScript can be checked
// outside the game. Not part of the portable test loop (it needs node); see run.sh.
#include "features/misc/general/general.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    const int horizontal = argc > 1 ? std::atoi(argv[1]) : 100;
    const Misc::Scoreboard::Scales scales{horizontal, 100, 100};

    const auto engine = Misc::Scoreboard::BuildEngineScript(scales);
    std::fwrite(engine.data(), 1, engine.size(), stdout);

    std::vector<Misc::Scoreboard::Equipment> players;
    Misc::Scoreboard::Equipment enemy;
    enemy.slot = 3; enemy.armor = 2; enemy.defuser = true; enemy.money = 4321; enemy.selected = 0;
    enemy.weapons[enemy.weapon_count++] = "ak47";
    enemy.weapons[enemy.weapon_count++] = "knife_karambit";
    enemy.weapons[enemy.weapon_count++] = "flashbang";
    std::snprintf(enemy.name, sizeof(enemy.name), "enemy");
    players.push_back(enemy);

    Misc::Scoreboard::Equipment mate;
    mate.slot = 7; mate.armor = 1; mate.selected = 0;
    mate.weapons[mate.weapon_count++] = "m4a1";
    std::snprintf(mate.name, sizeof(mate.name), "mate");
    players.push_back(mate);

    const auto feed = Misc::Scoreboard::BuildFeedScript(players, scales);
    const auto at = feed.find("\n__sb.feed(");
    std::fwrite("\n/*SPLIT*/\n", 1, 11, stdout);
    if (at != std::string::npos)
        std::fwrite(feed.data() + at + 1, 1, feed.size() - at - 1, stdout);
    return 0;
}
