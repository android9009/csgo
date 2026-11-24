#pragma once
// Visuals -> Misc -> LOCAL: "Fov changer".
//
// Forces the local player's view field of view to the menu value. The view is built from the
// base-player m_iFOV (the value the client uses to set the view's fov), so we write it — and
// the pre-zoom m_iFOVStart — every frame while the feature is on. Because the fov is held
// constant, the weapon's scope zoom is also nullled, which is the "Remove zoom" behaviour at
// its maximum; the exact scope/aspect refinements are layered on top of this core.
#include <cmath>
#include <cstdint>

namespace FovChanger {

struct Settings {
    bool enabled{};
    int fov{90};              // "Fov" (cfg.misc.fov), degrees
    float aspect{100.f};      // "Aspect ratio" (cfg.misc.aspect), %
    float remove_zoom{0.f};   // "Remove zoom" (cfg.misc.fov_scope), %
};

namespace Detail {
// Clamp a fov to the 1..179 range the renderer accepts (0 and >179 are rejected upstream).
inline int ClampFov(int fov) {
    if (fov < 1) return 1;
    if (fov > 179) return 179;
    return fov;
}
} // namespace Detail

void Publish(const Settings& s);
Settings Snapshot();
// FRAME_RENDER_START: write the local player's fov while enabled.
void FrameStage();
// The fov is the game's own value; we only override it per-frame, so there is nothing to restore.
void Shutdown();
} // namespace FovChanger
