#pragma once
// Misc -> Movement feature bridge.
#include "features/airflow_bridge.hpp"
namespace Misc {
namespace Movement {
struct Settings {
    bool strafe_viewangles{};
    bool strafe_movement_keys{};
    bool strafe_easy{};
    bool strafe_jump_release{};
    bool strafe_jump_release_after{};
    int smoothing{};
    bool avoid_walls{};
    bool bhop{};
    bool quick_stop{};
    bool edge_jump{};
    bool edge_stop{};
    bool long_jump{};
    bool ladder_jump{};
};
inline void Publish(const Settings& s) {
    g_cfg.misc.auto_strafe = s.strafe_viewangles || s.strafe_movement_keys;
    g_cfg.misc.auto_jump = s.bhop;
    g_cfg.misc.fast_stop = s.quick_stop;
    g_cfg.misc.strafe_smooth = s.smoothing;
}
}
}
