#pragma once
// Publishes the Visuals tab "Removals" combo box. It is a bitset, so the runtime gets
// the raw mask and maps it through Removals::FromMask - the bit order is pinned by
// tests/removals_test.cpp against the item list in menu/tabs/tab_visuals.cpp.
#include "../config.hpp"
#include "../../features/visuals/misc/other/removals/removals.hpp"

namespace RemovalsSettings {
inline void Configure() {
    Removals::Publish(static_cast<std::uint64_t>(cfg.misc.removals.get()));
}
} // namespace RemovalsSettings
