#pragma once
// Publishes the Misc tab settings into the tab façade. Called from the render path
// next to VisualSettings::Configure(), so the hooks never read GUI state.
#include <cstdint>

#include "../config.hpp"
#include "../../features/misc/general/general.hpp"
#include "../../features/misc/other/other.hpp"

namespace MiscSettings {
inline void Configure() {
    Misc::General::Publish({cfg.misc.radar.get(), cfg.misc.clantag.get(),
                            static_cast<std::uint64_t>(cfg.misc.scoreboard_options.get()),
                            static_cast<int>(cfg.misc.scoreboard_scale_x.get()),
                            static_cast<int>(cfg.misc.scoreboard_scale_y.get()),
                            static_cast<int>(cfg.misc.scoreboard_weapon_scale.get()),
                            cfg.misc.reveal_enemy_chat.get(),
                            cfg.misc.ragdoll_thrust_enabled.get(),
                            cfg.visuals.ragdoll_thrust.get(),
                            cfg.misc.ping_spike_enabled.get(),
                            cfg.aim.ping_spike_amt.get()});

    Misc::Other::Publish({cfg.misc.autoaccept.get(), cfg.misc.console_filter.get(),
                          cfg.misc.sv_pure.get(), cfg.misc.sv_cheats.get()});
}
} // namespace MiscSettings
