// Portable checks for the Visuals -> Misc screen-effect decision maths that the
// game-thread half of the pipeline runs on the host:
//   misc/other: Out of FOV (edge arrows), tracers (life/width/fade), impacts, hitmarker
//   misc/local: force crosshair + penetration crosshair (arm geometry, surface table),
//               fov changer (weapon inaccuracy for the scope/spread overlay)
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#include "features/visuals/misc/local/fov.hpp"
#include "features/visuals/misc/local/force_crosshair.hpp"
#include "features/visuals/misc/local/penetration_crosshair.hpp"
#include "features/visuals/misc/other/hitmarker.hpp"
#include "features/visuals/misc/other/impacts.hpp"
#include "features/visuals/misc/other/out_of_fov.hpp"
#include "features/visuals/misc/other/tracers.hpp"

namespace {
constexpr float kEps = 1e-4f;
bool Near(float a, float b) { return std::fabs(a - b) < kEps; }
} // namespace

int main() {
    // ── Out of FOV: the walk from the screen centre to the viewport edge. ──
    {
        using OutOfFov::Detail::EdgeArrow;
        // A point at the exact centre has no direction.
        assert(!EdgeArrow(0.5f, 0.5f, 0.1f, 0u).valid);
        // A point still inside the viewport is visible: no arrow.
        assert(!EdgeArrow(0.3f, 0.3f, 0.1f, 0u).valid);
        assert(!EdgeArrow(0.5f, 0.99f, 0.1f, 0u).valid);
        // Straight right / up / left / down.
        const auto right = EdgeArrow(1.2f, 0.5f, 0.25f, 0x0A0B0C0Du);
        assert(right.valid && Near(right.x, 1.f) && Near(right.y, 0.5f) && Near(right.angle, 0.f));
        assert(Near(right.size, 0.25f) && right.color == 0x0A0B0C0Du);
        const auto up = EdgeArrow(0.5f, -0.3f, 0.25f, 0u);
        assert(up.valid && Near(up.x, 0.5f) && Near(up.y, 0.f) && Near(up.angle, -1.5707963f));
        const auto left = EdgeArrow(-0.1f, 0.5f, 0.25f, 0u);
        assert(left.valid && Near(left.x, 0.f) && Near(left.y, 0.5f));
        // 0.1 would be on screen: that is "visible", not "out of fov".
        assert(!EdgeArrow(0.1f, 0.5f, 0.25f, 0u).valid);
        const auto down = EdgeArrow(0.5f, 1.7f, 0.25f, 0u);
        assert(down.valid && Near(down.x, 0.5f) && Near(down.y, 1.f) && Near(down.angle, 1.5707963f));
        // Diagonals land on the corner.
        const auto corner = EdgeArrow(1.4f, 1.4f, 0.25f, 0u);
        assert(corner.valid && Near(corner.x, 1.f) && Near(corner.y, 1.f) && Near(corner.angle, 0.7853981f));
        // A point far outside keeps its direction to the right edge.
        const auto far = EdgeArrow(5.f, 0.51f, 0.25f, 0u);
        assert(far.valid && Near(far.x, 1.f) && std::fabs(far.y - 0.5f) < 0.02f);

        // Behind-camera direction: screen axes, up flipped, degenerate -> right.
        const auto dr1 = OutOfFov::Detail::BehindDir(1.f, 0.f);
        assert(Near(dr1.first, 1.f) && Near(dr1.second, 0.f));
        const auto dr2 = OutOfFov::Detail::BehindDir(0.f, 1.f);
        assert(Near(dr2.first, 0.f) && Near(dr2.second, -1.f));
        const auto dr3 = OutOfFov::Detail::BehindDir(3.f, 4.f);
        assert(Near(dr3.first, 0.6f) && Near(dr3.second, -0.8f));
        const auto dr4 = OutOfFov::Detail::BehindDir(0.f, 0.f);
        assert(Near(dr4.first, 1.f) && Near(dr4.second, 0.f));
    }

    // ── Tracers: life, width and the alpha fade. ──
    {
        assert(Near(Tracers::Detail::Life(0), 0.2f));
        assert(Near(Tracers::Detail::Life(1), 0.1f));
        assert(Near(Tracers::Detail::Width(0), 2.5f));
        assert(Near(Tracers::Detail::Width(1), 1.5f));
        // 0xAABBGGRR: fade only touches the top byte.
        assert(Tracers::Detail::Fade(0xFF000000u, 0.f) == 0xFF000000u);
        assert(Tracers::Detail::Fade(0xFF000000u, 1.f) == 0x00000000u);
        assert(Tracers::Detail::Fade(0x80000000u, 0.5f) == 0x40000000u);
        // Out-of-range t clamps.
        assert(Tracers::Detail::Fade(0xFFFFFFFFu, -1.f) == 0xFFFFFFFFu);
        assert(Tracers::Detail::Fade(0xFFFFFFFFu, 2.f) == 0x00FFFFFFu);
    }

    // ── Impacts: constants + the same fade. ──
    {
        assert(Near(Impacts::Detail::kLife, 0.6f));
        assert(Near(Impacts::Detail::kSize, 0.008f));
        assert(Impacts::Detail::Fade(0x800000FFu, 0.5f) == 0x400000FFu);
    }

    // ── Hitmarker: the duration window. ──
    {
        assert(Hitmarker::Detail::Within(0.5f, 1.f));
        assert(Hitmarker::Detail::Within(0.f, 1.f));
        assert(!Hitmarker::Detail::Within(1.f, 1.f));
        assert(!Hitmarker::Detail::Within(1.5f, 1.f));
        assert(Hitmarker::Detail::Within(-0.1f, 1.f)); // a future stamp still shows
        assert(!Hitmarker::Detail::Within(0.f, 0.f));  // zero duration = off
        assert(!Hitmarker::Detail::Within(0.f, -1.f));
    }

    // ── Crosshairs: the four arms around the centre. ──
    {
        const auto arms = ForceCrosshair::Detail::Arms();
        assert(arms.size() == 4);
        for (const auto& l : arms)
            assert(l.color == ForceCrosshair::Detail::kColor && Near(l.width, ForceCrosshair::Detail::kWidth));
        const auto& left = arms[0];
        assert(Near(left.y1, 0.5f) && Near(left.y2, 0.5f));
        assert(left.x1 < left.x2 && left.x2 < 0.5f);
        assert(Near(left.x2, 0.5f - ForceCrosshair::Detail::kGap));
        assert(Near(left.x1, 0.5f - ForceCrosshair::Detail::kGap - ForceCrosshair::Detail::kArm));
        // Convention: x1/y1 is the far end of the arm, x2/y2 the one at the gap.
        const auto& right = arms[1];
        assert(Near(right.x1, 0.5f + ForceCrosshair::Detail::kGap + ForceCrosshair::Detail::kArm));
        assert(Near(right.x2, 0.5f + ForceCrosshair::Detail::kGap));
        const auto& top = arms[2];
        assert(Near(top.x1, 0.5f) && Near(top.x2, 0.5f) && top.y1 < top.y2 && top.y2 < 0.5f);
        const auto& bottom = arms[3];
        assert(Near(bottom.x1, 0.5f) && Near(bottom.x2, 0.5f) && bottom.y1 > bottom.y2 && bottom.y2 > 0.5f);

        const auto pen = PenetrationCrosshair::Detail::Arms();
        for (const auto& l : pen)
            assert(l.color == PenetrationCrosshair::Detail::kColor && Near(l.width, PenetrationCrosshair::Detail::kWidth));
        // Arms on all four sides, far end away from the centre.
        assert(pen[0].x2 < 0.5f && pen[1].x2 > 0.5f && pen[2].y2 < 0.5f && pen[3].y2 > 0.5f);
        assert(Near(pen[0].x1, 0.5f - PenetrationCrosshair::Detail::kGap - PenetrationCrosshair::Detail::kArm));

        // The penetrable surface set (CS:GO surfaces.txt order).
        assert(PenetrationCrosshair::Detail::IsPenetrable(0));   // Metal
        assert(PenetrationCrosshair::Detail::IsPenetrable(1));   // Cloth
        assert(PenetrationCrosshair::Detail::IsPenetrable(3));   // Flesh
        assert(PenetrationCrosshair::Detail::IsPenetrable(5));   // Metal Vents
        assert(PenetrationCrosshair::Detail::IsPenetrable(12));  // Wood
        assert(PenetrationCrosshair::Detail::IsPenetrable(29));  // Metal Brushed
        assert(PenetrationCrosshair::Detail::IsPenetrable(30));  // Plastic
        for (const int blocking : {2, 4, 6, 7, 8, 9, 10, 11, 14, 15, 24, 31, 100, -1})
            assert(!PenetrationCrosshair::Detail::IsPenetrable(blocking));
    }

    // ── Fov changer: the weapon inaccuracy that feeds the scope/spread overlay. ──
    {
        using FovChanger::Detail::Inaccuracy;
        assert(Near(Inaccuracy(0.01f, 0.f), 0.01f));
        assert(Near(Inaccuracy(0.01f, 1.f), 0.02f));
        assert(Near(Inaccuracy(0.01f, 10.f), 0.11f));
        assert(Near(Inaccuracy(0.f, 5.f), 0.f));
        // Garbage in (a stale netvar) must not produce a spread at all.
        assert(Near(Inaccuracy(std::numeric_limits<float>::quiet_NaN(), 1.f), 0.f));
        assert(Near(Inaccuracy(0.01f, std::numeric_limits<float>::quiet_NaN()), 0.01f));
        assert(Near(Inaccuracy(-0.01f, 1.f), 0.f));
    }

    return 0;
}
