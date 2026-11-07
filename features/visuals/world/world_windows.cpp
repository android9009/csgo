#include "world_render.hpp"

#include "../dependencies/source.hpp"
#include "../esp/esp_draw.hpp"
#include "../settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

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
    if (page.watermark_display_avatar) {
        // Steam avatar texture is fetched asynchronously by runtime/steam_avatar;
        // until it lands the slot stays a framed placeholder instead of popping in.
        const ImVec2 center(origin.x - 14.f, origin.y + height * 0.5f);
        draw->AddCircleFilled(center, 11.f, color(page.windows_color, 0.35f), 24);
        draw->AddCircle(center, 11.f, color(page.windows_color, 0.8f), 24, 1.f);
    }
}

} // namespace world_visuals
