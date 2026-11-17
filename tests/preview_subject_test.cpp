#include "../menu/preview/subject.hpp"
#include <cassert>
int main() {
    using namespace PreviewSubject;
    for (int team : {2, 3}) {
        assert(CounterTerrorist(team, 2) == (team == 3));
        assert(CounterTerrorist(team, 1) == (team == 3));
        assert(CounterTerrorist(team, 0) == (team == 2));
    }
    assert(CounterTerrorist(0, 2));
    assert(!CounterTerrorist(1, 0));
    assert(PlayerModel("models/player/custom_player/legacy/ctm_sas.mdl"));
    assert(PlayerModel("models/player/tm_phoenix.mdl"));
    assert(!PlayerModel("models/weapons/v_knife.mdl"));
    assert(!PlayerModel("models/player/../weapons/v_knife.mdl"));
    assert(!PlayerModel(""));
}
