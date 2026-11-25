#pragma once
// Penetration crosshair feature bridge (Visuals -> Misc -> Local).
#include "features/airflow_bridge.hpp"
namespace PenetrationCrosshair {
struct Settings {
    bool enabled{};
};
inline void Publish(const Settings& s) {
    g_cfg.misc.penetration_crosshair = s.enabled;
}
}
