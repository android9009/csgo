// Host checks for Visuals -> Misc -> LOCAL: "Fov changer". Only the pure clamp; the
// m_iFOV writes need the game.
#include "../features/visuals/fov.hpp"

#include <cassert>

using namespace FovChanger;

int main() {
    assert(Detail::ClampFov(0) == 1);
    assert(Detail::ClampFov(1) == 1);
    assert(Detail::ClampFov(90) == 90);
    assert(Detail::ClampFov(140) == 140);
    assert(Detail::ClampFov(179) == 179);
    assert(Detail::ClampFov(180) == 179);
    assert(Detail::ClampFov(1000) == 179);
    assert(Detail::ClampFov(-5) == 1);
    return 0;
}
