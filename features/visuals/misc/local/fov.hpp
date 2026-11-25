#pragma once
// Fov changer feature bridge (Visuals -> Misc -> Local).
#include "features/airflow_bridge.hpp"
namespace FovChanger {
struct Settings {
    bool enabled{};
    int fov{};
    bool aspect{};
    bool remove_zoom{};
};
inline void Publish(const Settings& s) {
    // airflow: misc.fovs[] = { world, arms, zoom }; the changer drives world fov.
    if (s.enabled) g_cfg.misc.fovs[fov_t::world] = s.fov;
}
}
