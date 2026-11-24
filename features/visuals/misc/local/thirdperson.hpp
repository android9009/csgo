#pragma once
// Visuals -> Misc -> LOCAL: "Thirdperson" — the chase camera and the local-model
// transparency that goes with it.
//
// The chase camera is driven through IInput's third-person fields (CInput::
// camera_in_third_person / camera_offset_*), which the client reads while it computes the
// frame's view. We set them every frame at FRAME_RENDER_START and clear them when the
// feature is off (or the player is dead without "Force when dead").
//
// Transparency: while the camera is active the local model's render mode is forced to
// kRenderTransTexture and its m_clrRender alpha is scaled by the menu's "Transparency"
// (scoped) / "+ with nades" values. The decision maths is in Detail so it can be checked
// on the host (tests/thirdperson_test.cpp); the camera and render-mode writes need the game.
#include <cmath>
#include <cstdint>

namespace ThirdPerson {

struct Settings {
    bool enabled{};
    bool force_dead{};             // "Force when dead"
    bool disable_grenade{};        // "Disable on grenade"
    float distance{100.f};         // "Distance" (camera offset)
    float scope_transparency{0.f}; // "Transparency" while scoped (0..100)
    float nade_transparency{0.f};  // "+ with nades" while holding a grenade (0..100)
};

// Source entity render modes (the values the client's render-mode enum uses).
enum RenderMode : int { RenderNormal = 0, RenderTransTexture = 5 };

namespace Detail {
// Whether the chase camera should be active this frame.
inline bool Active(const Settings& s, bool alive, bool holding_grenade) {
    if (!s.enabled) return false;
    if (!alive && !s.force_dead) return false;
    if (holding_grenade && s.disable_grenade) return false;
    return true;
}
// The local-model alpha (0..255) written into m_clrRender while the camera is active.
// 100% transparency -> 0 (invisible), 0% -> 255 (solid). The scoped slider wins over the
// nade one, matching the menu's precedence.
inline int ModelAlpha(const Settings& s, bool scoped, bool holding_grenade) {
    float pct = scoped ? s.scope_transparency : (holding_grenade ? s.nade_transparency : 0.f);
    if (!std::isfinite(pct) || pct < 0.f) pct = 0.f;
    if (pct > 100.f) pct = 100.f;
    return static_cast<int>(std::lround((100.f - pct) * 255.f / 100.f));
}
} // namespace Detail

void Publish(const Settings& s);
Settings Snapshot();
// FRAME_RENDER_START: set/clear the chase camera and the local-model alpha.
void FrameStage();
// Hands the camera and the local model's render state back to the game.
void Shutdown();
} // namespace ThirdPerson
