#pragma once
// Visuals -> World -> "Fog".
//
// The engine's fog is not a renderer state this build can hook: it is a small family of
// client cvars (`fog_override`, `fog_color`, `fog_start`, `fog_end`, `fog_maxdensity`,
// `fog_enable`) that the client reads when it builds the fog for the frame. `main` drives
// exactly that pair of them for its own fog (`post_processing.cpp: override_fog`) - it turns
// `fog_override` on and writes the colour, leaving the map's density and range alone - and
// that is what this file does too, through the cvar layer instead of the console.
//
// Only the colour is taken from the menu, because the menu only offers a colour. The density
// and the start/end range stay whatever the map set, so switching the checkbox on recolours
// the map's own fog instead of replacing the whole look. Both cvars are captured before the
// first write and handed back when the checkbox goes off, when there is no game, and on
// unload - a fog that stays overridden after the cheat is gone would be worse than no
// feature at all.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <windows.h>

#include "../../../../sdk/convars.hpp"
#include "../../../../utils/log.hpp"

namespace WorldFog {
namespace Detail {
struct State {
    bool attempted{};
    bool ready{};
    ConVars::ConVar* override_var{};
    ConVars::ConVar* colour_var{};
    int saved_override{};
    char saved_colour[64]{};
    bool active{};
    std::uint32_t written_colour{};
};
inline State& StateRef() {
    static State state;
    return state;
}

inline bool Ensure() {
    State& state = StateRef();
    if (state.ready) return true;
    if (!Interfaces::ConVar || state.attempted) return false;
    state.attempted = true;
    state.override_var = ConVars::Find("fog_override");
    state.colour_var = ConVars::Find("fog_color");
    if (!state.override_var || !state.colour_var) {
        LOG_WARN("world: fog cvars not found (fog_override=%p fog_color=%p); the Fog "
                 "checkbox stays inactive", static_cast<void*>(state.override_var),
                 static_cast<void*>(state.colour_var));
        return false;
    }
    state.saved_override = state.override_var->GetInt();
    std::snprintf(state.saved_colour, sizeof(state.saved_colour), "%s",
                  ConVars::GetString(state.colour_var));
    state.ready = true;
    LOG_INFO("world: fog cvars captured (override %d, colour \"%s\")", state.saved_override,
             state.saved_colour);
    return true;
}

// evo::ren's colour packs as (r << 16) | (g << 8) | b | (a << 24); `fog_color` wants the
// three components as one space separated string.
inline void BuildColour(std::uint32_t rgba, char* out, std::size_t size) {
    std::snprintf(out, size, "%d %d %d", static_cast<int>((rgba >> 16) & 0xFFu),
                  static_cast<int>((rgba >> 8) & 0xFFu), static_cast<int>(rgba & 0xFFu));
}
} // namespace Detail

// Hands the two cvars back. Safe to call when nothing was ever written.
inline void Restore();

// Called once per FRAME_RENDER_START, before the engine builds the frame's fog.
inline void Apply(bool enabled, std::uint32_t rgba) {
    using namespace Detail;
    State& state = StateRef();
    if (!Ensure()) return;
    if (!enabled) {
        Restore();
        return;
    }
    // The game re-applies the map's own values on every level change, so the override is
    // re-asserted whenever it is found switched off again. The writes are change-driven:
    // nothing is written while the cvar already holds the value this module wants.
    if (state.override_var->GetInt() != 1) ConVars::SetInt(state.override_var, 1);
    if (rgba != state.written_colour) {
        char buffer[24];
        BuildColour(rgba, buffer, sizeof(buffer));
        if (std::strcmp(ConVars::GetString(state.colour_var), buffer) != 0)
            ConVars::SetString(state.colour_var, buffer);
        state.written_colour = rgba;
    }
    if (!state.active) {
        state.active = true;
        LOG_INFO("world: fog overridden (%08X)", rgba);
    }
}

// Hands the two cvars back. Safe to call when nothing was ever written.
inline void Restore() {
    using namespace Detail;
    State& state = StateRef();
    if (!state.active || !state.ready) return;
    ConVars::SetInt(state.override_var, state.saved_override);
    if (state.saved_colour[0])
        ConVars::SetString(state.colour_var, state.saved_colour);
    state.written_colour = 0;
    state.active = false;
    LOG_INFO("world: fog handed back to the game");
}
} // namespace WorldFog
