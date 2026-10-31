#include "esp.hpp"
#include "imgui_internal.h"

#include "chams.hpp"
#include "settings.hpp"
#include "source.hpp"
#include "../../menu/weapon_icons.hpp"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {
using player_visuals::side_settings;
using visuals_source::line3;
using visuals_source::player_data;
using visuals_source::vector3;

struct rect_f {
    ImVec2 min{};
    ImVec2 max{};
};

struct line2 {
    ImVec2 from;
    ImVec2 to;
};

struct drag_rect {
    int side = 0;
    int item = 0;
    rect_f rect;
};

struct active_drag_t {
    int side = -1;
    int item = -1;
    ImVec2 base_min{};
    ImVec2 start_offset{};
    ImVec2 handle_size{};
    ImVec2 preview_min{};
    ImVec2 preview_max{};
    float scale = 1.f;
};

std::mutex drag_mutex;
std::vector<drag_rect> preview_drag_rects;
active_drag_t active_drag;
std::atomic<int> active_drag_item = -1;

ImU32 color(float r, float g, float b, float a = 1.f) {
    return ImGui::GetColorU32(ImVec4(std::clamp(r, 0.f, 1.f), std::clamp(g, 0.f, 1.f),
        std::clamp(b, 0.f, 1.f), std::clamp(a, 0.f, 1.f)));
}

ImU32 color(const c_color& value, float alpha = 1.f) {
    return color(value.r, value.g, value.b, value.a * alpha);
}

ImVec2 offset_for(const side_settings& settings, int item, float scale) {
    if (item < 0 || item >= player_visuals::item_count)
        return {};
    const auto& offset = settings.offsets[item];
    const float x = std::isfinite(offset.x) ? std::clamp(offset.x, -2048.f, 2048.f) : 0.f;
    const float y = std::isfinite(offset.y) ? std::clamp(offset.y, -2048.f, 2048.f) : 0.f;
    return ImVec2(x * scale, y * scale);
}

void register_drag_rect(int side, int item, const rect_f& rect) {
    std::scoped_lock lock(drag_mutex);
    preview_drag_rects.push_back({ side, item, rect });
}

void drag_handle(ImDrawList* draw, int side, int item, const rect_f& handle,
                 const ImVec2& preview_min, const ImVec2& preview_max, float scale) {
    if (!ImGui::GetCurrentWindow())
        return;
    rect_f usable = handle;
    if (usable.max.x - usable.min.x < 7.f * scale) {
        const float center = (usable.min.x + usable.max.x) * 0.5f;
        usable.min.x = center - 4.f * scale;
        usable.max.x = center + 4.f * scale;
    }
    if (usable.max.y - usable.min.y < 7.f * scale) {
        const float center = (usable.min.y + usable.max.y) * 0.5f;
        usable.min.y = center - 4.f * scale;
        usable.max.y = center + 4.f * scale;
    }
    register_drag_rect(side, item, usable);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 saved_cursor = window->DC.CursorPos;
    const ImVec2 saved_cursor_max = window->DC.CursorMaxPos;
    ImGui::PushID("PlayerEspPreviewDrag");
    ImGui::PushID(side);
    ImGui::PushID(item);
    ImGui::SetCursorScreenPos(usable.min);
    ImGui::InvisibleButton("##move", usable.max - usable.min);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated()) {
        active_drag.side = side;
        active_drag.item = item;
        active_drag.base_min = usable.min - offset_for(player_visuals::settings(side), item, scale);
        active_drag.start_offset = offset_for(player_visuals::settings(side), item, scale) / scale;
        active_drag.handle_size = usable.max - usable.min;
        active_drag.preview_min = preview_min;
        active_drag.preview_max = preview_max;
        active_drag.scale = scale;
        active_drag_item.store(item, std::memory_order_release);
    }
    if (ImGui::IsItemActive() && active_drag.side == side && active_drag.item == item) {
        const auto& io = ImGui::GetIO();
        const ImVec2 delta = (io.MousePos - io.MouseClickedPos[0]) / active_drag.scale;
        ImVec2 desired = active_drag.start_offset + delta;
        ImVec2 handle_min = active_drag.base_min + desired * active_drag.scale;
        const ImVec2 clamped(
            std::clamp(handle_min.x, active_drag.preview_min.x,
                std::max(active_drag.preview_min.x, active_drag.preview_max.x - active_drag.handle_size.x)),
            std::clamp(handle_min.y, active_drag.preview_min.y,
                std::max(active_drag.preview_min.y, active_drag.preview_max.y - active_drag.handle_size.y)));
        desired = (clamped - active_drag.base_min) / active_drag.scale;
        auto& offset = player_visuals::settings(side).offsets[item];
        offset.x = std::clamp(desired.x, -2048.f, 2048.f);
        offset.y = std::clamp(desired.y, -2048.f, 2048.f);
    }
    if (ImGui::IsItemDeactivated() && active_drag.side == side && active_drag.item == item) {
        active_drag.side = active_drag.item = -1;
        active_drag_item.store(-1, std::memory_order_release);
    }
    if (hovered || (active_drag.side == side && active_drag.item == item)) {
        draw->AddRect(usable.min - ImVec2(1.f, 1.f), usable.max + ImVec2(1.f, 1.f),
            color(0.65f, 0.78f, 1.f, 0.9f), 0.f, 0, 1.f);
    }
    ImGui::PopID(3);
    window->DC.CursorPos = saved_cursor;
    window->DC.CursorMaxPos = saved_cursor_max;
}

