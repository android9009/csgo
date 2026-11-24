#include "out_of_fov.hpp"
#include "../../../../context.hpp"
#include "../../../../sdk/interfaces/interfaces.hpp"
#include "../../../../sdk/entity/player.hpp"
#include "../../../../utils/math/math.hpp"
#include <algorithm>
#include <mutex>

namespace OutOfFov {
namespace {
Settings g_snapshot;
std::mutex g_mutex;
} // namespace

void Publish(const Settings& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_snapshot = s;
}

Settings Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_snapshot;
}

void Contribute(VisualRuntime::FxFrame& frame) {
    const auto s = Snapshot();
    if (!s.enabled || !ctx.in_game || !ctx.local || !Interfaces::Engine || !Interfaces::EntityList) return;
    const float range = std::isfinite(s.distance) && s.distance > 0.f ? s.distance : 0.f;
    if (!(range > 0.f)) return;
    int width = 0, height = 0;
    Interfaces::Engine->GetScreenSize(width, height);
    if (width < 2 || height < 2) return;
    // "Size" is in px; the overlay wants a fraction of the screen height.
    const float size_frac = std::isfinite(s.size) && s.size > 0.f ? s.size / height : 0.f;
    if (!(size_frac > 0.f)) return;

    QAngle view;
    Interfaces::Engine->GetViewAngles(view);
    Vector fwd{}, right{}, up{};
    Math::angle_vectors(view, &fwd, &right, &up);
    const Vector eye = ctx.local->GetEyePosition();
    const int local_team = ctx.local->m_iTeamNum();
    const int max_clients = (std::min)(64, Interfaces::Engine->GetMaxClients());

    for (int index = 1; index <= max_clients; ++index) {
        if (frame.arrow_count >= static_cast<int>(frame.arrows.size())) break;
        auto* entity = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(index));
        if (!entity || entity == ctx.local || entity->IsDormant() || !entity->IsAlive()) continue;
        if (entity->m_iTeamNum() == local_team) continue; // enemies only
        const Vector pos = entity->m_vecOrigin();
        if (!pos.IsValid()) continue;
        const float dist = (pos - eye).Length();
        if (!(dist < range)) continue;

        // On screen = no arrow. The view matrix carries the live fov, so this stays
        // correct while scoped or with the fov changer on. ctx.view_matrix is one
        // frame old (it is refreshed by the paint/scene hooks) - invisible for an
        // edge arrow.
        Vector2D screen;
        if (Math::world_to_screen(pos, screen)) {
            if (screen.x >= 0.f && screen.x <= width && screen.y >= 0.f && screen.y <= height) continue;
            const auto arrow = Detail::EdgeArrow(screen.x / width, screen.y / height, size_frac, s.color);
            if (arrow.valid)
                frame.arrows[frame.arrow_count++] = {arrow.x, arrow.y, arrow.angle, arrow.size, arrow.color};
        } else {
            // Behind the camera: the target's position in the view basis, collapsed
            // onto the screen axes.
            const Vector to = pos - eye;
            const auto dir = Detail::BehindDir(to.Dot(right), to.Dot(up));
            const auto arrow = Detail::EdgeArrow(.5f + dir.first, .5f + dir.second, size_frac, s.color);
            if (arrow.valid)
                frame.arrows[frame.arrow_count++] = {arrow.x, arrow.y, arrow.angle, arrow.size, arrow.color};
        }
    }
}
} // namespace OutOfFov
