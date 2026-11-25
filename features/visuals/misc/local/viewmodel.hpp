#pragma once
// Viewmodel changer feature bridge (Visuals -> Misc -> Local).
#include "features/airflow_bridge.hpp"
namespace ViewModelChanger {
struct Settings {
    bool enabled{};
    float fov{};
    float x{};
    float y{};
    float z{};
    float roll{};
    float yaw{};
    float pitch{};
};
inline void Publish(const Settings& s) {
    // airflow: misc.viewmodel_pos[3] = { x, y, z } (v_viewmodel_* offsets).
    if (s.enabled) {
        g_cfg.misc.viewmodel_pos[0] = static_cast<int>(s.x);
        g_cfg.misc.viewmodel_pos[1] = static_cast<int>(s.y);
        g_cfg.misc.viewmodel_pos[2] = static_cast<int>(s.z);
    }
}
}