rect_f draw_text(ImDrawList* draw, ImVec2 center, const std::string& text, ImU32 ink,
                 float scale, int alignment = 0) { // 0 center, 1 left, -1 right
    if (text.empty())
        return {};
    ImFont* font = ImGui::GetFont();
    const float font_size = std::max(8.f, 12.f * scale);
    const ImVec2 size = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, text.c_str());
    ImVec2 top_left = center - ImVec2(size.x * 0.5f, size.y * 0.5f);
    if (alignment > 0)
        top_left.x = center.x;
    else if (alignment < 0)
        top_left.x = center.x - size.x;
    const ImU32 shadow = color(0.f, 0.f, 0.f, 0.85f);
    draw->AddText(font, font_size, top_left + ImVec2(1.f, 1.f), shadow, text.c_str());
    draw->AddText(font, font_size, top_left, ink, text.c_str());
    return { top_left, top_left + size };
}

rect_f draw_bar(ImDrawList* draw, const rect_f& rect, float fraction, ImU32 fill, bool vertical) {
    const ImU32 border = color(0.f, 0.f, 0.f, 0.85f);
    draw->AddRectFilled(rect.min - ImVec2(1.f, 1.f), rect.max + ImVec2(1.f, 1.f), border);
    fraction = std::clamp(fraction, 0.f, 1.f);
    if (vertical) {
        const float y = rect.max.y - (rect.max.y - rect.min.y) * fraction;
        draw->AddRectFilled(ImVec2(rect.min.x, y), rect.max, fill);
    } else {
        const float x = rect.min.x + (rect.max.x - rect.min.x) * fraction;
        draw->AddRectFilled(rect.min, ImVec2(x, rect.max.y), fill);
    }
    return { rect.min - ImVec2(2.f, 2.f), rect.max + ImVec2(2.f, 2.f) };
}

std::string flag_text(int flag, const player_data& data) {
    char buffer[96];
    switch (flag) {
    case player_visuals::flag_unarmored:
        return data.armor_valid && data.armor <= 0 ? "UNARMORED" : "";
    case player_visuals::flag_defuser:
        return data.defuser ? "DEFUSER" : "";
    case player_visuals::flag_blind:
        return data.blind ? "BLIND" : "";
    case player_visuals::flag_scoped:
        return data.scoped ? "SCOPED" : "";
    case player_visuals::flag_reload:
        return data.reloading ? "RELOAD" : "";
    case player_visuals::flag_immunity:
        return data.immunity_valid && data.immunity ? "IMMUNITY" : "";
    case player_visuals::flag_slowed:
        return data.slowed ? "SLOWED" : "";
    case player_visuals::flag_vulnerable:
        return data.vulnerable ? "VULNERABLE" : "";
    case player_visuals::flag_bomb:
        return data.bomb ? "BOMB" : "";
    case player_visuals::flag_hostage:
        return data.hostage ? "HOSTAGE" : "";
    case player_visuals::flag_defuse:
        return data.defusing ? "DEFUSING" : "";
    case player_visuals::flag_pin_pulled:
        return data.pin_pulled ? "PIN PULLED" : "";
    case player_visuals::flag_money:
        if (!data.money_valid)
            return {};
        std::snprintf(buffer, sizeof(buffer), "$%d", data.money);
        return buffer;
    case player_visuals::flag_order_priority:
        if (data.order_priority <= 0)
            return {};
        std::snprintf(buffer, sizeof(buffer), "PRIORITY #%d", data.order_priority);
        return buffer;
    case player_visuals::flag_delay:
        if (data.delay_ms <= 1.f)
            return {};
        std::snprintf(buffer, sizeof(buffer), "DELAY %.0f MS", data.delay_ms);
        return buffer;
    default:
        return {};
    }
}

