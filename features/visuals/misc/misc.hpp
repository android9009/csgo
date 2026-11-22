#pragma once
// The MISC group boxes of the Visuals tab (visuals>misc>local and the
// viewmodel/fov part of it).
//
// The group's game-side logic has been removed: third person (the engine chase
// camera through CInput), the fov changer ("Remove zoom", "Force second zoom",
// the player's fov fields), the aspect-ratio override (IEngineClient slot 101),
// the viewmodel fov (ClientMode slot 35) and the viewmodel rotation
// (CalcViewModelView detour) are all gone. The menu and the config keep the
// controls and the settings, and the menu bridge keeps publishing them here,
// but nothing in the game consumes them anymore. `Publish` is a no-op on
// purpose so the bridge and the config layer stay untouched.
//
// What stays in this header is the "Remove scope" / "Spread" geometry: those
// four lines and the circle are drawn by the menu itself
// (menu/overlays/screen_overlay.hpp), so the maths they share with nothing
// else in the game keeps living here.
#include <atomic>
#include <cmath>
#include <cstdint>

namespace VisualsMisc {
struct Settings {
    bool thirdperson{};
    float thirdperson_dist{100.f};
    bool thirdperson_dead{};    // "Force when dead"
    bool thirdperson_grenade{}; // "Disable on grenade"
    float thirdperson_transparency{};      // "Transparency": local model alpha, percent
    float thirdperson_transparency_nade{}; // "+ with nades": the same while a grenade is out
    bool fov_enabled{};
    float fov{90.f};
    float aspect{};             // "Aspect ratio", percent; 0 = leave the engine value alone
    float fov_scope{};          // "Remove zoom", percent of the zoom taken away
    bool force_second_zoom{};   // "Force second zoom"
    bool force_viewmodel_scope{}; // "Force viewmodel in scope"
    bool vm_fov_enabled{};
    float vm_fov{90.f};
    // Viewmodel changer: the three offsets the game itself reads from
    // viewmodel_offset_x / viewmodel_offset_y / viewmodel_offset_z.
    float vm_x{};
    float vm_y{};
    float vm_z{};
    // The three rotation sliders.
    float vm_roll{};
    float vm_yaw{};
    float vm_pitch{};
    // "Remove scope" selection (Detail::ScopeMode) and the "Spread" circle selection
    // (0 = off, anything else = a circle; the menu only offers "gradient").
    int remove_scope{};
    bool spread_circle{};
    // "Force crosshair": the game's own crosshair is asked for on weapons that hide it.
    bool force_crosshair{};
    // "Disable landing bob": the engine's own viewmodel-bob amplitudes are held at zero.
    bool disable_landing_bob{};
};

// Called from the menu bridge on the render thread. The feature logic is gone,
// so the values are accepted and dropped.
inline void Publish(const Settings&) {}

// Portable rules for the scope/spread overlay (menu/overlays/screen_overlay.hpp).
namespace Detail {
// The fov the game uses with no changer at all; the fallback when the live fov
// the picture is drawn with has not been written yet.
inline constexpr float kDefaultWorldFov = 90.f;

// The four "Remove scope" selections, in the order `cfg_t::noscope` declares them.
enum ScopeMode {
    ScopeNone = 0,
    ScopeStatic = 1,
    ScopeDynamic = 2,
    ScopeGradientStatic = 3,
    ScopeGradientDynamic = 4,
};

inline bool ScopeLines(int mode) { return mode >= ScopeStatic && mode <= ScopeGradientDynamic; }
inline bool ScopeGradientLines(int mode) {
    return mode == ScopeGradientStatic || mode == ScopeGradientDynamic;
}
inline bool ScopeDynamicLines(int mode) { return mode == ScopeDynamic || mode == ScopeGradientDynamic; }

// Pixels per unit of `tan(angle)` at the middle of the screen: half the screen height over
// the tangent of half the vertical field of view. A fov the game has not written yet (0)
// falls back to the world default.
inline float FocalPixels(float screen_height, float fov_degrees) {
    if (!std::isfinite(screen_height) || screen_height <= 0.f) return 0.f;
    float fov = fov_degrees;
    if (!std::isfinite(fov) || fov <= 1.f) fov = kDefaultWorldFov;
    if (fov > 179.f) fov = 179.f;
    const float tangent = std::tan(fov * 0.5f * 3.14159265358979323846f / 180.f);
    if (!(tangent > 0.f)) return 0.f;
    return (screen_height * 0.5f) / tangent;
}

// `gapPx = focalPx * tan(angle)`: the same formula the game's own crosshair uses, applied to
// the four scope lines. Static is a fixed angle (the gap does not move), Dynamic follows the
// weapon's current inaccuracy.
inline constexpr float kScopeStaticGapDegrees = 6.f;
inline constexpr float kScopeMaxGapRadians = 0.5f; // ~28.6 degrees; beyond that the lines are gone

inline float ScopeGapPixels(bool dynamic, float focal_px, float inaccuracy_radians) {
    if (!std::isfinite(focal_px) || focal_px <= 0.f) return 0.f;
    float angle = kScopeStaticGapDegrees * 3.14159265358979323846f / 180.f;
    if (dynamic) {
        angle = inaccuracy_radians;
        if (!std::isfinite(angle) || angle < 0.f) angle = 0.f;
        if (angle > kScopeMaxGapRadians) angle = kScopeMaxGapRadians;
    }
    const float gap = focal_px * std::tan(angle);
    return std::isfinite(gap) && gap > 0.f ? gap : 0.f;
}

// "Line size" is the thickness of the four lines, 5..100% of a 1..5 px range. The gap is the
// mode's business, not the slider's, so dragging the size does not move the crosshair.
inline float ScopeThickness(float line_size_percent) {
    if (!std::isfinite(line_size_percent) || line_size_percent < 5.f) line_size_percent = 5.f;
    if (line_size_percent > 100.f) line_size_percent = 100.f;
    return 1.f + (line_size_percent / 100.f) * 4.f;
}

// The radius of the "Spread" circle: the same focal-length conversion, from the weapon's
// inaccuracy to pixels. A circle below a pixel is not drawn at all.
inline float SpreadRadiusPixels(float focal_px, float inaccuracy_radians) {
    return ScopeGapPixels(true, focal_px, inaccuracy_radians);
}
} // namespace Detail
} // namespace VisualsMisc
