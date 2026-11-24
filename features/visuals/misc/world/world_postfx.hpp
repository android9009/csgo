#pragma once
// Visuals -> World: "Exposure", "Bloom", "Local contrast", "Color correction",
// "Depth of field" and "Weather".
//
// Exposure and Bloom ride the map's `env_tonemap_controller` entity, exactly like the
// `main` branch (airflow, c_post_processing::remove_bloom): the entity's
// m_bUseCustomAutoExposureMin/Max + m_flCustomAutoExposureMin/Max fields are what the
// client's get_exposure_range() reads for the frame's exposure range, and
// m_bUseCustomBloomScale + m_flCustomBloomScale scale the engine's bloom. The field
// offsets come from the RecvTable walk with the Hazedumper snapshot as fallback.
//
// The rest of the tab is cvar-driven (the cvar names were verified against the supplied
// 2023-10-11 client dump, which references each of them):
//   Local contrast    mat_local_contrast_enable
//   Color correction  mat_colorcorrection, mat_tonemap_algorithm (tone curve)
//   Depth of field    mat_dof_enabled, mat_dof_override, mat_dof_near/far_focus_depth,
//                     mat_dof_near/far_blur_depth, mat_dof_near/far_blur_radius,
//                     mat_depthfeather_enable
//   Weather (rain)    r_raindensity, r_rainalpha, r_rainspeed, r_rainlength, r_rainwidth,
//                     r_rainparticledensity, cl_windspeed
// Every cvar is captured before the first write and handed back on disable, level change
// and unload. A cvar that is missing in a build simply leaves that part of the feature
// inactive (a LOG_WARN, not a failure).
#include <cstdint>

namespace WorldPostfx {

struct Settings {
    // Exposure: fixed exposure level (min = max), percent 0..100.
    bool exposure_enabled{};
    float exposure_amount{50.f};
    // Bloom: custom bloom scale, percent 0..100 (0 removes the engine bloom).
    bool bloom_enabled{};
    float bloom_strength{50.f};
    // Local contrast: enable + strength.
    bool local_contrast_enabled{};
    float contrast_strength{50.f};
    float contrast_edge_strength{50.f};
    // Color correction: enable + tone curve (0 linear, 1 filmic) + grayscale.
    bool color_correction_enabled{};
    int tone_curve{};
    bool grayscale{};
    // Depth of field: enable + mode (0 manual, 1 crosshair) + focus controls.
    bool dof_enabled{};
    int dof_mode{};
    float focus_distance{50.f};
    float focus_speed{9.f};
    float focus_range{50.f};
    // Weather: 0 none, 1 thunder, 2 rain, 3 snow.
    int weather{};
};

// The per-frame settings, published by the menu bridge.
void Publish(const Settings& s);
// Called once per FRAME_RENDER_START, before the original framestage runs (the same
// place the `main` branch writes its tonemap and fog overrides).
void Frame();
// Level change / left the game: hand the cvars back and drop the captures so the new
// map's values are re-captured.
void ResetState();
// Unload: hand everything back.
void Shutdown();
} // namespace WorldPostfx
