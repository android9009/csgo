#pragma once
// The MISC group boxes of the Visuals tab (visuals>misc>local and the viewmodel/fov part of
// it). One folder per menu group, one header for the folder, one translation unit per
// feature: thirdperson.cpp, fov.cpp (fov changer, remove zoom, aspect ratio, viewmodel fov)
// and publish.cpp (the menu -> game thread bridge).
//
// Provenance
// ----------
//   - Third person is `main`'s thirdperson.cpp (c_thirdperson::run_alive): the camera is the
//     ENGINE's chase camera, i.e. `CInput::camera_in_third_person` plus
//     `CInput::camera_offset = { pitch, yaw, distance }`, with the distance clipped by a
//     `MASK_NPCWORLDSTATIC` trace through a +-16 unit box, and "Force when dead" flips the
//     observer mode from in-eye to chase (main: observer_mode 4 -> 5).
//     The offsets of that pair were read out of the supplied client.dll, not guessed: the
//     CInput vtable slots that set and clear the flag, and the getter/setter pair that
//     copies the vec3, are listed in docs/interface-verification.md. Moving the camera in
//     `ClientMode::OverrideView` instead (what this build used to do) puts the renderer
//     behind the player but leaves the engine's own third-person path out of the loop, and
//     that path is what computes the aspect-correct view: the picture comes out stretched
//     and the model is lit as if it were still first person. The engine camera has neither
//     problem.
//   - The fov changer writes the player's own fov fields (m_iDefaultFOV / m_iFOV /
//     m_iFOVStart), which is what `main`'s c_fov_and_view::change_fov does; "Remove zoom"
//     is main's `fovs[zoom]` term expressed as "how much of the zoom to take away", and
//     "Force second zoom" keeps the first zoom level's field of view when the weapon zooms
//     further.
//   - "Aspect ratio" is main's `get_screen_aspect_ratio` detour: IEngineClient slot 101,
//     which in this build is a two-argument function that returns a float.
//   - The viewmodel fov is added in ClientMode::GetViewModelFOV (slot 35), which is `main`'s
//     get_viewmodel_fov detour; here the slider is the absolute viewmodel fov instead of a
//     delta, because that is what the menu shows. "Force viewmodel in scope" writes
//     `CCSWeaponData::hide_viewmodel_in_zoom`, the field main writes at +0x1BD.
//   - The viewmodel position/rotation sliders ("Disable landing bob" included) stay
//     unimplemented: they need a viewmodel calc-view hook this build does not have. See
//     docs/misc-runtime.md.
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
    float aspect{};             // "Aspect ratio", percent; 0 = leave the engine value alone
    float fov_scope{};          // "Remove zoom", percent of the zoom taken away
    bool force_second_zoom{};   // "Force second zoom"
    bool force_viewmodel_scope{}; // "Force viewmodel in scope"
    bool vm_fov_enabled{};
    float vm_fov{90.f};
};

void Publish(const Settings& settings);

// ClientMode::GetViewModelFOV detour (slot 35).
float ViewModelFov(float original);
// IEngineClient::GetScreenAspectRatio detour (slot 101).
float ScreenAspectRatio(float original);
// FrameStageNotify(FRAME_RENDER_START): the player's fov fields, "Remove zoom" and the
// viewmodel-in-scope field.
void FrameStage(int stage);
// FrameStageNotify(FRAME_RENDER_START): hand the engine its third person camera through
// CInput, which is what makes the view aspect-correct (the engine builds it).
void ApplyThirdPerson();
void Shutdown();

// Cleanup used by Shutdown(): hand the engine's camera back and the player's fov fields back.
void ResetThirdPerson();
void ResetFov();

