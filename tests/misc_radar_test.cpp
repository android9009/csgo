// Portable checks for the radar reveal rule: this decides whether an engine write
// happens for each player slot, so every branch is worth pinning down.
#include "features/misc/general/general.hpp"
#include <cassert>

namespace {
Misc::Radar::Target MakeTarget(bool dormant, bool alive, bool local, int team) {
    Misc::Radar::Target target;
    target.dormant = dormant;
    target.alive = alive;
    target.local = local;
    target.team = team;
    return target;
}
} // namespace

int main() {
    using Misc::Radar::ShouldSpot;
    assert(ShouldSpot(MakeTarget(false, true, false, 2)));  // T
    assert(ShouldSpot(MakeTarget(false, true, false, 3)));  // CT
    assert(!ShouldSpot(MakeTarget(true, true, false, 2)));  // dormant
    assert(!ShouldSpot(MakeTarget(false, false, false, 2))); // dead
    assert(!ShouldSpot(MakeTarget(false, true, true, 3)));  // local player
    assert(!ShouldSpot(MakeTarget(false, true, false, 1)));  // spectator
    assert(!ShouldSpot(MakeTarget(false, true, false, 0)));  // unassigned
    assert(!ShouldSpot(MakeTarget(false, true, false, 4)));  // out of range
    // Dormant and dead at once stays false, and the check is order independent.
    assert(!ShouldSpot(MakeTarget(true, false, false, 2)));
    return 0;
}
