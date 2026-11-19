#pragma once
// The "OTHER" group box of the Misc tab (group id misc>other).
//
// One header for the folder, one translation unit per feature; the portable rules are
// inline here so the host tests can exercise them without Windows or the engine.
#include <cstring>

namespace Misc::Other {
struct Settings {
    bool auto_accept{};
    bool console_filter{};
    bool sv_pure{};
    bool sv_cheats{};
};

// Called from the menu bridge on the render thread.
void Publish(const Settings& settings);

// Called on the game thread from Misc::FrameStage; installs the hooks once their switch
// has been published, because patching code from the render thread would race the game.
void FrameStage(int stage);

// Called once during unload: removes the hooks this group installed.
void Shutdown();
} // namespace Misc::Other

// ---------------------------------------------------------------------------
// Auto-accept (accept.cpp): when the match accept popup beeps, the client's
// SetLocalPlayerReady is called so the match is accepted without the user clicking.
// The trigger is IEngineSoundClient::EmitSound1 (slot 5); the matchmaker offers no
// other callback that is reachable from a client DLL.
// ---------------------------------------------------------------------------
namespace Misc::Other::Accept {
// The sample the accept popup plays; nothing else is intercepted.
inline bool IsAcceptSound(const char* sound_entry) {
    return sound_entry && std::strcmp(sound_entry, "UIPanorama.popup_accept_match_beep") == 0;
}

void SetEnabled(bool enabled);
void FrameStage(int stage);
void Shutdown();
} // namespace Misc::Other::Accept

// ---------------------------------------------------------------------------
// Console filter (console_filter.cpp): hooks the tier0 print path and drops the
// connection/queue chatter CS:GO writes into the developer console before the server
// has even been joined. The list is deliberately short and literal: it must never hide
// something the player may need to read.
// ---------------------------------------------------------------------------
namespace Misc::Other::ConsoleFilter {
inline constexpr const char* kFiltered[] = {
    "Connecting to ",                 // "Connecting to <server ip>"
    "Server using ",                  // server rules echo on connect
    "Connection to Steam servers",    // socket/Steam3 handshake noise
    "Assigned anonymous gameserver",  // matchmaking chatter
    "Assigned persistent gameserver", // matchmaking chatter
};

// Case-sensitive prefix match against the list above. A null or empty line is kept.
inline bool ShouldFilter(const char* line) {
    if (!line || !*line) return false;
    for (const char* prefix : kFiltered)
        if (std::strncmp(line, prefix, std::strlen(prefix)) == 0) return true;
    return false;
}

void SetEnabled(bool enabled);
void FrameStage(int stage);
void Shutdown();
} // namespace Misc::Other::ConsoleFilter

// ---------------------------------------------------------------------------
// sv_pure / sv_cheats (reported by Publish): both need a verified way to reach a
// ConVar, which this build does not have yet. They are not faked; enabling either one
// logs why it does nothing and docs/misc-runtime.md records what is missing.
// ---------------------------------------------------------------------------
