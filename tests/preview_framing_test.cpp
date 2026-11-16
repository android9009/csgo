#include "sdk/preview_framing.hpp"
#include <cassert>
#include <cmath>
int main() {
    constexpr float aspect = 235.f / 370.f;
    for (int yaw = -180; yaw <= 180; ++yaw) {
        const float d = PreviewFraming::Distance(18.f, 22.f, 40.f, static_cast<float>(yaw), aspect);
        const float a = yaw * .01745329252f;
        const float depth = std::abs(std::cos(a))*18.f + std::abs(std::sin(a))*22.f;
        const float horizontal = std::abs(std::sin(a))*18.f + std::abs(std::cos(a))*22.f;
        const float tangent = std::tan(6.f * .01745329252f);
        assert(horizontal / (d-depth) < tangent);
        assert(40.f / (d-depth) < tangent / aspect);
        assert(d > 0.f && std::isfinite(d));
    }
    assert(PreviewFraming::Distance(18,18,45,0,aspect) > PreviewFraming::Distance(18,18,40,0,aspect));
}