// Portable rules. Everything the detours decide is checked here on the host, so the camera,
// fov and aspect policy can be tested without the game.
namespace Detail {
// The menu's sliders span 60..140 for both fov controls, and 0..200 for the aspect ratio.
inline constexpr float kFovMin = 60.f;
inline constexpr float kFovMax = 140.f;
inline constexpr float kDistanceMin = 10.f;
inline constexpr float kDistanceMax = 250.f;
inline constexpr float kAspectMin = 20.f;   // percent; below that the picture is unusable
inline constexpr float kAspectMax = 300.f;
// The fov the game uses with no changer at all; `main` calls it 90 + fovs[world] with a
// zero offset slider.
inline constexpr float kDefaultWorldFov = 90.f;

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

// Absolute fov the game should render with, or 0 when the changer is off. While scoped the
// weapon owns the field of view, so this reports the unscoped value only.
inline int TargetFov(const Settings& settings, bool alive, bool scoped) {
    if (!settings.fov_enabled || !alive) return 0;
    if (scoped) return 0;
    float fov = settings.fov;
    if (!std::isfinite(fov)) return 0;
    if (fov < kFovMin) fov = kFovMin;
    if (fov > kFovMax) fov = kFovMax;
    return static_cast<int>(std::lround(fov));
}

// The world fov the zoom maths starts from: the changer's value when it is on, otherwise
// the engine's own 90.
inline int WorldFov(const Settings& settings) {
    const int target = TargetFov(settings, true, false);
    if (target > 0) return target;
    return static_cast<int>(kDefaultWorldFov);
}

// "Remove zoom": how much of the weapon's zoom to take away, in percent. `zoom_fov` is the
// field of view the game itself wrote for the current zoom level, `world_fov` the unscoped
// value. 0% keeps the zoom exactly as the game has it, 100% drops the zoom entirely.
inline int RemoveZoomFov(const Settings& settings, int zoom_fov, int world_fov) {
    if (zoom_fov <= 0) return world_fov; // nothing to interpolate from
    float percent = settings.fov_scope;
    if (!std::isfinite(percent) || percent <= 0.f) return zoom_fov;
    if (percent > 100.f) percent = 100.f;
    const float zoom = static_cast<float>(zoom_fov);
    const float world = static_cast<float>(world_fov);
    const float mixed = zoom + (world - zoom) * (percent * 0.01f);
    const int result = static_cast<int>(std::lround(mixed));
    return result < 1 ? 1 : result;
}

// "Force second zoom": the game zooms further when the scope button is pressed again. The
// first level observed in this scope session is the reference; anything narrower than it is
// the second level, and the reference is used instead. `first_zoom_fov` is 0 until a scope
// session has been seen, in which case the observed value is the reference by definition.
inline int ForcedFirstZoomFov(const Settings& settings, int first_zoom_fov, int observed_fov) {
    if (!settings.force_second_zoom) return observed_fov;
    if (first_zoom_fov <= 0 || observed_fov <= 0) return observed_fov;
    // A whole fov unit of difference: the game rewrites the field as an integer.
    if (observed_fov < first_zoom_fov - 1) return first_zoom_fov;
    return observed_fov;
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

// Screen aspect ratio after the configured override; `original` is the engine's own value.
// The slider is a percentage of a 4:3 aspect, which is how `main` reads it
// (`aspect_ratio / 100.f`); 0 means "leave it alone".
inline float TargetAspectRatio(const Settings& settings, float original) {
    if (!std::isfinite(original) || original <= 0.f) return original;
    float percent = settings.aspect;
    if (!std::isfinite(percent) || percent <= 0.f) return original;
    if (percent < kAspectMin) percent = kAspectMin;
    if (percent > kAspectMax) percent = kAspectMax;
    // The slider is the ratio in percent - main's `aspect_ratio / 100.f` - so the menu's
    // 178 renders 16:9 on a 4:3-ish setup, and 0 (the default) keeps the engine value.
    return percent * 0.01f;
}

// The settings the render thread last published.
Settings Snapshot();
} // namespace Detail
} // namespace VisualsMisc
