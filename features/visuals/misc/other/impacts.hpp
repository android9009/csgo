#pragma once
// Bullet impact feature bridge (Visuals -> Misc -> Other).
#include "features/airflow_bridge.hpp"
namespace Impacts {
struct Settings {
    bool enabled{};
    uint32_t color1{};
    uint32_t color2{};
};
inline void Publish(const Settings& s) {
    g_cfg.misc.impacts = s.enabled;
    g_cfg.misc.server_clr = fatality_bridge::unpack(s.color1);
    g_cfg.misc.client_clr = fatality_bridge::unpack(s.color2);
}
}
