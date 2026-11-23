#pragma once
// Menu -> runtime bridge for the Misc tab. Called every frame from the render loop (the same
// pattern as the Visuals/Removals bridges), it reads the live config and republishes it to the
// per-tab runtimes. Publishing is cheap (atomic stores), so doing it each frame keeps the
// features in lockstep with the menu with no change-listener bookkeeping.
//
// Bit-order notes (pinned to the combo boxes in menu/tabs/tab_misc.cpp):
//   misc>general>logs  : 0 Hit, 1 Miss, 2 Buy, 3 Hurt, 4 Naded
// The single-select combo boxes store their choice as one bit at the cfg_t index, so the
// selected value is first_set_bit() (0 when "None" / the bitset is empty).
#include <cstdint>
#include <memory>
#include <vector>
#include "../config.hpp"
#include "../../features/misc/buybot/buybot.hpp"
#include "../../features/misc/general/general.hpp"
#include "../../features/misc/movement/movement.hpp"
#include "../../features/misc/other/other.hpp"

namespace MiscSettings {
inline void Configure() {
    using namespace evo::gui;

    // MOVEMENT + MOVEMENT EXTRA
    {
        const bits s = cfg.misc.autostrafe.get();
        Misc::Movement::Settings m;
        m.strafe_viewangles = s.test(cfg_t::autostrafe_viewangles);
        m.strafe_movement_keys = s.test(cfg_t::autostrafe_movement_keys);
        m.strafe_easy = s.test(cfg_t::autostrafe_easy_strafe);
        m.strafe_jump_release = s.test(cfg_t::autostrafe_jump_release);
        m.strafe_jump_release_after = s.test(cfg_t::autostrafe_jump_release_after);
        m.smoothing = static_cast<int>(cfg.misc.autostrafe_smoothing.get());
        m.avoid_walls = cfg.misc.avoid_walls.get();
        m.bhop = cfg.misc.bhop.get();
        m.quick_stop = cfg.legit.quick_stop.get();
        m.edge_jump = cfg.misc.edge_jump.get();
        m.edge_stop = cfg.misc.edge_stop.get();
        m.long_jump = cfg.misc.long_jump.get();
        m.ladder_jump = cfg.misc.ladder_jump.get();
        Misc::Movement::Publish(m);
    }

    // MISC (general)
    {
        const bits s = cfg.misc.log_types.get();
        Misc::General::Settings g;
        g.log_hit = s.test(0);
        g.log_buy = s.test(2);
        g.log_hurt = s.test(3);
        g.log_naded = s.test(4);
        g.console_logs = cfg.misc.console_logs.get();
        g.hitsound = static_cast<int>(cfg.misc.hitsound_select.get().first_set_bit().value_or(0));
        g.hit_sound_enabled = cfg.misc.hit_sound_enabled.get();
        g.kill_sound_enabled = cfg.misc.kill_sound_enabled.get();
        g.kill_sound = static_cast<int>(cfg.misc.kill_sound_select.get().first_set_bit().value_or(0));
        g.clantag = cfg.misc.clantag.get();
        g.ping_spike = cfg.misc.ping_spike_enabled.get();
        g.ping_spike_ms = cfg.aim.ping_spike_amt.get();
        g.reveal_enemy_chat = cfg.misc.reveal_enemy_chat.get();
        Misc::General::Publish(g);
        // Enemy team chat reveal: hand the lines the game thread queued over to
        // the in-game toast system (gui::notify renders them once the
        // notifications popup is up; adding to the list is always safe).
        {
            std::vector<Misc::General::ChatToast> toasts;
            Misc::General::Detail::DrainChatToasts(toasts);
            for (auto& toast : toasts)
                notify.add(std::make_shared<notification>(toast.header, toast.text));
        }
    }

    // OTHER
    {
        Misc::Other::Settings o;
        o.sv_pure = cfg.misc.sv_pure.get();
        o.sv_cheats = cfg.misc.sv_cheats.get();
        o.console_filter = cfg.misc.console_filter.get();
        o.autoaccept = cfg.misc.autoaccept.get();
        o.radar = cfg.misc.radar.get();
        Misc::Other::Publish(o);
    }

    // BUY BOT
    {
        Misc::Buybot::Settings b;
        b.primary = static_cast<int>(cfg.misc.buybot_primary.get().first_set_bit().value_or(0));
        b.pistol = static_cast<int>(cfg.misc.buybot_pistol.get().first_set_bit().value_or(0));
        b.armor = static_cast<int>(cfg.misc.buybot_kevlar_helmet.get().first_set_bit().value_or(0));
        b.defuser = cfg.misc.buybot_defuser.get();
        b.taser = cfg.misc.buybot_taser.get();
        b.grenades = cfg.misc.buybot_grenades.get();
        Misc::Buybot::Publish(b);
    }
}
} // namespace MiscSettings
