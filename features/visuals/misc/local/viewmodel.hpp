#pragma once
// Visuals -> Misc -> LOCAL: "Viewmodel changer".
//
// The viewmodel's position, orientation and fov are the client's own cvars
// (vm_offset_x/y/z, vm_offset_roll/pitch/yaw, v_viewmodel_fov), which the game reads
// while it lays out the viewmodel for the frame. The feature asserts them through
// the cvar layer (sdk/convars.hpp) instead of detouring the viewmodel renderer:
// values are captured before the first write, asserted only while they differ from
// what the feature wants (so a map change that resets them self-heals on the next
// frame), and handed back when the feature is off, when there is no game, and on
// unload - the same contract as WorldFog.
//
// "Disable landing bob" has no safe cvar in this build, so the checkbox stays in the
// config but this module ignores it.
#include <cmath>
#include <cstdint>

namespace ViewModelChanger {

struct Settings {
    bool enabled{};
    float fov{90.f};        // "FOV" (cfg.misc.vm_fov)
    float x{}, y{}, z{};         // "X" / "Y" / "Z"
    float roll{}, yaw{}, pitch{}; // "Roll" / "Yaw" / "Pitch", degrees
};

namespace Detail {
// A cvar write only happens while the value actually differs. 0.005 keeps float
// noise from turning into a per-frame write storm without hiding a real drift.
inline bool Changed(float current, float want) {
    if (!std::isfinite(want)) want = 0.f;
    return std::fabs(current - want) > 0.005f;
}
} // namespace Detail

void Publish(const Settings& s);
Settings Snapshot();
// FRAME_RENDER_START: assert the cvars while enabled, hand them back when off.
void FrameStage();
// Hands the cvars back. Safe to call when nothing was ever written.
void Restore();
} // namespace ViewModelChanger
