#pragma once
// Publishes the Misc tab settings into the tab façade. Called from the render path
// next to VisualSettings::Configure(), so the hooks never read GUI state.
#include <cstdint>

#include "../config.hpp"
#include "../../features/misc/general/general.hpp"
#include "../../features/misc/movement/movement.hpp"
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

    // The MOVEMENT group box. "Auto strafe" has no master switch in the menu: the combo box
    // is a mode selector, so any of its four items means "on" - the same way the combo
    // itself stores the value.
    Misc::Movement::Settings movement;
    movement.strafe_viewangles = cfg.misc.autostrafe->test(cfg_t::autostrafe_viewangles);
    movement.strafe_movement_keys = cfg.misc.autostrafe->test(cfg_t::autostrafe_movement_keys);
    movement.strafe_easy = cfg.misc.autostrafe->test(cfg_t::autostrafe_easy_strafe);
    movement.strafe_jump_release = cfg.misc.autostrafe->test(cfg_t::autostrafe_jump_release);
    movement.autostrafe = movement.strafe_viewangles || movement.strafe_movement_keys ||
                          movement.strafe_easy || movement.strafe_jump_release;
    movement.smoothing = static_cast<int>(cfg.misc.autostrafe_smoothing.get());
    movement.avoid_walls = cfg.misc.avoid_walls.get();
    movement.bhop = cfg.misc.bhop.get();
    movement.quick_stop = cfg.legit.quick_stop.get();
    movement.edge_jump = cfg.misc.edge_jump.get();
    movement.edge_stop = cfg.misc.edge_stop.get();
    Misc::Movement::Publish(movement);

    Misc::Other::Settings other;
    other.auto_accept = cfg.misc.autoaccept.get();
    other.console_filter = cfg.misc.console_filter.get();
    other.sv_pure = cfg.misc.sv_pure.get();
    other.sv_cheats = cfg.misc.sv_cheats.get();
    Misc::Other::Publish(other);
}
} // namespace MiscSettings
