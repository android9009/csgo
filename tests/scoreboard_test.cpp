// Portable checks for the scoreboard reveal policy: the published bit order must match
// the combo box in menu/tabs/tab_misc.cpp, and the reveal may only be sent in a live
// match, at most once per interval.
#include "features/misc/general/general.hpp"
#include <cassert>
#include <cstdint>
#include <string_view>

namespace {
using namespace Misc::Scoreboard;

// Pinned against the item list of the "Scoreboard" combo box:
// {"Reveal ranks", "Reveal weapon", "Reveal unmatched ranks"}.
constexpr std::pair<std::string_view, int> kItems[] = {
    {"Reveal ranks", Bits::RevealRanks},
    {"Reveal weapon", Bits::RevealWeapon},
    {"Reveal unmatched ranks", Bits::RevealUnmatched},
    {"Reveal money", Bits::RevealMoney},
};

static_assert(Bits::RevealRanks == 0);
static_assert(Bits::RevealWeapon == 1);
static_assert(Bits::RevealUnmatched == 2);
static_assert(Bits::RevealMoney == 3);

// Bit helpers.
static_assert(Enabled(1ull << Bits::RevealRanks, Bits::RevealRanks));
static_assert(Enabled(1ull << Bits::RevealWeapon, Bits::RevealWeapon));
static_assert(Enabled(1ull << Bits::RevealUnmatched, Bits::RevealUnmatched));
static_assert(Enabled(1ull << Bits::RevealMoney, Bits::RevealMoney));
static_assert(!Enabled(1ull << Bits::RevealWeapon, Bits::RevealRanks));
static_assert(!Enabled(0, Bits::RevealRanks));
static_assert(!Enabled(~0ull, 64));
static_assert(!Enabled(~0ull, -1));

// Reveal gate.
static_assert(!ShouldRevealRanks(false, true, 10000, 0, kRevealIntervalMs));
static_assert(!ShouldRevealRanks(true, false, 10000, 0, kRevealIntervalMs));
static_assert(!ShouldRevealRanks(true, true, 10000, 0, 0));
static_assert(ShouldRevealRanks(true, true, 10000, 0, kRevealIntervalMs));
static_assert(ShouldRevealRanks(true, true, 9000, 8000, kRevealIntervalMs));
static_assert(!ShouldRevealRanks(true, true, 8500, 8000, kRevealIntervalMs));
// GetTickCount() wraps every ~49 days; the comparison has to survive it.
static_assert(!ShouldRevealRanks(true, true, 10, 0xFFFFFFF0u, kRevealIntervalMs));
static_assert(ShouldRevealRanks(true, true, 0x1000, 0xFFFFF000u, kRevealIntervalMs));

static_assert(kFeedIntervalMs > 0 && kRevealIntervalMs > 0);

int Main() {
    for (const auto& [name, bit] : kItems) {
        assert(!name.empty());
        assert(bit >= 0 && bit < 4);
        // Each item owns exactly one distinct bit, and the mask the menu produces for
        // it must switch that bit on and the other two off.
        const std::uint64_t mask = 1ull << bit;
        assert(Enabled(mask, bit));
        for (const auto& [other_name, other_bit] : kItems)
            if (other_bit != bit) assert(!Enabled(mask, other_bit));
    }
    return 0;
}
} // namespace

int main() { return Main(); }
