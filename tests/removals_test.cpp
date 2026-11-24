// Portable checks for the Visuals "Removals" mask and the pure decision helpers
// behind it. The bit order is pinned to the combo box order in
// menu/tabs/tab_visuals.cpp: a silent reorder there would toggle the wrong effect.
#include "features/visuals/misc/other/removals.hpp"
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

    // The full decoded mask: every bit lands in its own field.
    {
        const auto none = Removals::FromMask(0);
        assert(!(none.flash || none.smoke || none.particle || none.post_processing ||
                 none.water || none.blood || none.decals || none.ragdolls ||
                 none.visual_punch || none.shadows));
        const auto all = Removals::FromMask(~0ull);
        assert(all.flash && all.smoke && all.particle && all.post_processing &&
               all.water && all.blood && all.decals && all.ragdolls &&
               all.visual_punch && all.shadows);
        const auto one = Removals::FromMask(1ull << Removals::Bits::Water);
        assert(one.water && !(one.flash || one.smoke || one.particle || one.post_processing ||
                              one.blood || one.decals || one.ragdolls || one.visual_punch || one.shadows));
    }

    // Blood and decals share the mat_decals override (blood in CS:GO is a decal).
    assert(!Removals::Detail::DecalsActive(false, false));
    assert(Removals::Detail::DecalsActive(true, false));
    assert(Removals::Detail::DecalsActive(false, true));
    assert(Removals::Detail::DecalsActive(true, true));

    // Change-driven writes: the cvar / material flag is written exactly when the
    // wanted state differs from the asserted state.
    assert(Removals::Detail::ShouldWrite(true, false));
    assert(Removals::Detail::ShouldWrite(false, true));
    assert(!Removals::Detail::ShouldWrite(true, true));
    assert(!Removals::Detail::ShouldWrite(false, false));

    // Flash: only a positive duration is cleared, NaN included in the reject set.
    assert(Removals::Detail::ShouldClear(1.f));
    assert(Removals::Detail::ShouldClear(0.001f));
    assert(Removals::Detail::ShouldClear(std::numeric_limits<float>::max()));
    assert(!Removals::Detail::ShouldClear(0.f));
    assert(!Removals::Detail::ShouldClear(-1.f));
    assert(!Removals::Detail::ShouldClear(std::numeric_limits<float>::quiet_NaN()));
    assert(!Removals::Detail::ShouldClear(-std::numeric_limits<float>::infinity()));

    // Punch: any non-zero component needs a write, NaN compares unequal and is cleared.
    assert(!Removals::Detail::ShouldZero(0.f, 0.f, 0.f));
    assert(Removals::Detail::ShouldZero(1.f, 0.f, 0.f));
    assert(Removals::Detail::ShouldZero(0.f, -2.f, 0.f));
    assert(Removals::Detail::ShouldZero(0.f, 0.f, 0.5f));
    assert(Removals::Detail::ShouldZero(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f));
    return 0;
}
