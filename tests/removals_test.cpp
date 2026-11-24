// Portable checks for the Visuals "Removals" mask and the two removals that are
// implemented. The bit order is pinned to the combo box order in
// menu/tabs/tab_visuals.cpp: a silent reorder there would toggle the wrong effect.
#include "features/visuals/removals/flash.hpp"
#include "features/visuals/removals/punch.hpp"
#include "features/visuals/removals/removals.hpp"
#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    // Item order of the combo box, exactly as listed in the menu.
    assert(Removals::Bits::Flash == 0);           // "Flashbang Effect"
    assert(Removals::Bits::Smoke == 1);           // "Smoke Rendering"
    assert(Removals::Bits::Particle == 2);        // "Particle"
    assert(Removals::Bits::PostProcessing == 3);  // "Post Processing"
    assert(Removals::Bits::Water == 4);           // "Water Effects"
    assert(Removals::Bits::Blood == 5);           // "Blood Effects"
    assert(Removals::Bits::Decals == 6);          // "Decals"
    assert(Removals::Bits::Ragdolls == 7);        // "Ragdolls"
    assert(Removals::Bits::VisualPunch == 8);     // "Visual punch"
    assert(Removals::Bits::Shadows == 9);         // "Shadows"
    assert(Removals::Bits::Count == 10);

    // Bit decoding.
    assert(Removals::Enabled(1ull << Removals::Bits::Flash, Removals::Bits::Flash));
    assert(!Removals::Enabled(1ull << Removals::Bits::Smoke, Removals::Bits::Flash));
    assert(!Removals::Enabled(0, Removals::Bits::Flash));
    assert(!Removals::Enabled(0xFFFFFFFFull, -1));   // out of range index is never set

    // Mask -> flags: only the implemented bits survive, the rest of the mask is ignored.
    {
        const auto none = Removals::FromMask(0);
        assert(!none.flash && !none.visual_punch);
        const auto flash_only = Removals::FromMask(1ull << Removals::Bits::Flash);
        assert(flash_only.flash && !flash_only.visual_punch);
        const auto punch_only = Removals::FromMask(1ull << Removals::Bits::VisualPunch);
        assert(!punch_only.flash && punch_only.visual_punch);
        const auto both = Removals::FromMask((1ull << Removals::Bits::Flash) | (1ull << Removals::Bits::VisualPunch));
        assert(both.flash && both.visual_punch);
        const auto others = Removals::FromMask((1ull << Removals::Bits::Smoke) | (1ull << Removals::Bits::Shadows));
        assert(!others.flash && !others.visual_punch);
        // Every bit set: both implemented removals are on, no out-of-range shift.
        const auto all = Removals::FromMask(~0ull);
        assert(all.flash && all.visual_punch);
    }

    // Flash: only a positive duration is cleared, NaN included in the reject set.
    assert(Removals::Flash::ShouldClear(1.f));
    assert(Removals::Flash::ShouldClear(0.001f));
    assert(Removals::Flash::ShouldClear(std::numeric_limits<float>::max()));
    assert(!Removals::Flash::ShouldClear(0.f));
    assert(!Removals::Flash::ShouldClear(-1.f));
    assert(!Removals::Flash::ShouldClear(std::numeric_limits<float>::quiet_NaN()));
    assert(!Removals::Flash::ShouldClear(-std::numeric_limits<float>::infinity()));

    // Punch: any non-zero component needs a write, NaN compares unequal and is cleared.
    assert(!Removals::Punch::ShouldZero(0.f, 0.f, 0.f));
    assert(Removals::Punch::ShouldZero(1.f, 0.f, 0.f));
    assert(Removals::Punch::ShouldZero(0.f, -2.f, 0.f));
    assert(Removals::Punch::ShouldZero(0.f, 0.f, 0.5f));
    assert(Removals::Punch::ShouldZero(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f));
    return 0;
}
