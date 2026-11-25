#pragma once
// Bullet tracer feature bridge (Visuals -> Misc -> Other).
// airflow: misc.tracers bitmask { enemy=1, team=2, local=4 }, tracer_type, trace_clr[3].
#include "features/airflow_bridge.hpp"
#include <array>
namespace Tracers {
namespace Target {
inline constexpr int Local = 0;
inline constexpr int Enemy = 1;
inline constexpr int Team = 2;
inline constexpr int Count = 3;
}
struct Settings {
    std::array<bool, Target::Count> enabled{};
    std::array<uint32_t, Target::Count> color{};
    std::array<int, Target::Count> style{};
};
inline void Publish(const Settings& s) {
    unsigned mask = 0;
    if (s.enabled[Target::Enemy]) mask |= 1;
    if (s.enabled[Target::Team]) mask |= 2;
    if (s.enabled[Target::Local]) mask |= 4;
    g_cfg.misc.tracers = mask;
    for (int i = 0; i < 3; ++i) g_cfg.misc.trace_clr[i] = fatality_bridge::unpack(s.color[i]);
    // Laser vs line style of the first enabled target.
    for (int i = 0; i < Target::Count; ++i)
        if (s.enabled[i]) { g_cfg.misc.tracer_type = s.style[i]; break; }
}
}
