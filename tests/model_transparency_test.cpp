// Portable checks for prop / texture transparency: the slider -> alpha mapping and
// which draws the pass is allowed to touch. The material walk itself needs a live
// material system, so it is exercised in game, not here; only the interface-free
// rules header is included for that reason.
#include "features/visuals/misc/other/model_transparency_rules.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace ModelTransparency;

int main() {
    // 0 % is "off": the draw must be left to the engine untouched.
    assert(AlphaFor(0.f) == 1.f);
    assert(AlphaFor(-5.f) == 1.f);
    assert(AlphaFor(std::nanf("")) == 1.f);
    assert(AlphaFor(1.f) < 1.f);
    assert(std::fabs(AlphaFor(50.f) - 0.5f) < 1e-6f);
    assert(std::fabs(AlphaFor(100.f)) < 1e-6f);
    // The menu sliders stop at 100; anything above still means fully invisible.
    assert(AlphaFor(250.f) == 0.f);

    // Characters are the "texture" half, world props the "prop" half.
    assert(Classify("CCSPlayer") == Target::Texture);
    assert(Classify("CCSRagdoll") == Target::Texture);
    assert(Classify("CHostage") == Target::Texture);
    assert(Classify("CPhysicsProp") == Target::Prop);
    assert(Classify("CBaseAnimating") == Target::Prop);

    // Hands/weapons, throwables and the bomb share materials with other passes.
    assert(Classify("CBaseViewModel") == Target::None);
    assert(Classify("CBaseWeaponWorldModel") == Target::None);
    assert(Classify("CBaseCSGrenadeProjectile") == Target::None);
    assert(Classify("CSmokeGrenadeProjectile") == Target::None);
    assert(Classify("CInferno") == Target::None);
    assert(Classify("CPlantedC4") == Target::None);
    assert(Classify("CC4") == Target::None);
    assert(Classify("CCascadeLight") == Target::None);
    assert(Classify("") == Target::None);

    // Each half only reacts to its own slider.
    assert(PercentFor(Target::Texture, 40.f, 90.f) == 40.f);
    assert(PercentFor(Target::Prop, 40.f, 90.f) == 90.f);
    assert(PercentFor(Target::None, 40.f, 90.f) == 0.f);

    std::puts("model transparency: ok");
    return 0;
}
