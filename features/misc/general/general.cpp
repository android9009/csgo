#include "general.hpp"
#include <atomic>

namespace Misc::General {
namespace {
std::atomic<bool> g_log_hit{false};
std::atomic<bool> g_log_buy{false};
std::atomic<bool> g_log_hurt{false};
std::atomic<bool> g_log_naded{false};
std::atomic<bool> g_console_logs{false};
std::atomic<int> g_hitsound{0};
std::atomic<bool> g_hit_sound_enabled{false};
std::atomic<bool> g_kill_sound_enabled{false};
std::atomic<int> g_kill_sound{0};
std::atomic<bool> g_clantag{false};
std::atomic<bool> g_ping_spike{false};
std::atomic<float> g_ping_ms{0.f};
} // namespace

void Publish(const Settings& settings) {
    g_log_hit.store(settings.log_hit);
    g_log_buy.store(settings.log_buy);
    g_log_hurt.store(settings.log_hurt);
    g_log_naded.store(settings.log_naded);
    g_console_logs.store(settings.console_logs);
    g_hitsound.store(settings.hitsound);
    g_hit_sound_enabled.store(settings.hit_sound_enabled);
    g_kill_sound_enabled.store(settings.kill_sound_enabled);
    g_kill_sound.store(settings.kill_sound);
    g_clantag.store(settings.clantag);
    g_ping_spike.store(settings.ping_spike);
    g_ping_ms.store(settings.ping_spike_ms);
    Detail::ChatRevealPublish(settings.reveal_enemy_chat);
}

namespace Detail {
bool LogHit() { return g_log_hit.load(); }
bool LogBuy() { return g_log_buy.load(); }
bool LogHurt() { return g_log_hurt.load(); }
bool LogNaded() { return g_log_naded.load(); }
bool ConsoleLogs() { return g_console_logs.load(); }
int HitsoundSelect() { return g_hitsound.load(); }
bool HitSoundEnabled() { return g_hit_sound_enabled.load(); }
bool KillSoundEnabled() { return g_kill_sound_enabled.load(); }
int KillSoundSelect() { return g_kill_sound.load(); }
bool ClantagEnabled() { return g_clantag.load(); }
bool PingEnabled() { return g_ping_spike.load(); }
float PingMs() { return g_ping_ms.load(); }
} // namespace Detail

void OnRoundStart() {
    Detail::PingReset();
    Detail::LogRoundStart();
    Detail::ChatRevealOnRoundStart();
}

void OnPlayerHurt(const IGameEvent* event) {
    Detail::LogPlayerHurt(event);
    Detail::HitSoundOnHurt(event);
}

void OnPlayerDeath(const IGameEvent* event) {
    Detail::LogPlayerDeath(event);
    Detail::HitSoundOnDeath(event);
}

void OnItemPurchase(const IGameEvent* event) { Detail::LogItemPurchase(event); }

void OnPlayerSay(const IGameEvent* event) { Detail::ChatRevealOnPlayerSay(event); }

void FrameStage(int stage) {
    Detail::ClantagFrameStage(stage);
    Detail::ChatRevealFrameStage(stage);
}

void Shutdown() {
    Detail::PingReset();
    Detail::ChatRevealShutdown();
}
} // namespace Misc::General