ImU32 flag_color(int flag) {
    if (flag == player_visuals::flag_money)
        return color(0.45f, 0.95f, 0.45f);
    if (flag == player_visuals::flag_immunity || flag == player_visuals::flag_defuse)
        return color(0.40f, 0.75f, 1.f);
    if (flag == player_visuals::flag_vulnerable || flag == player_visuals::flag_bomb ||
        flag == player_visuals::flag_pin_pulled) {
        return color(1.f, 0.48f, 0.42f);
    }
    return color(0.92f, 0.94f, 1.f);
}

void maybe_drag(ImDrawList* draw, int preview_side, int item, const rect_f& rect,
                const ImVec2& preview_min, const ImVec2& preview_max, float scale) {
    if (preview_side >= 0)
        drag_handle(draw, preview_side, item, rect, preview_min, preview_max, scale);
}

void render_player(ImDrawList* draw, const side_settings& settings, const player_data& data,
                   const rect_f& box, const std::vector<line2>& skeleton,
                   const std::vector<line2>& hitboxes, float scale, int preview_side,
                   const ImVec2& preview_min, const ImVec2& preview_max) {
    const ImVec2 box_center((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f);
    const float box_width = box.max.x - box.min.x;
    const float box_height = box.max.y - box.min.y;
    if (box_width < 2.f || box_height < 2.f)
        return;
    const ImU32 side_color = color(settings.player_color);
    const ImU32 text_color = color(0.94f, 0.96f, 1.f);

    if (settings.main[player_visuals::main_bounding_box]) {
        const ImVec2 offset = offset_for(settings, player_visuals::main_bounding_box, scale);
        rect_f moved{ box.min + offset, box.max + offset };
        draw->AddRect(moved.min - ImVec2(1.f, 1.f), moved.max + ImVec2(1.f, 1.f),
            color(0.f, 0.f, 0.f, 0.8f));
        draw->AddRect(moved.min, moved.max, side_color);
        if (preview_side >= 0) {
            const rect_f handle{ moved.min - ImVec2(3.f, 3.f),
                moved.min + ImVec2(10.f, 10.f) * scale };
            maybe_drag(draw, preview_side, player_visuals::main_bounding_box, handle,
                preview_min, preview_max, scale);
        }
    }

    if (settings.main[player_visuals::main_skeleton]) {
        const ImVec2 offset = offset_for(settings, player_visuals::main_skeleton, scale);
        for (const auto& line : skeleton)
            draw->AddLine(line.from + offset, line.to + offset, side_color, std::max(1.f, scale));
        if (preview_side >= 0) {
            const ImVec2 center = ImVec2(box_center.x, box.min.y + 8.f * scale) + offset;
            maybe_drag(draw, preview_side, player_visuals::main_skeleton,
                { center - ImVec2(5.f, 5.f) * scale, center + ImVec2(5.f, 5.f) * scale },
                preview_min, preview_max, scale);
        }
    }

    if (settings.aimbot[player_visuals::aimbot_hitboxes]) {
        const ImVec2 offset = offset_for(settings,
            player_visuals::aimbot_item_id(player_visuals::aimbot_hitboxes), scale);
        const ImU32 hitbox_color = color(1.f, 0.35f, 0.55f, 0.9f);
        for (const auto& line : hitboxes)
            draw->AddLine(line.from + offset, line.to + offset, hitbox_color, std::max(1.f, scale));
        if (preview_side >= 0) {
            const ImVec2 center = ImVec2(box.max.x - 8.f * scale, box.min.y + 8.f * scale) + offset;
            maybe_drag(draw, preview_side, player_visuals::aimbot_item_id(player_visuals::aimbot_hitboxes),
                { center - ImVec2(5.f, 5.f) * scale, center + ImVec2(5.f, 5.f) * scale },
                preview_min, preview_max, scale);
        }
    }

    if (settings.main[player_visuals::main_name] && !data.name.empty()) {
        std::string name = data.name;
        if (name.size() > 26)
            name = name.substr(0, 23) + "...";
        const ImVec2 center = ImVec2(box_center.x, box.min.y - 9.f * scale) +
            offset_for(settings, player_visuals::main_name, scale);
        const rect_f text = draw_text(draw, center, name, text_color, scale);
        maybe_drag(draw, preview_side, player_visuals::main_name, text, preview_min, preview_max, scale);
    }

    if (settings.main[player_visuals::main_health_bar]) {
        const ImVec2 offset = offset_for(settings, player_visuals::main_health_bar, scale);
        const rect_f bar{ ImVec2(box.min.x - 7.f * scale, box.min.y) + offset,
            ImVec2(box.min.x - 3.f * scale, box.max.y) + offset };
        const float health = std::clamp(data.health / 100.f, 0.f, 1.f);
        const rect_f handle = draw_bar(draw, bar, health,
            color(1.f - health, health, 0.25f), true);
        maybe_drag(draw, preview_side, player_visuals::main_health_bar, handle,
            preview_min, preview_max, scale);
    }

    if (settings.main[player_visuals::main_ammo_bar] && data.clip >= 0 && data.max_clip > 0) {
        const ImVec2 offset = offset_for(settings, player_visuals::main_ammo_bar, scale);
        const rect_f bar{ ImVec2(box.min.x, box.max.y + 3.f * scale) + offset,
            ImVec2(box.max.x, box.max.y + 5.f * scale) + offset };
        const float ammo = std::clamp(static_cast<float>(data.clip) / data.max_clip, 0.f, 1.f);
        const rect_f handle = draw_bar(draw, bar, ammo, color(0.55f, 0.72f, 1.f), false);
        maybe_drag(draw, preview_side, player_visuals::main_ammo_bar, handle,
            preview_min, preview_max, scale);
    }

    if (settings.weapon[player_visuals::weapon_readiness_bar] && data.readiness >= 0.f) {
        const int item = player_visuals::weapon_item_id(player_visuals::weapon_readiness_bar);
        const ImVec2 offset = offset_for(settings, item, scale);
        const float extra = settings.main[player_visuals::main_ammo_bar] ? 5.f : 0.f;
        const rect_f bar{ ImVec2(box.min.x, box.max.y + (6.f + extra) * scale) + offset,
            ImVec2(box.max.x, box.max.y + (8.f + extra) * scale) + offset };
        const rect_f handle = draw_bar(draw, bar, data.readiness, color(0.50f, 1.f, 0.72f), false);
        maybe_drag(draw, preview_side, item, handle, preview_min, preview_max, scale);
    }

    int flag_row = 0;
    for (int flag = 0; flag < player_visuals::flag_count; ++flag) {
        if (!settings.flags[flag])
            continue;
        const std::string text = flag_text(flag, data);
        if (text.empty())
            continue;
        const int item = player_visuals::flag_item_id(flag);
        const ImVec2 center = ImVec2(box.max.x + 5.f * scale,
            box.min.y + (6.f + 12.f * flag_row) * scale) + offset_for(settings, item, scale);
        const rect_f text_rect = draw_text(draw, center, text, flag_color(flag), scale, 1);
        maybe_drag(draw, preview_side, item, text_rect, preview_min, preview_max, scale);
        ++flag_row;
    }

    if (settings.aimbot[player_visuals::aimbot_hit_chance] && data.hit_chance >= 0.f) {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "HC %.0f%%", data.hit_chance);
        const int item = player_visuals::aimbot_item_id(player_visuals::aimbot_hit_chance);
        const ImVec2 center = ImVec2(box.min.x - 5.f * scale, box.min.y + 6.f * scale) +
            offset_for(settings, item, scale);
        const rect_f text = draw_text(draw, center, buffer, color(0.70f, 0.86f, 1.f), scale, -1);
        maybe_drag(draw, preview_side, item, text, preview_min, preview_max, scale);
    }

    const bool bars = settings.main[player_visuals::main_ammo_bar] ||
        (settings.weapon[player_visuals::weapon_readiness_bar] && data.readiness >= 0.f);
    float bottom_y = box.max.y + (bars ? 15.f : 7.f) * scale;
    if (settings.main[player_visuals::main_distance]) {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.1f m", data.distance);
        const ImVec2 center = ImVec2(box_center.x, bottom_y) +
            offset_for(settings, player_visuals::main_distance, scale);
        const rect_f text = draw_text(draw, center, buffer, text_color, scale);
        maybe_drag(draw, preview_side, player_visuals::main_distance, text,
            preview_min, preview_max, scale);
        bottom_y += 13.f * scale;
    }
    if (settings.weapon[player_visuals::weapon_text] && !data.weapon_name.empty()) {
        const int item = player_visuals::weapon_item_id(player_visuals::weapon_text);
        const ImVec2 center = ImVec2(box_center.x, bottom_y) + offset_for(settings, item, scale);
        const rect_f text = draw_text(draw, center, data.weapon_name, text_color, scale);
        maybe_drag(draw, preview_side, item, text, preview_min, preview_max, scale);
        bottom_y += 13.f * scale;
    }
    if (settings.weapon[player_visuals::weapon_icon]) {
        const int item = player_visuals::weapon_item_id(player_visuals::weapon_icon);
        const int atlas = visuals_source::atlas_weapon_id(data.weapon_id);
        if (atlas >= 0) {
            const ImVec2 offset = offset_for(settings, item, scale);
            const ImVec2 center = ImVec2(box_center.x, bottom_y + 3.f * scale) + offset;
            const ImVec2 maximum(std::min(72.f * scale, box_width * 1.8f), 20.f * scale);
            weapon_icons::draw(draw, atlas, center, maximum, text_color);
            maybe_drag(draw, preview_side, item,
                { center - maximum * 0.5f, center + maximum * 0.5f },
                preview_min, preview_max, scale);
            bottom_y += 20.f * scale;
        }
    }

    const auto equipment = [&](int weapon_item, bool available, const char* label, ImU32 ink) {
        if (!settings.weapon[weapon_item] || !available)
            return;
        const int item = player_visuals::weapon_item_id(weapon_item);
        const ImVec2 center = ImVec2(box_center.x, bottom_y) + offset_for(settings, item, scale);
        const rect_f text = draw_text(draw, center, label, ink, scale);
        maybe_drag(draw, preview_side, item, text, preview_min, preview_max, scale);
        bottom_y += 12.f * scale;
    };
    equipment(player_visuals::weapon_bomb, data.bomb, "C4", color(1.f, 0.55f, 0.35f));
    equipment(player_visuals::weapon_defuser, data.defuser, "KIT", color(0.45f, 0.80f, 1.f));
    equipment(player_visuals::weapon_taser, data.taser, "ZEUS", color(1.f, 0.90f, 0.35f));
}

