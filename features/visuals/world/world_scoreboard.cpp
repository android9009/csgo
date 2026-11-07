#include "world_render.hpp"

#include "../dependencies/source.hpp"
#include "../esp/esp_draw.hpp"
#include "../settings.hpp"
#include "../../../menu/weapon_icons.hpp"
#include "../../../sdk/config_system/config_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifndef VK_TAB
#define VK_TAB 0x09
#endif

namespace world_visuals {

namespace {
// World > Scoreboard > Equipment slots, in the order of the menu multibox.
bool wants_slot(const player_visuals::world_esp_settings& page, int slot) {
    if (page.scoreboard_equipment_mode == 0)
        return false;
    if (page.scoreboard_equipment_mode == 2)
        return true;
    return slot >= 0 && slot < static_cast<int>(page.scoreboard_equipment.size()) &&
        page.scoreboard_equipment[slot];
}
} // namespace

// World > HUD > Scoreboard. Drawn while TAB is held so it lines up with the
// moment the player expects a scoreboard; the game's own panel stays untouched.
void render_scoreboard(ImDrawList* draw, const visuals_source::frame_data& frame,
                       const ImVec2& display) {
    if (!frame.in_game || frame.players.empty())
        return;
    if (!ImGui::IsKeyDown(ImGuiKey_Tab))
        return;
    const auto& page = player_visuals::world_settings();
    const float opacity = std::clamp(page.scoreboard_opacity, 0.f, 1.f);
    if (opacity <= 0.01f)
        return;
    const float width = display.x * std::clamp(page.scoreboard_width, 0.25f, 1.f);
    const float row_height = 24.f;
    const float header_height = 26.f;
    std::vector<const visuals_source::player_data*> rows;
    for (const auto& player : frame.players) {
        if (player.health <= 0 && player.side == 2)
            continue;
        rows.push_back(&player);
    }
    if (rows.empty())
        return;
    const float height = header_height + row_height * static_cast<float>(rows.size());
    const ImVec2 origin((display.x - width) * 0.5f, (display.y - height) * 0.5f);
    const ImVec2 max_point(origin.x + width, origin.y + height);

    draw->AddRectFilled(origin, max_point, color(page.windows_color, 0.35f * opacity), 4.f);
    draw->AddRect(origin, max_point, color(page.windows_color, opacity), 4.f);
    draw_text(draw, ImVec2(origin.x + 12.f, origin.y + header_height * 0.5f), "PLAYER",
        color(page.windows_color, opacity), 0.85f, 1);
    if (page.scoreboard_reveal_money)
        draw_text(draw, ImVec2(max_point.x - 150.f, origin.y + header_height * 0.5f), "MONEY",
            color(page.windows_color, opacity), 0.85f, 1);
    draw->AddLine(ImVec2(origin.x + 8.f, origin.y + header_height),
        ImVec2(max_point.x - 8.f, origin.y + header_height),
        color(page.windows_color, 0.5f * opacity), 1.f);

    float y = origin.y + header_height;
    for (const auto* player : rows) {
        const c_color ink = player->side == 1 ? c_color{ 0.45f, 0.78f, 1.f, opacity }
            : (player->side == 2 ? c_color{ 0.62f, 0.79f, 0.17f, opacity }
                                 : c_color{ 0.96f, 0.62f, 0.42f, opacity });
        draw_text(draw, ImVec2(origin.x + 12.f, y + row_height * 0.5f), player->name, color(ink),
            0.9f, 1);
        if (page.scoreboard_reveal_money && player->money_valid)
        {
            char money[24];
            std::snprintf(money, sizeof(money), "$%d", player->money);
            draw_text(draw, ImVec2(max_point.x - 150.f, y + row_height * 0.5f), money,
                color(0.62f, 0.85f, 0.42f, opacity), 0.85f, 1);
        }
        // Equipment icons reuse the existing weapon atlas; slots follow the menu
        // order Primary / Secondary / Knife / Taser / Grenades / Bomb / Kit / Armor / Other.
        float icon_x = max_point.x - 92.f;
        for (int slot = 0; slot < 9; ++slot) {
            if (!wants_slot(page, slot))
                continue;
            int weapon = 0;
            if (slot == 0 || slot == 1 || slot == 8)
                weapon = player->weapon_id;
            else if (slot == 7)
                weapon = player->armor > 0 ? (player->helmet ? 51 : 50) : 0;
            else if (slot == 6)
                weapon = player->defuser ? 55 : 0;
            if (weapon <= 0)
                continue;
            const int atlas = visuals_source::atlas_weapon_id(weapon);
            if (atlas < 0)
                continue;
            weapon_icons::draw(draw, atlas, ImVec2(icon_x + 9.f, y + row_height * 0.5f),
                ImVec2(20.f, 10.f), color(ink));
            icon_x += 20.f;
        }
        y += row_height;
    }
}

} // namespace world_visuals
