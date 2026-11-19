#pragma once
// The first group box of the Misc tab ("MISC", group id misc>general).
//
// One folder per menu group box, ONE header for the whole folder and one translation
// unit per feature: this file declares everything the folder does and the portable
// rules the features are built on, while all the logic lives in the five .cpp files next
// to it. Nothing here includes Windows or engine types, so the portable tests can use
// any of it on a host that has no game.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Façade: the only part the menu bridge and Misc::FrameStage talk to.
// ---------------------------------------------------------------------------
namespace Misc::General {
struct Settings {
    bool radar{};
    bool clantag{};

    // "Scoreboard" combo box mask; the bit order is pinned in the Scoreboard section
    // below and by tests/scoreboard_test.cpp.
    std::uint64_t scoreboard{};

    // "Scoreboard > settings" sliders, in percent (50..200).
    int scoreboard_scale_x{100};
    int scoreboard_scale_y{100};
    int scoreboard_scale_weapon{100};

    bool auto_accept{};        // "Auto-accept"
    bool reveal_enemy_chat{};  // "Reveal enemy chat"
    bool ragdoll_thrust{};     // "Ragdoll thrust"
    float ragdoll_percent{100.f}; // "Ragdoll thrust > Amount" (1..1000 %)

    bool ping_spike{};
    float ping_spike_ms{}; // "Ping spike > Amount", milliseconds
};

// Called from the menu bridge on the render thread; forwards each value to the feature
// that owns it.
void Publish(const Settings& settings);

// Called on the game thread from Misc::FrameStage.
void FrameStage(int stage);

// Called once during unload.
void Shutdown();
} // namespace Misc::General

// ---------------------------------------------------------------------------
// Radar (radar.cpp): marks players as spotted so they appear on the local radar.
// ---------------------------------------------------------------------------
namespace Misc::Radar {
// Playable teams in CS:GO: T = 2, CT = 3.
constexpr int kTeamTerrorist = 2;
constexpr int kTeamCounterTerrorist = 3;

struct Target {
    bool dormant{};
    bool alive{};
    bool local{};
    int team{};
};

// Only living, non-dormant players of a playable team are worth writing to: the local
// player is already on his own radar, and dormant entities carry stale data.
inline bool ShouldSpot(const Target& target) {
    return !target.dormant && target.alive && !target.local &&
           (target.team == kTeamTerrorist || target.team == kTeamCounterTerrorist);
}

void SetEnabled(bool enabled);
void FrameStage(int stage); // stage 4 (net update end) only
} // namespace Misc::Radar

// ---------------------------------------------------------------------------
// Clan tag (clantag.cpp): writes the tag through engine.dll's SetClanTag.
// ---------------------------------------------------------------------------
namespace Misc::ClanTag {
// The Misc tab exposes a plain on/off switch, so the tag itself lives here.
// The engine limit is 15 characters.
inline constexpr const char* kTag = "pastavar";

// How often the tag is re-asserted while in game. Every call is an engine string update
// that the server rate-limits, so this is deliberately coarse.
inline constexpr std::uint32_t kReapplyIntervalMs = 5000;

// Re-applied on a first enable, a tag change, a new local player entity (map change) or
// after the interval - never every frame.
inline bool ShouldApply(bool enabled, bool applied, const char* applied_tag,
                        const char* desired_tag, std::uintptr_t applied_local,
                        std::uintptr_t local, std::uint32_t now_ms, std::uint32_t last_ms,
                        std::uint32_t interval_ms) {
    if (!enabled || !local) return false;
    if (!applied) return true;
    if (!applied_tag || !desired_tag || std::strcmp(applied_tag, desired_tag) != 0) return true;
    if (applied_local != local) return true;
    // Unsigned subtraction keeps this correct across the 49-day tick wraparound.
    return static_cast<std::uint32_t>(now_ms - last_ms) >= interval_ms;
}

// The tag is restored exactly once, when the feature is switched off.
inline bool ShouldRestore(bool enabled, bool applied) { return applied && !enabled; }

void SetEnabled(bool enabled);
void FrameStage(int stage); // stages 0/4/5
void Shutdown();            // restores the tag this module owns
} // namespace Misc::ClanTag