bool world_to_screen(const float matrix[4][4], const vector3& point, const ImVec2& display,
                     ImVec2& output) {
    const float x = matrix[0][0] * point.x + matrix[0][1] * point.y +
        matrix[0][2] * point.z + matrix[0][3];
    const float y = matrix[1][0] * point.x + matrix[1][1] * point.y +
        matrix[1][2] * point.z + matrix[1][3];
    const float w = matrix[3][0] * point.x + matrix[3][1] * point.y +
        matrix[3][2] * point.z + matrix[3][3];
    if (!std::isfinite(w) || w < 0.001f)
        return false;
    const float inverse = 1.f / w;
    output.x = display.x * 0.5f + x * inverse * display.x * 0.5f;
    output.y = display.y * 0.5f - y * inverse * display.y * 0.5f;
    return std::isfinite(output.x) && std::isfinite(output.y);
}

bool screen_box(const visuals_source::frame_data& frame, const player_data& player,
                const ImVec2& display, rect_f& box) {
    bool initialized = false;
    for (int corner = 0; corner < 8; ++corner) {
        const vector3 point{
            player.origin.x + ((corner & 1) ? player.maxs.x : player.mins.x),
            player.origin.y + ((corner & 2) ? player.maxs.y : player.mins.y),
            player.origin.z + ((corner & 4) ? player.maxs.z : player.mins.z)
        };
        ImVec2 screen;
        if (!world_to_screen(frame.view_matrix, point, display, screen))
            return false;
        if (!initialized) {
            box.min = box.max = screen;
            initialized = true;
        } else {
            box.min.x = std::min(box.min.x, screen.x);
            box.min.y = std::min(box.min.y, screen.y);
            box.max.x = std::max(box.max.x, screen.x);
            box.max.y = std::max(box.max.y, screen.y);
        }
    }
    return initialized && box.max.x >= 0.f && box.min.x <= display.x &&
        box.max.y >= 0.f && box.min.y <= display.y;
}

