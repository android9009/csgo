#pragma once
// Grenade prediction + proximity warning feature bridge (Visuals -> Misc -> Local).
#include "features/airflow_bridge.hpp"
namespace GrenadeWarning {
struct Settings {
    bool prediction{};
    uint32_t prediction_hit_color{};
    uint32_t prediction_miss_color{};
    bool warning{};
    uint32_t warning_color{};
};
inline void Publish(const Settings& s) {
    g_cfg.visuals.grenade_predict = s.prediction;
    g_cfg.visuals.predict_clr = fatality_bridge::unpack(s.prediction_hit_color);
    g_cfg.visuals.grenade_warning = s.warning;
    g_cfg.visuals.warning_clr = fatality_bridge::unpack(s.warning_color);
}
}
