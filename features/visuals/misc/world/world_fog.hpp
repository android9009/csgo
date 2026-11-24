#pragma once
// Visuals -> World -> "Fog".
//
// The engine's fog is not a renderer state this build can hook: it is a small family of
// client cvars (`fog_override`, `fog_color`, `fog_start`, `fog_end`, `fog_maxdensity`) that
// the client reads while it builds the frame's fog. The `main` branch (airflow) drives the
// same family from the framestage hook, with two details that make the difference between
// "works in game" and "the map fights back":
//
//   1. The override is written at FRAME_RENDER_START *before* the original framestage
//      runs, so the game's own per-frame fog logic sees our values and cannot clobber them
//      later in the same frame.
//   2. The change callbacks of the fog cvars are removed first. In CS:GO the map's fog
//      state reacts to cvar changes through those callbacks; with the callbacks gone a
//      value we write stays the value the map sees.
//
// Only the colour is taken from the menu (the menu only offers a colour). The start/end
// range and the density are captured from the map once and re-asserted while the feature
// is on, so the map's own fog look is kept and only recoloured. All five cvars are handed
// back when the checkbox goes off, when there is no game, and on unload - a fog that stays
// overridden after the cheat is gone would be worse than no feature at all.
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
    ConVars::ConVar* start_var{};
    ConVars::ConVar* end_var{};
    ConVars::ConVar* density_var{};
    int saved_override{};
    char saved_colour[64]{};
    int saved_start{};
    int saved_end{};
    float saved_density{};
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
    state.start_var = ConVars::Find("fog_start");
    state.end_var = ConVars::Find("fog_end");
    state.density_var = ConVars::Find("fog_maxdensity");
    if (!state.override_var || !state.colour_var) {
        LOG_WARN("world: fog cvars not found (fog_override=%p fog_color=%p); the Fog "
                 "checkbox stays inactive", static_cast<void*>(state.override_var),
                 static_cast<void*>(state.colour_var));
        return false;
    }
    // The range/density cvars are optional (an older build without them): the override and
    // the colour still work, the map's range and density simply stay untouched.
    state.saved_override = state.override_var->GetInt();
    std::snprintf(state.saved_colour, sizeof(state.saved_colour), "%s",
                  ConVars::GetString(state.colour_var));
    state.saved_start = state.start_var ? state.start_var->GetInt() : 0;
    state.saved_end = state.end_var ? state.end_var->GetInt() : 0;
    state.saved_density = state.density_var ? state.density_var->GetFloat() : 0.f;
    // Kill the map's change-callback reactions on the whole fog family (airflow does this
    // for the same five). Safe to repeat; the header is only touched when plausible.
    state.override_var->RemoveChangeCallbacks();
    state.colour_var->RemoveChangeCallbacks();
    state.start_var->RemoveChangeCallbacks();
    state.end_var->RemoveChangeCallbacks();
    state.density_var->RemoveChangeCallbacks();
    state.ready = true;
    LOG_INFO("world: fog cvars captured (override %d, colour \"%s\", start %d, end %d, "
             "density %.3f; callbacks dropped)",
             state.saved_override, state.saved_colour, state.saved_start, state.saved_end,
             state.saved_density);
    return true;
}

// evo::ren's colour packs as (r << 16) | (g << 8) | b | (a << 24); `fog_color` wants the
// three components as one space separated string.
inline void BuildColour(std::uint32_t rgba, char* out, std::size_t size) {
    std::snprintf(out, size, "%d %d %d", static_cast<int>((rgba >> 16) & 0xFFu),
                  static_cast<int>((rgba >> 8) & 0xFFu), static_cast<int>(rgba & 0xFFu));
}
} // namespace Detail

// Hands the cvars back. Safe to call when nothing was ever written.
inline void Restore();

// Called once per FRAME_RENDER_START, before the original framestage runs.
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
    // nothing is written while a cvar already holds the value this module wants.
    if (state.override_var->GetInt() != 1) ConVars::SetInt(state.override_var, 1);
    if (state.start_var && state.start_var->GetInt() != state.saved_start)
        ConVars::SetInt(state.start_var, state.saved_start);
    if (state.end_var && state.end_var->GetInt() != state.saved_end)
        ConVars::SetInt(state.end_var, state.saved_end);
    if (state.density_var) {
        const float density = state.density_var->GetFloat();
        if (density != state.saved_density)
            ConVars::SetFloat(state.density_var, state.saved_density);
    }
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

// Hands the cvars back. Safe to call when nothing was ever written.
inline void Restore() {
    using namespace Detail;
    State& state = StateRef();
    if (!state.active || !state.ready) return;
    ConVars::SetInt(state.override_var, state.saved_override);
    if (state.saved_colour[0])
        ConVars::SetString(state.colour_var, state.saved_colour);
    if (state.start_var) ConVars::SetInt(state.start_var, state.saved_start);
    if (state.end_var) ConVars::SetInt(state.end_var, state.saved_end);
    if (state.density_var) ConVars::SetFloat(state.density_var, state.saved_density);
    state.written_colour = 0;
    state.active = false;
    LOG_INFO("world: fog handed back to the game");
}

// A level change replaces the fog state: drop the capture so the new map's values are
// re-captured before the next override.
inline void Invalidate() {
    using namespace Detail;
    State& state = StateRef();
    if (state.active) Restore();
    state = State{};
}
} // namespace WorldFog
