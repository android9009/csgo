#pragma once
// Shared helpers for the FATALITY menu -> airflow runtime bridges.
//
// The ported menu keeps its own evo::gui config (cfg_t). Each per-feature
// "Publish" call in the menu's bridges hands us the published settings; this
// layer is the single point that translates them into airflow's runtime
// config (g_cfg) so the *existing* airflow features - which we do not modify -
// react to the new menu. Where a menu setting has no airflow counterpart the
// value is simply not forwarded (the checkbox still exists and keeps its
// state in the menu).
//
// evo::ren::color::rgba() packs (r<<16)|(g<<8)|(b)|(a<<24); airflow stores
// colours as c_float_color (0..255 ints).
#include "globals.hpp"

namespace fatality_bridge {
inline c_float_color unpack(uint32_t rgba) {
    const int r = (rgba >> 16) & 0xFF;
    const int g = (rgba >> 8) & 0xFF;
    const int b = rgba & 0xFF;
    const int a = (rgba >> 24) & 0xFF;
    return c_float_color(r, g, b, a);
}
}