// ---------------------------------------------------------------------------
// Scoreboard (scoreboard.cpp + scoreboard_script.cpp)
//
// Rank reveal: asks the server for the rank table through the client's own
// ServerRankRevealAll handler. Weapon reveal: draws the equipment row with JavaScript
// injected into the Panorama HUD context (docs/panorama.md).
// ---------------------------------------------------------------------------
namespace Misc::Scoreboard {
// Item order of the "Scoreboard" combo box in menu/tabs/tab_misc.cpp
// ("Reveal ranks", "Reveal weapon", "Reveal unmatched ranks", "Reveal money") is the
// bit order.
namespace Bits {
inline constexpr int RevealRanks = 0;
inline constexpr int RevealWeapon = 1;
inline constexpr int RevealUnmatched = 2;
inline constexpr int RevealMoney = 3;
} // namespace Bits

// Every rank-reveal send makes the server answer with the rank table, so it is
// throttled far below the tick rate instead of being sent every frame.
inline constexpr std::uint32_t kRevealIntervalMs = 1000;

// The equipment script is re-fed while the scoreboard is open; each feed is a small
// script execution, so twice a second is both plenty and cheap.
inline constexpr std::uint32_t kFeedIntervalMs = 500;

constexpr bool Enabled(std::uint64_t mask, int bit) {
    return bit >= 0 && bit < 64 && ((mask >> bit) & 1ull) != 0ull;
}

// The reveal only makes sense while a match is live, and only once per interval.
constexpr bool ShouldRevealRanks(bool enabled, bool in_game, std::uint32_t now_ms,
                                 std::uint32_t last_ms, std::uint32_t interval_ms) {
    if (!enabled || !in_game || interval_ms == 0) return false;
    // Unsigned subtraction, so the comparison survives the GetTickCount() wrap.
    return static_cast<std::uint32_t>(now_ms - last_ms) >= interval_ms;
}

// ---- the injected script -------------------------------------------------
// The sliders are 50..200 percent; anything outside is clamped so the emitted CSS can
// never produce a negative or absurd size.
constexpr int kScaleMin = 50;
constexpr int kScaleMax = 200;
constexpr int kScaleDefault = 100;
int ClampScale(int percent);

struct Scales {
    int x{kScaleDefault};      // "Horizontal scale" - row width and per-weapon widths
    int y{kScaleDefault};      // "Vertical scale"   - armor and helmet icons
    int weapon{kScaleDefault}; // "Weapon scale"     - weapon icons
};

// One player's equipment in the form the script draws it. `weapons` holds icon names,
// never console names: every entry comes from WeaponIcon(), so no string reaching the
// script can be attacker controlled.
struct Equipment {
    int slot{};       // entity index
    int armor{};      // 0 none, 1 kevlar, 2 kevlar + helmet
    bool defuser{};
    int money{-1};    // -1 when "Reveal money" is off or the value is not readable
    int weapon_count{};
    int selected{-1}; // index into weapons, -1 when nothing is selected
    const char* weapons[8]{};
};

// Icon name for an item definition index (7 -> "ak47"), or nullptr when the item has no
// scoreboard icon. Names follow the client's icon files: the console name without its
// "item_"/"weapon_" prefix.
const char* WeaponIcon(int definition);

// Minimum width, in unscaled pixels, that a given icon needs to look right.
// 0 means "let the icon size itself".
int WeaponBaseWidth(const char* icon);

// Installer for the HUD context. Idempotent per layout: calling it again with the same
// scales does nothing, calling it with new scales destroys the previous rows first, so
// moving a slider takes effect on the next feed.
std::string BuildEngineScript(const Scales& scales);

// Engine + one data push - the only script that runs while "Reveal weapon" is on.
std::string BuildFeedScript(const std::vector<Equipment>& players, const Scales& scales);

// Removes the injected rows and unregisters the handlers.
std::string BuildResetScript();

// ---- runtime -------------------------------------------------------------
void Publish(std::uint64_t mask, int scale_x, int scale_y, int scale_weapon);
void FrameStage(int stage); // stage 4 only
} // namespace Misc::Scoreboard

// ---------------------------------------------------------------------------
// Reveal enemy chat (chat.cpp): hooks IBaseClientDll::DispatchUserMessage (slot 38,
// the vtable that also holds FrameStageNotify 37) and re-addresses a team chat message
// as all-chat, so the HUD prints what the server sent but the client hid.
//
// Only the message name field is replaced; every other byte of the payload is copied
// verbatim, because the published descriptions of this layout disagree about its header.
// ---------------------------------------------------------------------------
namespace Misc::Chat {
// A chat name is a one-byte length plus the name itself ("Cstrike_Chat_CT_Loc").
constexpr int kNameScanWindow = 32;

// Rewrites team names and leaves all-chat names alone: "Cstrike_Chat_All" is already
// printed for everybody, and rewriting it would only duplicate the line.
inline bool ShouldRevealChat(bool enabled, const char* name, int name_length) {
    if (!enabled || !name || name_length <= 0) return false;
    if (name_length <= 13 || std::strncmp(name, "Cstrike_Chat_", 13) != 0) return false;
    // "Cstrike_Chat_All", "Cstrike_Chat_All_Dead", "Cstrike_Chat_All_Spec".
    if (name_length >= 16 && std::strncmp(name + 13, "All", 3) == 0) return false;
    return true;
}

void SetEnabled(bool enabled);
// Installs the vtable patch from the game thread once the switch was published.
void FrameStage(int stage);
void Shutdown();
} // namespace Misc::Chat

