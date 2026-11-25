#pragma once
// Misc -> Other feature bridge.
#include "features/airflow_bridge.hpp"
namespace Misc {
namespace Other {
struct Settings {
    bool sv_pure{};
    bool sv_cheats{};
    bool console_filter{};
    bool autoaccept{};
    bool radar{};
};
inline void Publish(const Settings& s) {
    g_cfg.misc.force_radar = s.radar;
    g_cfg.misc.bypass_sv_pure = s.sv_pure;
    g_cfg.misc.unlock_hidden_cvars = s.sv_cheats;
}
}
}