std::vector<line2> project_lines(const visuals_source::frame_data& frame,
                                 const std::vector<line3>& lines, const ImVec2& display) {
    std::vector<line2> projected;
    projected.reserve(lines.size());
    for (const auto& line : lines) {
        ImVec2 from, to;
        if (world_to_screen(frame.view_matrix, line.from, display, from) &&
            world_to_screen(frame.view_matrix, line.to, display, to)) {
            projected.push_back({ from, to });
        }
    }
    return projected;
}

visuals_chams::pass_settings chams_pass(int material, const c_color& primary,
                                        const c_color& secondary) {
    visuals_chams::pass_settings pass;
    pass.material = material;
    pass.color[0] = primary.r;
    pass.color[1] = primary.g;
    pass.color[2] = primary.b;
    pass.color[3] = primary.a;
    pass.secondary[0] = secondary.r;
    pass.secondary[1] = secondary.g;
    pass.secondary[2] = secondary.b;
    pass.secondary[3] = secondary.a;
    return pass;
}

player_data preview_data(int side) {
    player_data data;
    data.index = side + 1;
    data.side = side;
    data.health = 73;
    data.armor = 0; // Unarmored remains visible in the representative preview.
    data.money = 4850;
    data.weapon_id = 7;
    data.weapon_name = "AK-47";
    data.clip = 18;
    data.max_clip = 30;
    data.reserve_ammo = 90;
    data.order_priority = 1;
    data.distance = 12.4f;
    data.readiness = 0.64f;
    data.hit_chance = 87.5f;
    data.delay_ms = 37.f;
    data.armor_valid = true;
    data.money_valid = true;
    data.immunity_valid = true;
    data.helmet = false;
    data.defuser = true;
    data.blind = true;
    data.scoped = true;
    data.reloading = true;
    data.immunity = true;
    data.slowed = true;
    data.vulnerable = true;
    data.bomb = true;
    data.hostage = true;
    data.defusing = true;
    data.pin_pulled = true;
    data.taser = true;
    data.name = side == 0 ? "Enemy Preview" : "Teammate Preview";
    return data;
}

