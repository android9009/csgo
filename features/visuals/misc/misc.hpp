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
//     delta, because that is what the menu shows. "Force viewmodel in scope" is published but
//     not applied: main writes `CCSWeaponData::hide_viewmodel_in_zoom` at +0x1BD and that
//     offset/object pair is not verified for this build (see docs/misc-runtime.md).
//   - The viewmodel position/rotation sliders ("Disable landing bob" included) stay
//     unimplemented: they need a viewmodel calc-view hook this build does not have. See
//     docs/misc-runtime.md.
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
// c_mindistance / c_maxdistance are what the chase camera clamps cam_idealdist with; the
// camera code has to be able to sit further out than the game's own 30..250 default.
inline constexpr float kChaseMinDistance = 0.f;
inline constexpr float kChaseMaxDistance = 99999.f;
// The viewmodel offset sliders span -30..30.
inline constexpr float kViewmodelOffsetLimit = 30.f;
inline constexpr float kAspectMin = 20.f;   // percent; below that the picture is unusable
inline constexpr float kAspectMax = 300.f;
// Time constant of the displayed-ratio smoothing, seconds. 40 ms is fast enough to feel
// immediate and slow enough to hide the slider's pixel steps.
inline constexpr float kAspectTau = 0.04f;
// The fov the game uses with no changer at all; `main` calls it 90 + fovs[world] with a
// zero offset slider.
inline constexpr float kDefaultWorldFov = 90.f;

// The "Distance" slider, clamped to the menu's own range. This is the *desired* distance:
// the camera code hands it to cam_idealdist and clips it against the world every frame
// without ever writing the clipped value back.
inline float DesiredDistance(const Settings& settings) {
    float distance = settings.thirdperson_dist;
    if (!std::isfinite(distance)) return 0.f;
    if (distance < kDistanceMin) distance = kDistanceMin;
    if (distance > kDistanceMax) distance = kDistanceMax;
    return distance;
}

// Alpha for the local player's own model, 0..255, or a negative value when the module does
// not own the model this frame. `"Transparency"` is a percentage of transparency: 0 keeps
// the model solid, 100 makes it invisible. `"+ with nades"` uses its own value while a
// grenade is out, which is the only thing that slider is for.
inline int LocalModelAlpha(const Settings& settings, bool active, bool holding_grenade) {
    if (!active) return -1;
    // "+ with nades" is an override that only applies while a grenade is out; at 0 the base
    // slider stays in charge, which is what a fresh config with only "Transparency" set
    // expects.
    float percent = settings.thirdperson_transparency;
    if (holding_grenade && std::isfinite(settings.thirdperson_transparency_nade) &&
        settings.thirdperson_transparency_nade > 0.f)
        percent = settings.thirdperson_transparency_nade;
    if (!std::isfinite(percent) || percent <= 0.f) return -1;
    if (percent > 100.f) percent = 100.f;
    const float alpha = 255.f * (1.f - percent * 0.01f);
    int result = static_cast<int>(std::lround(alpha));
    if (result < 0) result = 0;
    if (result > 255) result = 255;
    return result;
}

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
    const float distance = DesiredDistance(settings);
    if (!(distance > 0.f)) return plan;
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
    // 0 means "the game has not written a zoom fov for this level yet": leave it alone
    // instead of snapping the picture to the unscoped value for a frame.
    if (zoom_fov <= 0) return 0;
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

// Zoom bookkeeping for one scope session. Everything it keeps comes from the game: the fov
// the game wrote for the level in effect and the widest level seen since the scope opened.
// A value this module wrote itself is recognised (`our_write`) and never becomes the
// reference, otherwise "Remove zoom" would walk towards a full zoom removal frame by frame.
struct ZoomSession {
    bool open{};
    std::uintptr_t weapon{};
    int first_level{};
    int level{};

    void Reset() { *this = ZoomSession{}; }

    void Observe(std::uintptr_t weapon_id, int observed_fov, int our_write) {
        if (weapon_id != weapon) {
            Reset();
            weapon = weapon_id;
        }
        open = true;
        if (observed_fov <= 0) return;
        if (observed_fov == our_write) return; // ours, not the game's
        level = observed_fov;
        // The first zoom level is the widest one: a sniper's second zoom is narrower.
        if (first_level <= 0 || observed_fov > first_level) first_level = observed_fov;
    }
};

// The whole scope pipeline in the order the values have to be applied:
//   1. `level_fov` is what the game itself wrote for the zoom level in effect,
//   2. "Force second zoom" replaces a narrower second level with the first one,
//   3. "Remove zoom" mixes that level towards the world fov by the slider's percentage.
// It returns 0 when nothing should be written, and it never reads its own previous output:
// every input comes from the game.
inline int ZoomFov(const Settings& settings, int first_level_fov, int level_fov, int world_fov) {
    if (level_fov <= 0) return 0;
    const int base = ForcedFirstZoomFov(settings, first_level_fov, level_fov);
    return RemoveZoomFov(settings, base, world_fov);
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

// One viewmodel offset slider, clamped to the menu's range. The three values are written into
// the game's own viewmodel_offset_* cvars, which is where `C_BaseViewModel` reads the pose
// offset from; the world camera and the world fov are untouched by them.
inline float ViewmodelOffset(float value) {
    if (!std::isfinite(value)) return 0.f;
    if (value < -kViewmodelOffsetLimit) return -kViewmodelOffsetLimit;
    if (value > kViewmodelOffsetLimit) return kViewmodelOffsetLimit;
    return value;
}

// One frame of the aspect-ratio smoothing: `shown` walks towards `target` with a time
// constant, so the picture follows the slider instead of the slider's integer steps. `dt` is
// the frame time in seconds; a missing or nonsense dt leaves the value where it is. This is
// the only place time is involved - the returned ratio is what the renderer is given for the
// whole frame, so it does not depend on how often the engine asks.
inline float SmoothAspectRatio(float shown, float target, float dt) {
    if (!std::isfinite(target)) return shown;
    if (!std::isfinite(shown) || shown <= 0.f) return target;
    if (!std::isfinite(dt) || dt <= 0.f || dt > 0.5f) return shown;
    const float k = 1.f - std::exp(-dt / kAspectTau);
    const float next = shown + (target - shown) * k;
    // A tenth of a percent of the target is visually identical and keeps a settled slider
    // from reporting a new value every frame.
    if (std::fabs(target - next) < target * 0.001f) return target;
    return next;
}

// Screen aspect ratio after the configured override; `original` is the engine's own value.
// The slider is a percentage of a 4:3 aspect, which is how `main` reads it
// (`aspect_ratio / 100.f`); 0 means "leave it alone".
inline float TargetAspectRatio(const Settings& settings, float original) {
    if (!std::isfinite(original) || original <= 0.f) return original;
    float percent = settings.aspect;
    if (!std::isfinite(percent) || percent <= 0.f) return original;
    if (percent > kAspectMax) percent = kAspectMax;
    // The slider is the ratio in percent - main's `aspect_ratio / 100.f` - so the menu's
    // 178 renders 16:9 on a 4:3-ish setup, and 0 (the default) keeps the engine value.
    const float ratio = percent * 0.01f;
    if (percent >= kAspectMin) return ratio;
    // Below the usable range the value is blended from the engine's own ratio down to the
    // floor, so dragging off 0 is continuous: the old version snapped to 20% (a visible jump
    // and the first "nothing happens, then it kicks in" step on the slider).
    return original + (ratio - original) * (percent / kAspectMin);
}

// The settings the render thread last published.
Settings Snapshot();
} // namespace Detail
} // namespace VisualsMisc
