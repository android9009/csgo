#pragma once
// The "OTHER" group box of the Misc tab (group id misc>other): auto-accept,
// console filter and the sv_pure / sv_cheats switches.
//
// The group's game-side logic (the accept-popup sound trigger, the tier0
// console detour and the ConVar attempts) has been removed: the menu and the
// config keep the controls and the settings, and the menu bridge keeps
// publishing them here, but nothing in the game consumes them anymore.
// `Publish` is a no-op on purpose so the bridge and the config layer stay
// untouched.

namespace Misc::Other {
struct Settings {
    bool auto_accept{};
    bool console_filter{};
    bool sv_pure{};
    bool sv_cheats{};
};

// Called from the menu bridge on the render thread. The feature logic is gone,
// so the values are accepted and dropped.
inline void Publish(const Settings&) {}
} // namespace Misc::Other
