#pragma once
// The MISC group boxes of the Visuals tab (visuals>misc>local and the viewmodel/fov part of
// it). One folder per menu group, one header for the folder, one translation unit per
// feature: fov.cpp (fov changer + viewmodel fov) and thirdperson.cpp.
//
// Provenance
// ----------
//   - Third person follows `main`'s thirdperson.cpp (c_thirdperson::run_alive plus
//     fix_camera_on_fakeduck): the camera moves backwards along the view direction, an
//     engine trace with a +-16 unit box clips the distance against world geometry, the
//     local model is not drawn while the camera is outside it, and "Force when dead" flips
//     the observer mode from in-eye to chase (main: observer_mode 4 -> 5).
//     `main` writes the game's own CInput::camera_in_third_person fields. Those are struct
//     offsets inside CInput with no signature of their own, and a wrong guess there writes
//     into unrelated input state, so this build instead places the camera in
//     ClientMode::OverrideView (slot 18, main's OVERRIDE_VIEW_IDX) - the same hook, the
//     same maths, and the same field of view tail `main`'s own fakeluck fixup writes.
//   - The fov changer writes the player's own fov fields (m_iDefaultFOV / m_iFOV /
//     m_iFOVStart), which is what `main`'s c_fov_and_view::change_fov does. The viewmodel
//     fov is added in ClientMode::GetViewModelFOV (slot 35), which is `main`'s
//     get_viewmodel_fov detour; here the slider is the absolute viewmodel fov instead of a
//     delta, because that is what the menu shows.
//   - "Remove zoom", "Aspect ratio", "Force second zoom", "Force viewmodel in scope",
//     "Disable landing bob" and the whole viewmodel position/rotation set stay
//     unimplemented: they need the weapon-info zoom table, the viewmodel's own calc-view
//     hook or CInput state this build does not have. See docs/misc-runtime.md.
#include <atomic>
#include <cmath>

namespace VisualsMisc {
struct Settings {
    bool thirdperson{};
    float thirdperson_dist{100.f};
    bool thirdperson_dead{};    // "Force when dead"
    bool thirdperson_grenade{}; // "Disable on grenade"
    bool fov_enabled{};
    float fov{90.f};
    bool vm_fov_enabled{};
    float vm_fov{90.f};
};

void Publish(const Settings& settings);

// ClientMode::OverrideView detour (slot 18). Moves the camera for third person.
void OverrideView(void* view_setup);
// ClientMode::GetViewModelFOV detour (slot 35).
float ViewModelFov(float original);
// FrameStageNotify(FRAME_RENDER_START): applies the player's fov fields.
void FrameStage(int stage);
// True while the local model must not be drawn (the camera is outside the player).
bool HideLocalModel();
void Shutdown();

// Portable rules. Everything the detours decide is checked here on the host, so the
// camera and fov policy can be tested without the game.
namespace Detail {
// The menu's sliders span 60..140 for both fov controls.
inline constexpr float kFovMin = 60.f;
inline constexpr float kFovMax = 140.f;
inline constexpr float kDistanceMin = 10.f;
inline constexpr float kDistanceMax = 250.f;

struct ThirdPerson {
    bool active{};
    float distance{};
    bool dead_observer{}; // flip OBS_MODE_IN_EYE to OBS_MODE_CHASE
};

// `holding_grenade` is the "Disable on grenade" input; `alive` the local player's state.
inline ThirdPerson Plan(const Settings& settings, bool in_game, bool alive, bool holding_grenade) {
    ThirdPerson plan;
    if (!settings.thirdperson || !in_game) return plan;
    if (!alive) {
        // The camera follows whoever we are spectating, so there is no origin to move; only
        // the observer mode can be flipped, and only when the menu asks for it.
        plan.dead_observer = settings.thirdperson_dead;
        return plan;
    }
    if (holding_grenade) return plan; // "Disable on grenade"
    float distance = settings.thirdperson_dist;
    if (!std::isfinite(distance)) return plan;
    if (distance < kDistanceMin) distance = kDistanceMin;
    if (distance > kDistanceMax) distance = kDistanceMax;
    plan.active = true;
    plan.distance = distance;
    return plan;
}

// Distance the camera can travel back before hitting the world (main multiplies its
// configured distance by the trace fraction).
inline float ClampedDistance(float requested, float fraction) {
    if (!std::isfinite(requested) || requested <= 0.f) return 0.f;
    if (!std::isfinite(fraction)) return 0.f;
    if (fraction < 0.f) fraction = 0.f;
    if (fraction > 1.f) fraction = 1.f;
    return requested * fraction;
}

// Absolute fov the game should render with, or 0 when the changer is off.
inline int TargetFov(const Settings& settings, bool alive, bool scoped) {
    if (!settings.fov_enabled || !alive) return 0;
    // While scoped the weapon owns the field of view: the zoom animation writes the same
    // two fields every frame, so an override here would be overwritten or break the zoom.
    // "Remove zoom" is a separate, unimplemented item (see docs/misc-runtime.md).
    if (scoped) return 0;
    float fov = settings.fov;
    if (!std::isfinite(fov)) return 0;
    if (fov < kFovMin) fov = kFovMin;
    if (fov > kFovMax) fov = kFovMax;
    return static_cast<int>(std::lround(fov));
}

// Viewmodel fov after the configured override; `original` is the engine's own value.
inline float TargetViewModelFov(const Settings& settings, float original) {
    if (!settings.vm_fov_enabled) return original;
    float fov = settings.vm_fov;
    if (!std::isfinite(fov)) return original;
    if (fov < kFovMin) fov = kFovMin;
    if (fov > kFovMax) fov = kFovMax;
    return fov;
}

// The settings the render thread last published, and the flag the DrawModelExecute hook
// reads. Both are lock-free: the game thread only ever loads them.
Settings Snapshot();
void SetHideLocalModel(bool hide);
} // namespace Detail
} // namespace VisualsMisc
