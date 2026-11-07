#include "world_render.hpp"

#include "../dependencies/source.hpp"
#include "../esp/esp_draw.hpp"
#include "../settings.hpp"
#include "../../../sdk/config_system/config_system.hpp"
#include "../../../menu/binds.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace world_visuals {

namespace {
std::string local_user_name() {
    char buffer[64]{};
    DWORD size = static_cast<DWORD>(sizeof(buffer));
    if (!GetUserNameA(buffer, &size))
        return "unknown";
    return buffer;
}
} // namespace

// World > Windows > Watermark. Items, position, density and the shared window
// style are the real settings; Ping and the avatar need a netchannel / Steam
// avatar texture, so they degrade gracefully instead of drawing garbage.
void render_windows(ImDrawList* draw, const visuals_source::frame_data& frame,
                    const ImVec2& display) {
    const auto& page = player_visuals::world_settings();
    if (!page.window_watermark)
        return;

    char time_text[32]{};
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::snprintf(time_text, sizeof(time_text), "%02d:%02d:%02d",
        local.tm_hour, local.tm_min, local.tm_sec);
    char fps_text[32]{};
    std::snprintf(fps_text, sizeof(fps_text), "%.0f fps", ImGui::GetIO().Framerate);

    struct part { std::string text; bool accent; };
    std::string name;
    std::string ping;
    static const std::string user_name = local_user_name();
    const bool density_compact = page.watermark_density == 1;
    const bool density_expanded = page.watermark_density == 2;
    const char* labels[4] = { "TIME", "NAME", "PING", "FPS" };
    std::string values[4] = { time_text, user_name, " - ", fps_text };
    const float scale = density_expanded ? 1.05f : (density_compact ? 0.8f : 0.9f);
    const float line_height = 15.f * scale;
    const float pad = (density_compact ? 6.f : 9.f);
    const float gap = density_compact ? 8.f : 12.f;

    float total_width = 0.f;
    for (int i = 0; i < 4; ++i) {
        if (!page.watermark_items[i])
            continue;
        char piece[96];
        std::snprintf(piece, sizeof(piece), "%s%s", density_compact ? "" : labels[i],
            density_compact ? values[i].c_str() : (" " + values[i]).c_str());
        total_width += ImGui::GetFont()->CalcTextSizeA(12.f * scale, FLT_MAX, 0.f, piece).x + gap;
    }
    if (total_width <= 0.f)
        return;
    total_width += pad * 2.f;
    const float height = line_height + pad * 1.2f;

    ImVec2 origin{};
    switch (std::clamp(page.watermark_position, 0, 3)) {
    case 0: origin = ImVec2(14.f, 14.f); break;                       // Top-Left
    case 1: origin = ImVec2(display.x - total_width - 14.f, 14.f); break;   // Top-Right
    case 2: origin = ImVec2(14.f, display.y - height - 14.f); break;  // Bottom-Left
    default: origin = ImVec2(display.x - total_width - 14.f, display.y - height - 14.f); break;
    }

    const float background = std::clamp(page.windows_background_opacity, 0.f, 1.f);
    const float line = std::clamp(page.windows_line_opacity, 0.f, 1.f);
    draw->AddRectFilled(origin, origin + ImVec2(total_width, height),
        color(page.windows_color, background * 0.45f), 3.f);
    if (line > 0.f)
        draw->AddRect(origin, origin + ImVec2(total_width, height),
            color(page.windows_color, line), 3.f);

    float x = origin.x + pad;
    const float text_y = origin.y + height * 0.5f;
    for (int i = 0; i < 4; ++i) {
        if (!page.watermark_items[i])
            continue;
        const std::string piece = density_compact ? values[i] : labels[i] + std::string(" ") + values[i];
        const ImVec2 measured = ImGui::GetFont()->CalcTextSizeA(12.f * scale, FLT_MAX, 0.f, piece.c_str());
        const c_color ink = (page.watermark_additional_colors && (i == 0 || i == 3))
            ? c_color{ 0.62f, 0.79f, 0.17f, 1.f } : c_color{ 0.92f, 0.94f, 1.f, 1.f };
        draw->AddText(ImGui::GetFont(), 12.f * scale, ImVec2(x, text_y - measured.y * 0.5f),
            color(ink), piece.c_str());
        x += measured.x + gap;
    }
    // World > Windows > indicator badges. Each toggle shows the live state of the
    // matching gameplay feature next to the watermark instead of a static label.
    struct badge { bool wanted; const char* text; bool active; };
    char hitchance_text[32];
    std::snprintf(hitchance_text, sizeof(hitchance_text), "HC %d",
        std::clamp(g_cfg->misc.m_triggerbot_hitchance, 0, 100));
    const badge badges[4] = {
        { page.windows_double_tap, "DT", g_cfg->misc.m_double_tap },
        { page.windows_jump_bug, "JB", g_cfg->misc.m_jump_bug },
        { page.windows_slowdown, "SW", g_cfg->misc.m_slow_walk },
        { page.windows_hit_chance_override, hitchance_text, true },
    };
    float badge_x = origin.x;
    const float badge_y = origin.y + height + 6.f;
    for (const auto& entry : badges) {
        if (!entry.wanted)
            continue;
        const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(11.f, FLT_MAX, 0.f, entry.text);
        const ImVec2 min_point(badge_x, badge_y);
        const ImVec2 max_point(badge_x + size.x + 10.f, badge_y + 16.f);
        draw->AddRectFilled(min_point, max_point,
            color(page.windows_color, entry.active ? 0.45f : 0.18f), 3.f);
        draw_text(draw, ImVec2(min_point.x + 5.f, badge_y + 8.f), entry.text,
            color(entry.active ? c_color{ 0.62f, 0.79f, 0.17f, 1.f }
                               : c_color{ 0.7f, 0.72f, 0.78f, 1.f }), 0.78f, 1);
        badge_x = max_point.x + 5.f;
    }

    if (page.watermark_display_avatar) {
        // Steam avatar texture is fetched asynchronously by runtime/steam_avatar;
        // until it lands the slot stays a framed placeholder instead of popping in.
        const ImVec2 center(origin.x - 14.f, origin.y + height * 0.5f);
        draw->AddCircleFilled(center, 11.f, color(page.windows_color, 0.35f), 24);
        draw->AddCircle(center, 11.f, color(page.windows_color, 0.8f), 24, 1.f);
    }
}


