#pragma once

#include <algorithm>

namespace evo::gui
{
    // Scroll only overflowing text; pause at both ends and return smoothly on leave.
    class marquee
    {
    public:
        float update(bool hovered, float overflow, float frame_time)
        {
            const float dt = std::clamp(frame_time, 0.f, .1f);
            overflow = (std::max)(0.f, overflow);
            offset = std::clamp(offset, 0.f, overflow);
            if (!hovered || overflow <= 0.f)
            {
                offset = (std::max)(0.f, offset - 100.f * dt);
                pause = .45f;
                forward = true;
            }
            else if (pause > 0.f)
                pause -= dt;
            else
            {
                offset += (forward ? 30.f : -30.f) * dt;
                if (offset >= overflow) { offset = overflow; forward = false; pause = .7f; }
                if (offset <= 0.f) { offset = 0.f; forward = true; pause = .7f; }
            }
            return offset;
        }
    private:
        float offset{}, pause{.45f};
        bool forward{true};
    };
}
