#pragma once
// World post-processing feature bridge (Visuals -> World).
// Forwards the settings that airflow's post_processing reads from g_cfg.misc
// (custom bloom scale + exposure range). The finer tone/DoF/weather controls are
// retained in the menu state (airflow applies those through its own cvar path).
#include "features/airflow_bridge.hpp"
namespace WorldPostfx {
struct Settings {
    bool exposure_enabled{};
    float exposure_amount{};
    bool bloom_enabled{};
    float bloom_strength{};
    bool local_contrast_enabled{};
    float contrast_strength{};
    float contrast_edge_strength{};
    bool color_correction_enabled{};
    int tone_curve{};
    bool grayscale{};
    bool dof_enabled{};
    int dof_mode{};
    float focus_distance{};
    float focus_speed{};
    float focus_range{};
    int weather{};
};
inline void Publish(const Settings& s) {
    if (s.bloom_enabled) {
        g_cfg.misc.custom_bloom = true;
        g_cfg.misc.bloom_scale = static_cast<int>(s.bloom_strength);
    }
    if (s.exposure_enabled) {
        const int e = static_cast<int>(s.exposure_amount);
        g_cfg.misc.exposure_min = e;
        g_cfg.misc.exposure_max = e;
    }
}
}
