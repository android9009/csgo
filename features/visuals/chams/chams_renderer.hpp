#pragma once
// The old project's chams material renderer lived here (PreviewMaterial over
// IMaterial). In this port the chams themselves are rendered by airflow's own
// chams feature, so nothing from the old renderer is needed; the header exists
// only so the menu bridge's include resolves.
namespace ChamsRenderer {
inline bool active() { return false; }
}
