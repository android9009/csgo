#pragma once
// In-game player ESP overlay.
//
// In the original FATALITY build this rendered the ESP (boxes / skeletons /
// labels) with the evo::ren renderer, reading the published VisualRuntime
// state. In this port the actual in-game ESP is drawn by airflow's own esp
// feature (driven from g_cfg, which the menu now writes through the bridges),
// so this overlay is intentionally a no-op to avoid double-rendering. The
// menu-local ESP *preview* (menu/preview/esp_preview.hpp) still works.
namespace PlayerEspOverlay {
inline void Render() {}
}
