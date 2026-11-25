#pragma once
#include <atomic>
#include <set>
namespace LocalInventory {
inline std::atomic<bool> enabled{false};
struct Loadout {
    bool ready{};
    std::set<int> definitions;
    bool Contains(int definition) const { return ready && definitions.count(definition)!=0; }
};
Loadout GetLoadout(bool ct);
// Game-thread entry, including outside matches. Experimental legacy PE32 ABI.
void Frame();
void Shutdown();
}
