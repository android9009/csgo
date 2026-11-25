#pragma once
// Force crosshair feature bridge (Visuals -> Misc -> Local).
// No dedicated airflow g_cfg field; the value is retained in the menu state.
#include "features/airflow_bridge.hpp"
namespace ForceCrosshair {
struct Settings {
    bool enabled{};
};
inline void Publish(const Settings& /*s*/) {}
}
