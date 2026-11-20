#pragma once
// Publishes the Misc tab settings into the tab façade. Called from the render path
// next to VisualSettings::Configure(), so the hooks never read GUI state.
#include <cstdint>

#include "../config.hpp"
#include "../../features/misc/general/general.hpp"
#include "../../features/misc/other/other.hpp"

namespace MiscSettings {
inline void Configure() {
    // Built field by field on purpose: a braced list silently shifts every following
    // value when a field is added or removed (MSVC reports it as a narrowing error far
    // from the cause), while named assignments keep the mapping explicit.
    Misc::General::Settings general;
    general.radar = cfg.misc.radar.get();
    general.clantag = cfg.misc.clantag.get();
    general.reveal_enemy_chat = cfg.misc.reveal_enemy_chat.get();
    general.ping_spike = cfg.misc.ping_spike_enabled.get();
    general.ping_spike_ms = cfg.aim.ping_spike_amt.get();
    Misc::General::Publish(general);

    Misc::Other::Settings other;
    other.auto_accept = cfg.misc.autoaccept.get();
    other.console_filter = cfg.misc.console_filter.get();
    other.sv_pure = cfg.misc.sv_pure.get();
    other.sv_cheats = cfg.misc.sv_cheats.get();
    Misc::Other::Publish(other);
}
} // namespace MiscSettings
