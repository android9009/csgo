#include "player_esp.hpp"
#include "../../context.hpp"
#include "../../math/math.hpp"
#include "../../utils/render.hpp"
#include "../../features/menu/config.hpp"
#include "../../sdk/interfaces/interfaces.hpp"
#include <cstring>
namespace Features::Visuals {
void PlayerEsp() {
    if (!Config::settings.player_esp || !Interfaces::Engine || !Interfaces::EntityList || !ctx.in_game) return;
    const int count = Interfaces::Engine->GetMaxClients();
    for (int i = 1; i <= count; ++i) {
        auto* player = static_cast<CCSPlayer*>(Interfaces::EntityList->GetClientEntity(i));
        if (!player || player == ctx.local || player->IsDormant() || !player->IsAlive() || !player->IsPlayer()) continue;
        const bool enemy = !ctx.local || player->m_iTeamNum() != ctx.local->m_iTeamNum();
        const Color color = enemy ? Config::settings.enemy_color : Config::settings.friendly_color;
        Vector2D bottom, top;
        const Vector origin = player->m_vecOrigin();
        if (!Math::world_to_screen(origin, bottom) || !Math::world_to_screen(origin + Vector(0.f, 0.f, 72.f), top)) continue;
        const float height = bottom.y - top.y;
        if (height < 2.f || height > 1000.f) continue;
        const int width = static_cast<int>(height * 0.42f);
        const int x = static_cast<int>(top.x - width * 0.5f);
        const int y = static_cast<int>(top.y);
        Render::OutlinedRect(x - 1, y - 1, width + 2, static_cast<int>(height) + 2, Color(0, 0, 0, 210));
        Render::OutlinedRect(x, y, width, static_cast<int>(height), color);
        if (Config::settings.player_names) {
            const int health = player->m_iHealth();
            Render::Text(x, y - 16, color, enemy ? "enemy" : "teammate");
            Render::FilledRect(x - 5, y, 3, static_cast<int>(height), Color(20, 20, 20, 200));
            Render::FilledRect(x - 5, y + static_cast<int>(height * (100 - Math::clamp(health, 0, 100)) / 100.f), 3,
                               static_cast<int>(height * Math::clamp(health, 0, 100) / 100.f), Color(80, 220, 105, 255));
        }
    }
}
}
