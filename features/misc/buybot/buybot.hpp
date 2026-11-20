#pragma once
// The BUY BOT group box of the Misc tab (id misc>buybot).
//
// One folder per menu group box, one header for the folder, one translation unit for the
// logic. Everything here is portable: the weapon table, the command string and the
// once-per-round scheduler can be checked on a host without the game.
//
// Provenance
// ----------
//   - The command form, the priority order and the alternative-weapon pairs come from this
//     repository's `main` branch (buy_bot.hpp / buy_bot.cpp): one `buy X; ` string built in
//     the order primary -> secondary -> armor -> the rest, executed once through
//     IEngineClient::ClientCmd_Unrestricted.
//   - `main` triggers on the `round_start` game event. This build has no game-event
//     listener, so the window comes from the game rules object instead: c_game_rules'
//     `m_bFreezePeriod` (byte +0x20, the field `main` reads as is_freeze_time()), located
//     through the `game_rules` signature that `main` also carries. Freeze time is when
//     CS:GO accepts `buy` from the client, so it is the honest trigger - and unlike the
//     event it is a state that can be polled every tick, which lets the same code retry a
//     purchase the server rejected before the round had really started.
//   - The menu stores fewer groups than `main`'s bitmask (one "Grenades" checkbox instead
//     of four bits), so the grenade string buys the four main grenades. CZ75-Auto and the
//     R8 are separate menu entries here, so each maps to its own command instead of
//     riding along with the Desert Eagle.
//
// Everything runs from the ClientMode::CreateMove detour (the same hook movement uses), so
// the purchase is issued on the game thread at most kMaxAttempts times per buy window.
#include <atomic>
#include <cstddef>
#include <string>

namespace Misc::Buybot {

// Values of menu/config.hpp's cfg_t::buybot_primary / buybot_pistol / buybot_kevlar.
// menu/bridges/misc_runtime.hpp static_asserts every one of them, so a reorder in the
// config enums is a compile error instead of a silently wrong purchase.
enum Primary : int {
    PrimaryNone = 0,
    PrimaryAk47,    // "AK47 / M4A1"  -> ak47, m4a1, m4a1_silencer
    PrimarySg556,   // "SG-556"       -> aug, sg556
    PrimaryAwp,
    PrimarySsg08,
    PrimaryScar20,  // "SCAR-20 / G3SG1"
    PrimaryNegev,
};

enum Pistol : int {
    PistolNone = 0,
    PistolDeagle,
    PistolElites,   // Dual Berettas
    PistolTec9,     // "Tec-9 / Five-SeveN"
    PistolP250,
    PistolCz75a,
    PistolRevolver,
};

enum Armor : int {
    ArmorNone = 0,
    ArmorKevlar,
    ArmorKevlarHelmet,
};

struct Settings {
    int primary{PrimaryNone};
    int pistol{PistolNone};
    int armor{ArmorNone};
    bool defuser{};
    bool taser{};
    bool grenades{};
};

// True when at least one group would buy something. The group box has no master switch:
// an empty selection means "off", exactly like the Auto strafe combo box.
inline bool Configured(const Settings& settings) {
    if (settings.primary > PrimaryNone && settings.primary <= PrimaryNegev) return true;
    if (settings.pistol > PistolNone && settings.pistol <= PistolRevolver) return true;
    if (settings.armor > ArmorNone && settings.armor <= ArmorKevlarHelmet) return true;
    return settings.defuser || settings.taser || settings.grenades;
}

namespace Detail {
inline void Append(std::string& out, const char* command) {
    if (!command || !*command) return;
    if (!out.empty()) out += ' ';
    out += "buy ";
    out += command;
    out += ';';
}
} // namespace Detail

// The command line `main` would send, without the trailing space:
//   "buy ak47; buy m4a1; buy m4a1_silencer; buy vesthelm; buy hegrenade; ..."
// Empty when nothing is configured. Buying the expensive groups first is `main`'s order
// and matters: the server rejects a purchase the player can no longer afford, so money
// left over must go to the rifle rather than to a grenade.
inline std::string BuildCommand(const Settings& settings) {
    std::string command;
    // main's primary table. The pairs are "buy the CT one, then the T one": an invalid
    // purchase for the current team is rejected by the server and the next command in the
    // same line still runs, so one string covers both sides.
    switch (settings.primary) {
    case PrimaryAk47: Detail::Append(command, "ak47"); Detail::Append(command, "m4a1"); Detail::Append(command, "m4a1_silencer"); break;
    case PrimarySg556: Detail::Append(command, "aug"); Detail::Append(command, "sg556"); break;
    case PrimaryAwp: Detail::Append(command, "awp"); break;
    case PrimarySsg08: Detail::Append(command, "ssg08"); break;
    case PrimaryScar20: Detail::Append(command, "scar20"); Detail::Append(command, "g3sg1"); break;
    case PrimaryNegev: Detail::Append(command, "negev"); break;
    default: break;
    }
    switch (settings.pistol) {
    case PistolDeagle: Detail::Append(command, "deagle"); break;
    case PistolElites: Detail::Append(command, "elite"); break;
    case PistolTec9: Detail::Append(command, "tec9"); Detail::Append(command, "fn57"); break;
    case PistolP250: Detail::Append(command, "p250"); break;
    case PistolCz75a: Detail::Append(command, "cz75a"); break;
    case PistolRevolver: Detail::Append(command, "revolver"); break;
    default: break;
    }
    switch (settings.armor) {
    case ArmorKevlar: Detail::Append(command, "vest"); break;
    case ArmorKevlarHelmet: Detail::Append(command, "vesthelm"); break;
    default: break;
    }
    if (settings.defuser) Detail::Append(command, "defuser");
    if (settings.taser) Detail::Append(command, "taser");
    // One menu checkbox for main's four grenade bits (he / molotov+inc / smoke) plus the
    // flashbang, which main's bitmask did not cover.
    if (settings.grenades) {
        Detail::Append(command, "hegrenade");
        Detail::Append(command, "molotov");
        Detail::Append(command, "incgrenade");
        Detail::Append(command, "smokegrenade");
        Detail::Append(command, "flashbang");
    }
    return command;
}

// One buy window (a freeze period). `attempts` is capped so a rejected purchase cannot
// turn into a command every tick.
struct Window {
    bool open{};
    int attempts{};
    float last_attempt{-1.f};

