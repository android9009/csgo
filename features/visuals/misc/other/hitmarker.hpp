#pragma once
// Hitmarker feature bridge (Visuals -> Misc -> Other).
// airflow: misc.hitmarker bitmask { world=1, screen=2, ... } + misc.hitmarker_clr.
#include "features/airflow_bridge.hpp"
namespace Hitmarker {
struct Settings {
    bool world{};
    bool screen{};
    bool kibit{};
    uint32_t world_color{};
    uint32_t screen_color{};
    uint32_t kibit_color{};
    float world_duration{};
    float screen_duration{};
    float kibit_duration{};
};
inline void Publish(const Settings& s) {
    unsigned mask = 0;
    if (s.world) mask |= 1;
    if (s.screen) mask |= 2;
    if (s.kibit) mask |= 4;
    g_cfg.misc.hitmarker = mask;
    // Single shared colour in airflow: the screen hitmarker colour.
    g_cfg.misc.hitmarker_clr = fatality_bridge::unpack(s.screen_color);
}
}
