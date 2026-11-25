#pragma once
// ThirdPerson feature bridge (Visuals -> Misc -> Local).
// Translates the published settings into airflow's g_cfg misc fields.
#include "features/airflow_bridge.hpp"
namespace ThirdPerson {
struct Settings {
    bool enabled{};
    bool force_dead{};
    bool disable_grenade{};
    float distance{};
    float scope_transparency{};
    float nade_transparency{};
};
inline void Publish(const Settings& s) {
    // Airflow drives third-person through its misc block + the tp keybind.
    // Forward the distance and the dead-cam flag (clean 1:1 fields).
    g_cfg.misc.thirdperson_dist = static_cast<int>(s.distance);
    g_cfg.misc.thirdperson_dead = s.force_dead;
}
}