// World > Windows > Hotkeys. Style / right-aligned / dynamic header are the real
// settings; the list itself comes from menu_binds::overlay_entries() (same
// entries the bind cards mark with the eye icon).
void render_hotkeys(ImDrawList* draw, const ImVec2& display) {
    const auto& page = player_visuals::world_settings();
    if (!page.window_hotkeys)
        return;
    const auto binds = menu_binds::overlay_entries();
    if (binds.empty())
        return;
    const bool minimal = page.hotkeys_style == 1;
    const bool detailed = page.hotkeys_style == 2;
    const float scale = minimal ? 0.78f : (detailed ? 1.0f : 0.88f);
    const float line = 16.f * scale;
    float y = page.window_watermark ? (14.f + 30.f) : 16.f;
    if (page.hotkeys_dynamic_header) {
        const std::string header = "hotkeys (" + std::to_string(binds.size()) + ")";
        const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(12.f, FLT_MAX, 0.f, header.c_str());
        const float x = page.hotkeys_right_aligned ? (display.x - 14.f - size.x) : 14.f;
        draw_text(draw, ImVec2(x, y), header, color(page.windows_color), 0.85f, page.hotkeys_right_aligned ? 1 : 1);
        y += line;
    }
    for (const auto& bind : binds) {
        std::string text;
        if (minimal)
            text = bind.label;
        else if (detailed)
            text = bind.label + " [" + bind.key + "]";
        else
            text = bind.label + "  " + bind.key;
        const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(12.f * scale, FLT_MAX, 0.f, text.c_str());
        const float x = page.hotkeys_right_aligned ? (display.x - 14.f - size.x) : 14.f;
        draw_text(draw, ImVec2(x, y), text, color(page.windows_color), scale, 1);
        y += line;
    }
}


// World > Windows > Spectators. Names come from the captured player list; the
// indices come from the entity walk (m_hObserverTarget pointing at us).
void render_spectators(ImDrawList* draw, const visuals_source::frame_data& frame,
                       const ImVec2& display) {
    const auto& page = player_visuals::world_settings();
    if (!page.window_spectators || !frame.in_game)
        return;
    const auto indices = visuals_source::local_spectators();
    if (indices.empty())
        return;
    const float line = 16.f;
    float y = display.y * 0.5f - line * static_cast<float>(indices.size()) * 0.5f;
    draw_text(draw, ImVec2(display.x - 14.f, y - line), "SPECTATORS",
        color(page.windows_color), 0.8f, -1);
    for (const int index : indices) {
        std::string label = "player " + std::to_string(index);
        for (const auto& player : frame.players) {
            if (player.index == index) {
                label = player.name;
                break;
            }
        }
        draw_text(draw, ImVec2(display.x - 14.f, y), label,
            color(1.f, 0.42f, 0.36f, 1.f), 0.85f, -1);
        y += line;
    }
}


namespace {
struct death_notice_entry {
    std::string text;
    bool assistant = false;
    bool by_local = false;
    std::chrono::steady_clock::time_point created{};
};
std::mutex death_notices_mutex;
std::vector<death_notice_entry> death_notices;
} // namespace

void push_death_notice(std::string text, const bool assistant, const bool by_local) {
    if (text.empty())
        return;
    std::scoped_lock lock(death_notices_mutex);
    death_notices.push_back({ std::move(text), assistant, by_local,
        std::chrono::steady_clock::now() });
    while (death_notices.size() > 24)
        death_notices.erase(death_notices.begin());
}

// World > Death notices: Off / All / Kills Only switch, teammate assists filter,
// and "preserve" keeps the list alive for the whole map instead of fading.
void render_death_notices(ImDrawList* draw, const ImVec2& display) {
    const auto& page = player_visuals::world_settings();
    const int mode = std::clamp(page.preserve_death_notices_mode, 0, 2);
    if (mode == 0 || !page.preserve_death_notices)
        return;
    std::vector<death_notice_entry> entries;
    {
        std::scoped_lock lock(death_notices_mutex);
        entries = death_notices;
    }
    if (entries.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    float y = display.y - 40.f;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (mode == 2 && !it->by_local)
            continue;
        if (it->assistant && page.hide_teammate_assists)
            continue;
        const float age = std::chrono::duration<float>(now - it->created).count();
        float alpha = 1.f;
        if (!page.preserve_death_notices && age > 7.f)
            alpha = std::max(0.f, 1.f - (age - 7.f) / 1.5f);
        if (alpha <= 0.01f)
            continue;
        const c_color ink = it->by_local ? c_color{ 0.62f, 0.79f, 0.17f, alpha }
                                         : c_color{ 0.92f, 0.94f, 1.f, alpha };
        draw_text(draw, ImVec2(14.f, y), it->text, color(ink), 0.85f, 1);
        y -= 16.f;
    }
}

} // namespace world_visuals
