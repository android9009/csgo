#pragma once
// Visuals -> Misc -> LOCAL: "Penetration crosshair".
//
// Answers the one question before a wallbang: is the surface under the crosshair
// penetrable? Every frame the module traces the crosshair ray; when it stops on a
// world surface whose material allows penetration it draws a green cross, otherwise
// it draws nothing (a red cross would be noise the menu does not offer a colour for).
//
// The decision is Detail::IsPenetrable, keyed by the trace's surface index
// (csurface_t::surfaceProps, the CS:GO surfaces.txt order): the standard penetrable
// set is metal, cloth, flesh, metal vents, wood, metal brushed and plastic. Stone,
// concrete, glass, water, plant, sand and the rest block.
#include <array>
#include <cmath>
#include <cstdint>

#include "../../../../sdk/visual_runtime_state.hpp"

namespace PenetrationCrosshair {

struct Settings {
    bool enabled{};
};

namespace Detail {
inline bool IsPenetrable(int surface_props) {
    switch (surface_props) {
        case 0:   // Metal
        case 1:   // Cloth
        case 3:   // Flesh
        case 5:   // Metal Vents
        case 12:  // Wood
        case 29:  // Metal Brushed
        case 30:  // Plastic
            return true;
        default:
            return false;
    }
}
inline constexpr float kGap = 0.008f;
inline constexpr float kArm = 0.03f;   // a bit longer than the force crosshair: it is a signal
inline constexpr float kWidth = 1.5f;
inline constexpr std::uint32_t kColor = 0xFF00FF00u; // green = "the wall gives"

// The four arms, same geometry as the force crosshair.
inline std::array<VisualRuntime::ScreenLine, 4> Arms(float gap = kGap, float arm = kArm,
                                                     float width = kWidth,
                                                     std::uint32_t color = kColor) {
    const float inner = gap;
    const float outer = gap + arm;
    return {{
        {.5f - outer, .5f, .5f - inner, .5f, width, color},
        {.5f + outer, .5f, .5f + inner, .5f, width, color},
        {.5f, .5f - outer, .5f, .5f - inner, width, color},
        {.5f, .5f + outer, .5f, .5f + inner, width, color},
    }};
}
} // namespace Detail

void Publish(const Settings& s);
Settings Snapshot();
// Per frame, from the game thread: contribute the cross when the surface is penetrable.
void Contribute(VisualRuntime::FxFrame& frame);
} // namespace PenetrationCrosshair
