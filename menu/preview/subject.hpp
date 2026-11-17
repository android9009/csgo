#pragma once
#include <string_view>
namespace PreviewSubject {
// Out of game / spectator: use CT as the reference local side.
inline bool CounterTerrorist(int local_team, int subject) {
    const bool local_ct = local_team != 2;
    return subject == 0 ? !local_ct : local_ct;
}
inline bool PlayerModel(std::string_view path) {
    return path.size() > 18 && path.size() < 260 &&
        path.substr(0, 14) == "models/player/" &&
        path.substr(path.size() - 4) == ".mdl" && path.find("..") == path.npos;
}
}
