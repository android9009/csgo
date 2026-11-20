// Portable checks for Visuals -> Misc -> Other: the bullet-tracer / impact / hitmarker
// pipeline. Everything the game thread decides before it publishes a frame is checked here:
// the projection (and its near-plane clipping), the three shooter categories, the tracer styles
// and the record lifetimes.
#include "features/visuals/misc/fx.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace VisualsFx;

namespace {
bool Near(float a, float b, float epsilon = 1e-4f) { return std::fabs(a - b) <= epsilon; }

// A perspective matrix of the shape the engine hands out: x/y are the first and second row,
// w is the fourth row, and +z is in front of the camera.
std::array<float, 16> Perspective() {
    std::array<float, 16> m{};
    m[0] = 1.f;  // x_clip = x
    m[5] = 1.f;  // y_clip = y
    m[14] = 1.f; // w = z
    return m;
}
} // namespace

int main() {
    const float kNan = std::numeric_limits<float>::quiet_NaN();
    const auto matrix = Perspective();

    // A point straight ahead lands in the middle of the screen; the mapping is
    // 0.5 + x/(2w) on x and 0.5 - y/(2w) on y, i.e. y grows downwards.
    {
        ScreenPoint p{};
        assert(ProjectPoint(matrix, Vec3{0.f, 0.f, 10.f}, p));
        assert(Near(p.x, 0.5f) && Near(p.y, 0.5f));
        assert(ProjectPoint(matrix, Vec3{10.f, 0.f, 10.f}, p));
        assert(Near(p.x, 1.f) && Near(p.y, 0.5f));
        assert(ProjectPoint(matrix, Vec3{0.f, -10.f, 10.f}, p));
        assert(Near(p.x, 0.5f) && Near(p.y, 1.f));
        // Behind the camera (w <= 0) is not projectable at all.
        assert(!ProjectPoint(matrix, Vec3{0.f, 0.f, -1.f}, p));
        assert(!ProjectPoint(matrix, Vec3{0.f, 0.f, 0.f}, p));
        assert(!ProjectPoint(matrix, Vec3{kNan, 0.f, 10.f}, p));
    }

    // A segment with both ends in front of the camera is projected end to end.
    {
        ScreenSegment s{};
        assert(ProjectSegment(matrix, Vec3{0.f, 0.f, 10.f}, Vec3{10.f, 0.f, 10.f}, s));
        assert(Near(s.x1, 0.5f) && Near(s.x2, 1.f));
        assert(Near(s.y1, 0.5f) && Near(s.y2, 0.5f));
    }

    // Both ends behind the camera: nothing to draw.
    {
        ScreenSegment s{};
        assert(!ProjectSegment(matrix, Vec3{0.f, 0.f, -5.f}, Vec3{0.f, 0.f, -1.f}, s));
    }

    // The shooter's eye is behind the camera when a tracer starts behind the local player's
    // view; the visible part is clipped at the camera plane and stays on screen instead of
    // being dropped (the old alternative: the whole tracer disappears).
    {
        ScreenSegment s{};
        assert(ProjectSegment(matrix, Vec3{0.f, 0.f, -5.f}, Vec3{0.f, 0.f, 10.f}, s));
        assert(std::isfinite(s.x1) && std::isfinite(s.y1));
        assert(std::isfinite(s.x2) && std::isfinite(s.y2));
        // The far end is untouched, the near end sits on the camera plane.
        assert(Near(s.x2, 0.5f) && Near(s.y2, 0.5f));
    }

    // Categories: "Local" wins over the team, otherwise team-vs-enemy.
    {
        assert(Category(true, true) == Local);
        assert(Category(false, true) == Team);
        assert(Category(false, false) == Enemy);
    }

    // Styles: "Laser" is the thicker of the two, "Line" the thin one, and an unknown style
    // falls back to the thick one instead of drawing nothing.
    {
        assert(Near(TracerWidth(0), 3.f));
        assert(Near(TracerWidth(1), 1.5f));
        assert(Near(TracerWidth(7), 3.f));
    }

    // Lifetimes: alive inside the window, dead once it has passed, and dead when the clock
    // jumped backwards (map change / demo seek) or the record has no usable timestamp.
    {
        assert(Alive(0.f, 1.f, 2.f));
        assert(Alive(0.f, 2.f, 2.f));
        assert(!Alive(0.f, 2.5f, 2.f));
        assert(!Alive(5.f, 1.f, 2.f));
        assert(!Alive(0.f, 1.f, 0.f));
        assert(!Alive(kNan, 1.f, 2.f));
        assert(!Alive(0.f, kNan, 2.f));
    }

    // Pixel sizes become a fraction of the screen height; a missing height has no scale.
    {
        assert(Near(NormalizedSize(4.f, 1080.f), 4.f / 1080.f));
        assert(Near(NormalizedSize(4.f, 0.f), 0.f));
        assert(Near(NormalizedSize(kNan, 1080.f), 0.f));
    }

    std::puts("visuals_fx_test: ok");
    return 0;
}