// ---------------------------------------------------------------------------
// Ragdoll thrust (ragdoll.cpp): scales m_vecForce and m_vecRagdollVelocity on every
// CCSRagdoll, so bodies fly "further" without touching anything else.
// ---------------------------------------------------------------------------
namespace Misc::Ragdoll {
// The menu slider runs 1..1000 %.
constexpr int kPercentMin = 1;
constexpr int kPercentMax = 1000;
// Above this speed the ragdoll is left alone: the write is not idempotent, so without a
// ceiling a per-frame multiply would launch it into space.
constexpr float kSpeedLimit = 2000.f;

inline float ScalePercent(int percent) {
    if (percent < kPercentMin) return static_cast<float>(kPercentMin);
    if (percent > kPercentMax) return static_cast<float>(kPercentMax);
    return static_cast<float>(percent);
}

// Scaling is idempotent only while the ragdoll is slow: past the limit the value is left
// alone, so repeated frames cannot accelerate it into an explosion. A NaN velocity (an
// uninitialised vector) fails the comparison and is therefore refused as well.
inline bool ShouldScale(float speed, float limit) {
    if (!(speed >= 0.f)) return false;
    return speed < limit;
}

// Called from the menu bridge on the render thread.
void Publish(bool enabled, float percent);

// Called on the game thread from FrameStageNotify (stage 4, every fourth frame).
void FrameStage(int stage);
} // namespace Misc::Ragdoll

// ---------------------------------------------------------------------------
// Ping spike (ping.cpp): hooks CNetChan::SendDatagram and CNetChan::ProcessPacket and
// lowers the incoming sequence number, so the server measures the local client as
// laggier than it is. Ported from this repository's `main` branch: detour_hooks.cpp
// (send_datagram / process_packet), fake_lag.cpp (c_ping_spike) and structs.hpp
// (c_net_channel offsets).
// ---------------------------------------------------------------------------
namespace Misc::Ping {
// The menu slider runs 0..200 ms; anything above the ceiling is clamped, because the
// server rejects a shift larger than its own unlag budget anyway.
constexpr int kMaxSpikeMs = 200;

// sv_maxunlag defaults to 1.0 s in CS:GO. It is read from the ConVar when the read is
// plausible; these bounds are what decides "plausible".
constexpr float kDefaultMaxUnlag = 1.0f;
constexpr float kMinMaxUnlag = 0.05f;
constexpr float kMaxMaxUnlag = 2.0f;

// TIME_TO_TICKS, with the interval guarded: before the first map load it is zero, and
// dividing by it would be undefined.
inline int TicksForSeconds(float seconds, float interval) {
    if (!(interval > 0.f) || !(seconds > 0.f)) return 0;
    return static_cast<int>(0.5f + seconds / interval);
}

// `main` turns the requested delay into a delay *on top of* what the connection already
// has (outgoing latency and interpolation), then clamps it to the server's unlag budget.
// Parts that could not be read arrive as 0 and are simply skipped.
inline float SpikeSeconds(float desired_ms, float outgoing_latency, float lerp_time,
                          float max_unlag) {
    if (!(desired_ms > 0.f)) return 0.f;
    float correct = desired_ms / 1000.f;
    if (outgoing_latency > 0.f) correct -= outgoing_latency;
    if (lerp_time > 0.f) correct -= lerp_time;
    if (!(correct > 0.f)) return 0.f;
    if (max_unlag > 0.f && correct > max_unlag) return max_unlag;
    return correct;
}

// The incoming sequence number is only ever lowered, and only while there is room: a
// value below the shift would wrap into negative territory and reconnect the client.
inline bool ShouldSpike(int in_sequence_nr, int ticks) {
    return ticks > 0 && in_sequence_nr > ticks;
}

// Called from the menu bridge on the render thread.
void Publish(bool enabled, float milliseconds);

// Installs the two detours from the game thread once the switch was published.
void FrameStage(int stage);
void Shutdown();
} // namespace Misc::Ping