void preview_geometry(const rect_f& box, std::vector<line2>& skeleton, std::vector<line2>& hitboxes) {
    const float x = (box.min.x + box.max.x) * 0.5f;
    const float w = box.max.x - box.min.x;
    const float h = box.max.y - box.min.y;
    const ImVec2 head(x, box.min.y + h * 0.11f);
    const ImVec2 neck(x, box.min.y + h * 0.23f);
    const ImVec2 pelvis(x, box.min.y + h * 0.56f);
    const ImVec2 left_hand(box.min.x, box.min.y + h * 0.47f);
    const ImVec2 right_hand(box.max.x, box.min.y + h * 0.47f);
    const ImVec2 left_foot(x - w * 0.22f, box.max.y);
    const ImVec2 right_foot(x + w * 0.22f, box.max.y);
    skeleton = {
        { head, neck }, { neck, pelvis }, { neck, left_hand }, { neck, right_hand },
        { pelvis, left_foot }, { pelvis, right_foot }
    };
    const auto rectangle = [&](float x0, float y0, float x1, float y1) {
        const ImVec2 a(x0, y0), b(x1, y0), c(x1, y1), d(x0, y1);
        hitboxes.push_back({ a, b });
        hitboxes.push_back({ b, c });
        hitboxes.push_back({ c, d });
        hitboxes.push_back({ d, a });
    };
    rectangle(head.x - w * 0.13f, box.min.y + h * 0.03f,
        head.x + w * 0.13f, box.min.y + h * 0.19f);
    rectangle(x - w * 0.28f, box.min.y + h * 0.23f,
        x + w * 0.28f, box.min.y + h * 0.55f);
    rectangle(x - w * 0.28f, box.min.y + h * 0.56f,
        x - w * 0.04f, box.max.y);
    rectangle(x + w * 0.04f, box.min.y + h * 0.56f,
        x + w * 0.28f, box.max.y);
}
} // namespace

