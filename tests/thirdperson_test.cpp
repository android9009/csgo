// Host checks for Visuals -> Misc -> LOCAL: "Thirdperson". Only exercises the pure
// decision maths in features/visuals/misc/local/thirdperson.hpp; the camera and render-mode writes
// need the game.
#include "../features/visuals/misc/local/thirdperson.hpp"

#include <cassert>
#include <cmath>

using namespace ThirdPerson;

int main() {
    Settings s;
    // Off => never active, regardless of state.
    assert(!Detail::Active(s, true, false));
    assert(!Detail::Active(s, false, false));

    // On, alive => active.
    s.enabled = true;
    assert(Detail::Active(s, true, false));

    // Dead: only with "force when dead".
    assert(!Detail::Active(s, false, false));
    s.force_dead = true;
    assert(Detail::Active(s, false, false));
    s.force_dead = false;

    // Grenade: disabled only when "disable on grenade" is set.
    assert(Detail::Active(s, true, true));
    s.disable_grenade = true;
    assert(!Detail::Active(s, true, true));
    s.disable_grenade = false;

    // Model alpha: 0% -> solid (255), 100% -> invisible (0); scoped and nade pick their own.
    s.scope_transparency = 0.f;
    s.nade_transparency = 0.f;
    assert(Detail::ModelAlpha(s, false, false) == 255);

    s.scope_transparency = 100.f;
    assert(Detail::ModelAlpha(s, true, false) == 0);
    assert(Detail::ModelAlpha(s, false, false) == 255); // nade slider must not apply when not scoped

    s.scope_transparency = 0.f;
    s.nade_transparency = 100.f;
    assert(Detail::ModelAlpha(s, false, true) == 0);
    assert(Detail::ModelAlpha(s, true, true) == 255); // scoped takes precedence over nade

    // Mid, clamp and NaN are all safe.
    s.scope_transparency = 50.f;
    assert(Detail::ModelAlpha(s, true, false) == 128);
    s.scope_transparency = -10.f;
    assert(Detail::ModelAlpha(s, true, false) == 255);
    s.scope_transparency = 1000.f;
    assert(Detail::ModelAlpha(s, true, false) == 0);
    s.scope_transparency = std::nanf("");
    assert(Detail::ModelAlpha(s, true, false) == 255);

    return 0;
}