    // Tracks the window edges. Returns true on the tick the window opens.
    bool Update(bool is_open) {
        const bool started = is_open && !open;
        open = is_open;
        if (!is_open) {
            attempts = 0;
            last_attempt = -1.f;
        }
        return started;
    }
    void Note(float curtime) {
        ++attempts;
        last_attempt = curtime;
    }
};

// `main` buys once and stops; a single attempt is fired the moment the round starts, which
// on a listen server can precede the first tick the server accepts purchases on. Retrying
// twice more, one second apart, inside the same window costs nothing when the first
// attempt worked (the server rejects duplicates) and saves the round when it did not.
inline constexpr int kMaxAttempts = 3;
inline constexpr float kRetryDelay = 1.f;

// Portable decision: buy only while the window is open, only for a live local player in a
// running game, and never more than kMaxAttempts times per window.
inline bool ShouldAttempt(const Settings& settings, bool in_game, bool alive,
                          bool window_open, const Window& window, float curtime) {
    if (!in_game || !alive || !window_open) return false;
    if (!Configured(settings)) return false;
    if (window.attempts >= kMaxAttempts) return false;
    if (window.attempts == 0) return true;
    if (curtime < window.last_attempt) return true; // the clock restarted (new map)
    return curtime - window.last_attempt >= kRetryDelay;
}

void Publish(const Settings& settings);
// Called from the ClientMode::CreateMove detour.
void Tick();
void Shutdown();

namespace Detail {
// c_game_rules::m_bFreezePeriod - byte +0x20, the offset `main` uses.
inline constexpr std::ptrdiff_t kFreezePeriodOffset = 0x20;
// The client's game rules object, or null when the signature did not resolve.
void* GameRules();
bool FreezePeriod(void* rules);
} // namespace Detail
} // namespace Misc::Buybot
