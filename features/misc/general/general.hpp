#pragma once
// The first Misc group box (id misc>general): logs, hitsound, clantag, ping spike and radar.
// One header for the folder, one translation unit per feature. The event entry points below
// are called from the global game-event listener (features/misc/events).
#include "../../../sdk/datatypes/game_event.hpp"

namespace Misc::General {
struct Settings {
    // Logs (event_logs.cpp) - our menu's log_types bit order: 0 hit, 1 miss, 2 buy, 3 hurt, 4 naded.
    bool log_hit{};
    bool log_buy{};
    bool log_hurt{};
    bool log_naded{};
    bool console_logs{};

    // Hitsound (hitsound.cpp) - selected preset (1..8), 0/none off.
    int hitsound{};
    bool hit_sound_enabled{};
    bool kill_sound_enabled{};
    int kill_sound{};

    // Clantag (clantag.cpp)
    bool clantag{};

    // Ping spike (ping.cpp)
    bool ping_spike{};
    float ping_spike_ms{}; // "Amount" slider (cfg.aim.ping_spike_amt)
};

void Publish(const Settings& settings);
void FrameStage(int stage);
void Shutdown();

// Event entry points (features/misc/events dispatches to these).
void OnRoundStart();
void OnPlayerHurt(const IGameEvent* event);
void OnPlayerDeath(const IGameEvent* event);
void OnItemPurchase(const IGameEvent* event);

namespace Detail {
// Published setting accessors, defined in general.cpp next to the atomics they read.
bool LogHit();
bool LogBuy();
bool LogHurt();
bool LogNaded();
bool ConsoleLogs();
int HitsoundSelect();
bool HitSoundEnabled();
bool KillSoundEnabled();
int KillSoundSelect();
bool ClantagEnabled();
bool PingEnabled();
float PingMs();

void PingReset();                                   // ping.cpp
bool PingInstall();                                 // ping.cpp
void PingRemove();                                  // ping.cpp
void LogRoundStart();                               // event_logs.cpp
void LogPlayerHurt(const IGameEvent* event);        // event_logs.cpp
void LogPlayerDeath(const IGameEvent* event);       // event_logs.cpp
void LogItemPurchase(const IGameEvent* event);      // event_logs.cpp
void HitSoundOnHurt(const IGameEvent* event);       // hitsound.cpp
void HitSoundOnDeath(const IGameEvent* event);      // hitsound.cpp
void ClantagFrameStage(int stage);                  // clantag.cpp
} // namespace Detail
} // namespace Misc::General
