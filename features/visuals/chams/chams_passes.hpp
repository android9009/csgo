#pragma once
// Chams material selection (Visuals tab).
// The combo box stores the selected material as a single bit whose index is the
// material id. Both the old "airflow" material list and this project's
// g_cfg.visuals.chams[].material use the same numbering, so the selection is the
// bit index itself (0 = none).
#include <cstdint>
namespace ChamsPasses {
inline int Selection(int bit) {
    if (bit < 0) return 0;
    return bit;
}
}