namespace player_esp {
void render() {
    visuals_source::initialize();
    visuals_chams::initialize();
    for (int side = 0; side < player_visuals::side_count; ++side) {
        const auto& settings = player_visuals::settings(side);
        visuals_chams::set_side_passes(side,
            chams_pass(settings.player_material, settings.player_color, settings.player_secondary),
            chams_pass(settings.behind_walls_material, settings.behind_walls_color,
                settings.behind_walls_secondary), settings.custom_sprite.c_str());
    }

    if (!visuals_source::ready())
        return;
    visuals_source::frame_data frame;
    visuals_source::capture(frame);
    if (!frame.in_game || !frame.has_view_matrix || !ImGui::GetCurrentContext())
        return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x < 1.f || display.y < 1.f)
        return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (const auto& player : frame.players) {
        const auto& settings = player_visuals::settings(player.side);
        if (!settings.esp_enabled)
            continue;
        rect_f box;
        if (!screen_box(frame, player, display, box))
            continue;
        const auto skeleton = project_lines(frame, player.skeleton, display);
        const auto hitboxes = project_lines(frame, player.hitboxes, display);
        render_player(draw, settings, player, box, skeleton, hitboxes, 1.f, -1, {}, {});
    }
}

void prepare_preview(int side) {
    if (side != 0 && side != 1)
        return;
    const auto& settings = player_visuals::settings(side);
    visuals_chams::set_preview_passes(side,
        chams_pass(settings.player_material, settings.player_color, settings.player_secondary),
        chams_pass(settings.behind_walls_material, settings.behind_walls_color,
            settings.behind_walls_secondary), settings.custom_sprite.c_str());
}

void draw_preview(const ImVec2& min, const ImVec2& max, int side) {
    {
        std::scoped_lock lock(drag_mutex);
        preview_drag_rects.clear();
    }
    if (active_drag_item.load(std::memory_order_acquire) >= 0 && !ImGui::IsMouseDown(0)) {
        active_drag.side = active_drag.item = -1;
        active_drag_item.store(-1, std::memory_order_release);
    }
    if ((side != 0 && side != 1) || max.x - min.x < 32.f || max.y - min.y < 32.f)
        return;
    const auto& settings = player_visuals::settings(side);
    if (!settings.esp_enabled)
        return;

    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const float box_width = std::min(width * 0.42f, 128.f);
    // Reserve enough space below the model for all bottom-aligned weapon items
    // when every chip is enabled at once.
    const float box_height = height * 0.58f;
    const float center_x = (min.x + max.x) * 0.5f;
    const rect_f box{
        ImVec2(center_x - box_width * 0.5f, min.y + height * 0.12f),
        ImVec2(center_x + box_width * 0.5f, min.y + height * 0.12f + box_height)
    };
    std::vector<line2> skeleton, hitboxes;
    preview_geometry(box, skeleton, hitboxes);
    const player_data data = preview_data(side);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(min, max, true);
    render_player(draw, settings, data, box, skeleton, hitboxes, 1.f, side, min, max);
    draw->PopClipRect();
}

bool preview_drag_hit(const ImVec2& point) {
    if (active_drag_item.load(std::memory_order_acquire) >= 0)
        return true;
    std::scoped_lock lock(drag_mutex);
    for (const auto& drag : preview_drag_rects) {
        if (point.x >= drag.rect.min.x && point.x <= drag.rect.max.x &&
            point.y >= drag.rect.min.y && point.y <= drag.rect.max.y) {
            return true;
        }
    }
    return false;
}
} // namespace player_esp
