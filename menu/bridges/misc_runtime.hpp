#pragma once
// Publishes the Misc tab settings into the tab façade. Called from the render path
// next to VisualSettings::Configure(), so the hooks never read GUI state.
#include <cstdint>

#include "../config.hpp"
#include "../../features/misc/buybot/buybot.hpp"
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
    // is a mode selector, so any of its three items means "on" - the same way the combo
    // itself stores the value. (The old "Viewangles" item is gone: it wrote the command's
    // angles and the camera followed them.)
    Misc::Movement::Settings movement;
    movement.strafe_movement_keys = cfg.misc.autostrafe->test(cfg_t::autostrafe_movement_keys);
    movement.strafe_easy = cfg.misc.autostrafe->test(cfg_t::autostrafe_easy_strafe);
    movement.strafe_jump_release = cfg.misc.autostrafe->test(cfg_t::autostrafe_jump_release);
    movement.autostrafe = movement.strafe_movement_keys || movement.strafe_easy ||
                          movement.strafe_jump_release;
    movement.smoothing = static_cast<int>(cfg.misc.autostrafe_smoothing.get());
    movement.avoid_walls = cfg.misc.avoid_walls.get();
    movement.bhop = cfg.misc.bhop.get();
    movement.quick_stop = cfg.legit.quick_stop.get();
    movement.edge_jump = cfg.misc.edge_jump.get();
    movement.edge_stop = cfg.misc.edge_stop.get();
    Misc::Movement::Publish(movement);

    // The BUY BOT group box. No master switch: a completely empty selection is "off".
    // The static_asserts keep the feature's own enums aligned with cfg_t, so reordering a
    // config enum cannot silently change which weapon is bought.
    static_assert(static_cast<int>(cfg_t::buybot_primary_none) == static_cast<int>(Misc::Buybot::PrimaryNone));
    static_assert(static_cast<int>(cfg_t::buybot_primary_ak47) == static_cast<int>(Misc::Buybot::PrimaryAk47));
    static_assert(static_cast<int>(cfg_t::buybot_primary_sg556) == static_cast<int>(Misc::Buybot::PrimarySg556));
    static_assert(static_cast<int>(cfg_t::buybot_primary_awp) == static_cast<int>(Misc::Buybot::PrimaryAwp));
    static_assert(static_cast<int>(cfg_t::buybot_primary_ssg08) == static_cast<int>(Misc::Buybot::PrimarySsg08));
    static_assert(static_cast<int>(cfg_t::buybot_primary_scar20) == static_cast<int>(Misc::Buybot::PrimaryScar20));
    static_assert(static_cast<int>(cfg_t::buybot_primary_negev) == static_cast<int>(Misc::Buybot::PrimaryNegev));
    static_assert(static_cast<int>(cfg_t::buybot_pistol_none) == static_cast<int>(Misc::Buybot::PistolNone));
    static_assert(static_cast<int>(cfg_t::buybot_pistol_deagle) == static_cast<int>(Misc::Buybot::PistolDeagle));
    static_assert(static_cast<int>(cfg_t::buybot_pistol_elites) == static_cast<int>(Misc::Buybot::PistolElites));
    static_assert(static_cast<int>(cfg_t::buybot_pistol_tec9) == static_cast<int>(Misc::Buybot::PistolTec9));
    static_assert(static_cast<int>(cfg_t::buybot_pistol_p250) == static_cast<int>(Misc::Buybot::PistolP250));
    static_assert(static_cast<int>(cfg_t::buybot_pistol_cz75a) == static_cast<int>(Misc::Buybot::PistolCz75a));
    static_assert(static_cast<int>(cfg_t::buybot_pistol_revolver) == static_cast<int>(Misc::Buybot::PistolRevolver));
    static_assert(static_cast<int>(cfg_t::buybot_kevlar_none) == static_cast<int>(Misc::Buybot::ArmorNone));
    static_assert(static_cast<int>(cfg_t::buybot_kevlar_k) == static_cast<int>(Misc::Buybot::ArmorKevlar));
    static_assert(static_cast<int>(cfg_t::buybot_kevlar_kh) == static_cast<int>(Misc::Buybot::ArmorKevlarHelmet));
    Misc::Buybot::Settings buybot;
    // The three combo boxes are single-selection bit sets, so the stored value is the
    // index of the one set bit (0 = "None").
    buybot.primary = static_cast<int>(cfg.misc.buybot_primary.get().first_set_bit().value_or(0));
    buybot.pistol = static_cast<int>(cfg.misc.buybot_pistol.get().first_set_bit().value_or(0));
    buybot.armor = static_cast<int>(cfg.misc.buybot_kevlar_helmet.get().first_set_bit().value_or(0));
    buybot.defuser = cfg.misc.buybot_defuser.get();
    buybot.taser = cfg.misc.buybot_taser.get();
    buybot.grenades = cfg.misc.buybot_grenades.get();
    Misc::Buybot::Publish(buybot);

    Misc::Other::Settings other;
    other.auto_accept = cfg.misc.autoaccept.get();
    other.console_filter = cfg.misc.console_filter.get();
    other.sv_pure = cfg.misc.sv_pure.get();
    other.sv_cheats = cfg.misc.sv_cheats.get();
    Misc::Other::Publish(other);
}
} // namespace MiscSettings
