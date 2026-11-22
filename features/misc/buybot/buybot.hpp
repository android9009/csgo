#pragma once
// The BUY BOT group box of the Misc tab (id misc>buybot).
//
// The feature's game-side logic (the freeze-period window, the command string
// and the ClientCmd_Unrestricted send) has been removed: the menu and the
// config keep the controls and the settings, and the menu bridge keeps
// publishing them here, but nothing in the game consumes them anymore.
// `Publish` is a no-op on purpose so the bridge and the config layer stay
// untouched. The enums below are the config's own values mirrored here, and
// menu/bridges/misc_runtime.hpp still static_asserts every one of them, so a
// reorder in the config enums is a compile error.
#include <cstddef>

namespace Misc::Buybot {

// Values of menu/config.hpp's cfg_t::buybot_primary / buybot_pistol /
// buybot_kevlar; the bridge static_asserts all of them.
enum Primary : int {
    PrimaryNone = 0,
    PrimaryAk47,    // "AK47 / M4A1"
    PrimarySg556,   // "SG-556"
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

// Called from the menu bridge on the render thread. The feature logic is gone,
// so the values are accepted and dropped.
inline void Publish(const Settings&) {}
} // namespace Misc::Buybot
