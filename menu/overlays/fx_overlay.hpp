#pragma once
// In-game FX overlay (bullet tracers / impacts / hitmarker / grenade
// trajectory).
//
// In the original build this drew the transient effects with the evo::ren
// renderer. In this port those effects come from airflow's own bullet_tracers /
// impact / hitmarker features (reading g_cfg, fed by the menu bridges), so this
// overlay is a no-op to prevent double-drawing.
namespace FxOverlay {
inline void Render() {}
}
