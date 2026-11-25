#pragma once
// Out of FOV feature bridge (Visuals -> Misc -> Other).
// Airflow renders its off-screen indicators inside the ESP feature; the size and
// distance have clean counterparts. The toggle is retained in menu state.
#include "features/airflow_bridge.hpp"
namespace OutOfFov {
struct Settings {
    bool enabled{};
    uint32_t color{};
    float distance{};
    float size{};
};
inline void Publish(const Settings& s) {
    // Off-screen arrow geometry + colour (airflow draws these in the ESP pass).
    auto& e = g_cfg.visuals.esp[esp_enemy];
    e.offscreen_size = static_cast<int>(s.size);
    e.offscreen_dist = static_cast<int>(s.distance);
    e.colors.offscreen_arrow = fatality_bridge::unpack(s.color);
}
}
