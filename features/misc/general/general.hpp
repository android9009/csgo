#pragma once
// The first group box of the Misc tab ("MISC", group id misc>general).
//
// The group's game-side logic (radar reveal, clan tag, reveal enemy chat and ping
// spike) has been removed: the menu and the config keep the controls and the
// settings, and the menu bridge keeps publishing them here, but nothing in the
// game consumes them anymore. `Publish` is a no-op on purpose so the bridge and
// the config layer stay untouched.
#include <cstdint>

namespace Misc::General {
struct Settings {
    bool radar{};
    bool clantag{};

    // "Auto-accept" lives in Misc::Other::Settings: its checkbox is in the OTHER group.
    bool reveal_enemy_chat{};  // "Reveal enemy chat"

    bool ping_spike{};
    float ping_spike_ms{}; // "Ping spike > Amount", milliseconds
};

// Called from the menu bridge on the render thread. The feature logic is gone,
// so the values are accepted and dropped.
inline void Publish(const Settings&) {}
} // namespace Misc::General
