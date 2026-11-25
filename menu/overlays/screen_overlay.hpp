#pragma once
// In-game screen-space overlay (scope removal / spread circle / out-of-FOV
// arrows / hitmarker screen FX).
//
// As with the player ESP overlay, the on-screen effects are produced by
// airflow's own features (scope removal via the post-screen detour, spread via
// its renderer, etc.) reading g_cfg - which the menu now feeds through the
// bridges. Left as a no-op here so the two renderers do not fight.
namespace ScreenOverlay {
inline void Render() {}
}
