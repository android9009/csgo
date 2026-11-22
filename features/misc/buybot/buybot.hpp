#pragma once
// BUY BOT group box of the Misc tab. Ported from this repository's `main` branch
// (airflow) c_buy_bot: a `round_start` game event latches a single purchase, which is then
// issued as one `buy X; buy Y; ...` line through IEngineClient::ClientCmd. `main` buys the
// expensive groups first (rifle before grenade) because the server rejects whatever the
// player can no longer afford.
#include <string>

namespace Misc::Buybot {
// Mirror of menu/config.hpp cfg_t::buybot_primary / buybot_pistol / buybot_kevlar. The menu
// bridge resolves the selected combo to one of these and static_asserts the numeric match,
// so a reorder in the config enums is a compile error, not a silently wrong purchase.
enum PrimaryWeapon : int {
    PrimaryNone = 0,
    PrimaryAk47,    // "AK47 / M4A1"
    PrimarySg556,   // "SG-556"
    PrimaryAwp,
    PrimarySsg08,
    PrimaryScar20,  // "SCAR-20 / G3SG1"
    PrimaryNegev,
};
enum PistolWeapon : int {
    PistolNone = 0,
    PistolDeagle,  // "Desert Eagle"
    PistolElites,  // "Dual Berettas"
    PistolTec9,    // "Tec-9 / Five-SeveN"
    PistolP250,
    PistolCz75a,   // "CZ75-Auto"
    PistolRevolver, // "R8 Revolver"
};
enum ArmorKind : int {
    ArmorNone = 0,
    ArmorKevlar,       // "Kevlar"
    ArmorKevlarHelmet, // "Kevlar + Helmet"
};

// The selected combo values, resolved from the cfg_t enums by the menu bridge.
struct Settings {
    int primary{PrimaryNone};
    int pistol{PistolNone};
    int armor{ArmorNone};
    bool defuser{};
    bool taser{};
    bool grenades{};
};

void Publish(const Settings& settings);
// Set from the round_start game event; consumed once by Tick.
void OnRoundStart();
// Called from the frame stage once per tick; issues the latched purchase.
void Tick();
void Shutdown();
} // namespace Misc::Buybot
