#pragma once
// Misc -> General feature bridge.
#include "features/airflow_bridge.hpp"
#include <string>
#include <vector>
namespace Misc {
namespace General {
struct ChatToast {
    std::string header;
    std::string text;
};
struct Settings {
    bool log_hit{};
    bool log_buy{};
    bool log_hurt{};
    bool log_naded{};
    bool console_logs{};
    int hitsound{};
    bool hit_sound_enabled{};
    bool kill_sound_enabled{};
    int kill_sound{};
    bool clantag{};
    bool ping_spike{};
    int ping_spike_ms{};
    bool reveal_enemy_chat{};
};
inline void Publish(const Settings& s) {
    // Event logs: airflow bits { hurt=1, buy=8, bomb/nade=16 }.
    unsigned logs = 0;
    if (s.log_hurt) logs |= 1;
    if (s.log_buy) logs |= 8;
    if (s.log_naded) logs |= 16;
    g_cfg.visuals.eventlog.logs = logs;
    g_cfg.visuals.eventlog.enable = logs != 0;
    g_cfg.visuals.eventlog.filter_console = s.console_logs;
    g_cfg.misc.clantag = s.clantag;
}
namespace Detail {
// Enemy chat reveal would queue lines from the game thread; that capture path is
// not ported, so the drain is a no-op (the toast list stays empty).
inline void DrainChatToasts(std::vector<ChatToast>& toasts) {
    toasts.clear();
}
}
}
}
