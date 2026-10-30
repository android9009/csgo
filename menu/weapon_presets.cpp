#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "weapon_presets.hpp"
#include "gui.hpp"
#include "hashes.hpp"
#include "weapon_icons.hpp"
#include "../sdk/config_system/config_system.hpp"

#include <algorithm>
#include <cstdio>

namespace {
c_config_system::rage_bot_t& rage_state() {
    return g_cfg->rage_bot;
}

bool preset_active(int slot) {
    return slot > 0 && slot < RAGE_WEAPON_PRESET_MAX &&
        rage_state().m_weapon_config[slot].m_override_general;
}

int preset_category(int slot) {
    if (slot <= 0 || slot >= RAGE_WEAPON_PRESET_MAX)
        return WEAPON_PRESET_CUSTOM;
    const int category = rage_state().m_preset_category[slot];
    return category >= 0 && category < WEAPON_PRESET_CATEGORY_COUNT ? category : WEAPON_PRESET_CUSTOM;
}

const char* preset_name(int slot) {
    if (slot == 0)
        return "Global";
    if (!preset_active(slot))
        return "Global";
    if (!rage_state().m_preset_names[slot].empty())
        return rage_state().m_preset_names[slot].c_str();
    if (rage_state().m_preset_custom[slot] || preset_category(slot) == WEAPON_PRESET_CUSTOM)
        return "Unnamed";
    return weapon_preset_category_names[preset_category(slot)];
}

bool preset_has_weapon(int slot, int weapon) {
    if (!preset_active(slot) || weapon < 0 || weapon >= WEAPON_COUNT)
        return false;
    if (rage_state().m_preset_initialized[slot])
        return rage_state().m_preset_weapons[slot][weapon];
    return weapon_preset_category_has_weapon(preset_category(slot), weapon);
}

int preset_weapon_count(int slot) {
    int count = 0;
    for (int weapon = 0; weapon < WEAPON_COUNT; ++weapon)
        count += preset_has_weapon(slot, weapon) ? 1 : 0;
    return count;
}

int preset_first_weapon(int slot) {
    for (int weapon = 0; weapon < WEAPON_COUNT; ++weapon)
        if (preset_has_weapon(slot, weapon))
            return weapon;
    return -1;
}

void initialize_legacy_preset(int slot) {
    if (!preset_active(slot) || rage_state().m_preset_initialized[slot])
        return;
    rage_state().m_preset_initialized[slot] = true;
    for (int weapon = 0; weapon < WEAPON_COUNT; ++weapon) {
        rage_state().m_preset_weapons[slot][weapon] =
            weapon_preset_category_has_weapon(preset_category(slot), weapon);
    }
}

int create_preset(int category) {
    auto& rage = rage_state();
    for (int slot = 1; slot < RAGE_WEAPON_PRESET_MAX; ++slot) {
        if (rage.m_weapon_config[slot].m_override_general)
            continue;

        rage.m_weapon_config[slot] = c_config_system::weapon_config_t{};
        rage.m_weapon_config[slot].m_override_general = true;
        rage.m_preset_initialized[slot] = true;
        rage.m_preset_custom[slot] = category == WEAPON_PRESET_CUSTOM;
        rage.m_preset_names[slot].clear();
        rage.m_preset_category[slot] = category;
        std::fill_n(rage.m_preset_weapons[slot], WEAPON_COUNT, false);
        for (int weapon = 0; weapon < WEAPON_COUNT; ++weapon) {
            rage.m_preset_weapons[slot][weapon] =
                category != WEAPON_PRESET_CUSTOM && weapon_preset_category_has_weapon(category, weapon);
        }
        return slot;
    }
    return -1;
}

void delete_preset(int slot) {
    if (!preset_active(slot))
        return;
    auto& rage = rage_state();
    rage.m_weapon_config[slot] = c_config_system::weapon_config_t{};
    rage.m_preset_initialized[slot] = false;
    rage.m_preset_custom[slot] = false;
    rage.m_preset_names[slot].clear();
    rage.m_preset_category[slot] = WEAPON_PRESET_CUSTOM;
    std::fill_n(rage.m_preset_weapons[slot], WEAPON_COUNT, false);
    if (rage.m_selected_group == slot)
        rage.m_selected_group = 0;
}

void draw_text_centered(ImDrawList* draw, const ImRect& rect, const char* text, ImU32 color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    draw->AddText(ImVec2(IM_ROUND(rect.GetCenter().x - size.x * 0.5f),
                         IM_ROUND(rect.GetCenter().y - size.y * 0.5f)), color, text);
}

void draw_create_group_popup(const ImGuiID popup_id, const ImVec2& anchor, float scale) {
    bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    const float alpha = gui.popup_fade_alpha(popup_id, open);
    if (!gui.begin_popup_card_at("##weapon_create_group_popup", open, alpha, anchor, ImVec2(0.f, 0.f), 232.f))
        return;

    const float row_h = 32.f * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 create_pos = ImGui::GetCursorScreenPos();
    const bool create_clicked = ImGui::InvisibleButton("##create_custom", ImVec2(width, row_h));
    const bool create_hovered = ImGui::IsItemHovered();
    if (create_hovered)
        draw->AddRectFilled(create_pos, create_pos + ImVec2(width, row_h), gui.frame_inactive.to_im_color(0.65f), 5.f * scale);
    draw->AddText(create_pos + ImVec2(10.f * scale, (row_h - ImGui::GetFontSize()) * 0.5f),
                  gui.text_disabled.to_im_color(), ICON_FA_LAYER_GROUP);
    draw->AddText(create_pos + ImVec2(42.f * scale, (row_h - ImGui::GetFontSize()) * 0.5f),
                  gui.text.to_im_color(), "Create Group");
    if (create_clicked) {
        create_preset(WEAPON_PRESET_CUSTOM);
        ImGui::CloseCurrentPopup();
    }

    ImGui::Separator();
    ImGui::BeginChild("##create_group_list", ImVec2(0.f, 300.f * scale), false);
    draw = ImGui::GetWindowDrawList();
    for (int category = WEAPON_PRESET_AUTOSNIPERS; category <= WEAPON_PRESET_TASER; ++category) {
        ImGui::PushID(category);
        const ImVec2 row_pos = ImGui::GetCursorScreenPos();
        const float row_width = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton("##category", ImVec2(row_width, row_h));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
            draw->AddRectFilled(row_pos, row_pos + ImVec2(row_width, row_h), gui.frame_inactive.to_im_color(0.65f), 5.f * scale);
        weapon_icons::draw(draw, weapon_preset_category_icons[category],
                           row_pos + ImVec2(29.f * scale, row_h * 0.5f), ImVec2(42.f * scale, 19.f * scale),
                           gui.text.to_im_color());
        draw->AddText(row_pos + ImVec2(62.f * scale, (row_h - ImGui::GetFontSize()) * 0.5f),
                      gui.text.to_im_color(), weapon_preset_category_names[category]);
        ImGui::PopID();
        if (clicked) {
            create_preset(category);
            ImGui::CloseCurrentPopup();
            break;
        }
    }
    ImGui::EndChild();
    gui.end_popup_card();
}

void draw_weapon_picker_popup(const ImGuiID popup_id, int& slot, const ImVec2& anchor, float scale) {
    bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    const float alpha = gui.popup_fade_alpha(popup_id, open);
    if (!gui.begin_popup_card_at("##weapon_picker_popup", open, alpha, anchor, ImVec2(0.f, 0.f), 272.f))
        return;

    if (!preset_active(slot)) {
        if (open)
            ImGui::CloseCurrentPopup();
        gui.end_popup_card();
        return;
    }

    const float width = ImGui::GetContentRegionAvail().x;
    const float header_h = 26.f * scale;
    const ImVec2 header_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    static int name_slot = -1;
    static bool was_open = false;
    static char name_buffer[64] = {};
    if (open && (!was_open || name_slot != slot)) {
        std::snprintf(name_buffer, sizeof(name_buffer), "%s", preset_name(slot));
        name_slot = slot;
    }
    was_open = open;
    ImGui::SetCursorScreenPos(header_pos);
    ImGui::InputTextEx("##preset_name", nullptr, name_buffer, static_cast<int>(sizeof(name_buffer)),
                       ImVec2(width - 26.f * scale, header_h), ImGuiInputTextFlags_None, nullptr, nullptr);
    rage_state().m_preset_names[slot] = name_buffer;

    ImGui::SetCursorScreenPos(header_pos + ImVec2(width - 24.f * scale, 0.f));
    const bool close_clicked = ImGui::InvisibleButton("##close_picker", ImVec2(24.f * scale, header_h));
    const ImRect close_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    draw_text_centered(draw, close_rect, ICON_FA_TIMES,
                       ImGui::IsItemHovered() ? IM_COL32(235, 85, 100, 255)
                                              : static_cast<ImU32>(gui.text_disabled.to_im_color()));
    if (close_clicked)
        ImGui::CloseCurrentPopup();
    ImGui::SetCursorScreenPos(header_pos);
    ImGui::Dummy(ImVec2(width, header_h));
    ImGui::Separator();

    ImGui::BeginChild("##weapon_list", ImVec2(0.f, 322.f * scale), false);
    draw = ImGui::GetWindowDrawList();
    const float row_h = 30.f * scale;
    bool stop_list = false;
    for (int weapon_class_index = 0; weapon_class_index < WEAPON_CLASS_COUNT; ++weapon_class_index) {
        if (weapon_class_index != 0)
            ImGui::Dummy(ImVec2(1.f, 4.f * scale));
        ImGui::TextDisabled("%s", weapon_class_names[weapon_class_index]);
        ImGui::Dummy(ImVec2(1.f, 3.f * scale));
        for (int weapon = 0; weapon < WEAPON_COUNT; ++weapon) {
            if (weapon_class[weapon] != weapon_class_index)
                continue;
            ImGui::PushID(weapon);
            const bool added = preset_has_weapon(slot, weapon);
            const ImVec2 row_pos = ImGui::GetCursorScreenPos();
            const float row_width = ImGui::GetContentRegionAvail().x;
            bool clicked = false;
            if (!added)
                clicked = ImGui::InvisibleButton("##add_weapon", ImVec2(row_width, row_h));
            else
                ImGui::Dummy(ImVec2(row_width, row_h));
            const bool hovered = !added && ImGui::IsItemHovered();
            if (hovered)
                draw->AddRectFilled(row_pos, row_pos + ImVec2(row_width, row_h), gui.frame_inactive.to_im_color(0.65f), 5.f * scale);
            const ImU32 color = added ? gui.text_disabled.to_im_color(0.45f) : gui.text.to_im_color();
            weapon_icons::draw(draw, weapon, row_pos + ImVec2(29.f * scale, row_h * 0.5f),
                               ImVec2(42.f * scale, 19.f * scale), color);
            draw->AddText(row_pos + ImVec2(62.f * scale, (row_h - ImGui::GetFontSize()) * 0.5f),
                          color, weapon_names[weapon]);
            if (clicked) {
                initialize_legacy_preset(slot);
                rage_state().m_preset_weapons[slot][weapon] = true;
                stop_list = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            if (clicked)
                break;
        }
        if (stop_list)
            break;
    }
    ImGui::EndChild();
    gui.end_popup_card();
}

void draw_main_popup() {
    const float scale = gui.m_scale;
    const ImVec2 side_anchor = ImGui::GetWindowPos() + ImVec2(ImGui::GetWindowWidth() + 4.f * scale, 0.f);
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 header_pos = ImGui::GetCursorScreenPos();
    const float header_h = 28.f * scale;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* bold = ImGui::GetIO().Fonts->Fonts.Size > 4 ? ImGui::GetIO().Fonts->Fonts[4] : ImGui::GetFont();
    draw->AddText(bold, 16.f * scale, header_pos + ImVec2(2.f * scale, 3.f * scale),
                  gui.text.to_im_color(), "Weapon Presets");

    const ImGuiID create_popup_id = ImGui::GetID("##weapon_create_group_popup");
    ImGui::SetCursorScreenPos(header_pos + ImVec2(width - 26.f * scale, 0.f));
    const bool create_clicked = ImGui::InvisibleButton("##open_create_group", ImVec2(26.f * scale, header_h));
    const ImRect create_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (ImGui::IsItemHovered())
        draw->AddRectFilled(create_rect.Min, create_rect.Max, gui.frame_inactive.to_im_color(0.65f), 5.f * scale);
    draw_text_centered(draw, create_rect, ICON_FA_PLUS,
                       ImGui::IsItemHovered() ? gui.accent_color.to_im_color() : gui.text.to_im_color());
    if (create_clicked)
        ImGui::OpenPopupEx(create_popup_id, ImGuiPopupFlags_None);
    ImGui::SetCursorScreenPos(header_pos);
    ImGui::Dummy(ImVec2(width, header_h));
    ImGui::Separator();

    float list_height = 36.f * scale;
    for (int slot = 1; slot < RAGE_WEAPON_PRESET_MAX; ++slot) {
        if (!preset_active(slot))
            continue;
        const int rows = (preset_weapon_count(slot) + 1 + 4) / 5;
        list_height += (26.f + rows * 26.f + 7.f) * scale;
    }
    list_height = ImMin(list_height, 342.f * scale);

    ImGui::BeginChild("##weapon_preset_list", ImVec2(0.f, list_height), false);
    draw = ImGui::GetWindowDrawList();
    const ImGuiID picker_popup_id = ImGui::GetID("##weapon_picker_popup");
    static int picker_slot = -1;
    static int rename_slot = -1;
    static char rename_buffer[64] = {};
    static bool rename_focus = false;
    int pending_delete = -1;

    {
        const ImVec2 row_pos = ImGui::GetCursorScreenPos();
        const float row_width = ImGui::GetContentRegionAvail().x;
        const float row_h = 32.f * scale;
        const bool clicked = ImGui::InvisibleButton("##global", ImVec2(row_width, row_h));
        const bool hovered = ImGui::IsItemHovered();
        const bool selected = rage_state().m_selected_group == 0;
        if (hovered || selected)
            draw->AddRectFilled(row_pos, row_pos + ImVec2(row_width, row_h),
                                gui.frame_inactive.to_im_color(selected ? 0.9f : 0.55f), 5.f * scale);
        if (selected)
            draw->AddRect(row_pos, row_pos + ImVec2(row_width, row_h), gui.popup_border.to_im_color(), 5.f * scale);
        draw->AddText(row_pos + ImVec2(10.f * scale, (row_h - ImGui::GetFontSize()) * 0.5f),
                      gui.text.to_im_color(), "Global");
        if (clicked)
            rage_state().m_selected_group = 0;
        ImGui::Dummy(ImVec2(1.f, 4.f * scale));
    }

    for (int slot = 1; slot < RAGE_WEAPON_PRESET_MAX; ++slot) {
        if (!preset_active(slot))
            continue;
        ImGui::PushID(slot);
        const int weapon_count = preset_weapon_count(slot);
        const int rows = (weapon_count + 1 + 4) / 5;
        const float block_h = (24.f + rows * 26.f + 3.f) * scale;
        const ImVec2 block_pos = ImGui::GetCursorScreenPos();
        const float block_width = ImGui::GetContentRegionAvail().x;
        const bool selected = rage_state().m_selected_group == slot;
        if (selected) {
            draw->AddRectFilled(block_pos, block_pos + ImVec2(block_width, block_h),
                                gui.frame_inactive.to_im_color(0.55f), 5.f * scale);
            draw->AddRect(block_pos, block_pos + ImVec2(block_width, block_h), gui.popup_border.to_im_color(), 5.f * scale);
        }

        bool control_hovered = false;
        if (rename_slot == slot) {
            ImGui::SetCursorScreenPos(block_pos + ImVec2(0.f, 1.f * scale));
            if (rename_focus) {
                ImGui::SetKeyboardFocusHere();
                rename_focus = false;
            }
            const bool rename_enter = ImGui::InputTextEx("##rename", nullptr, rename_buffer,
                static_cast<int>(sizeof(rename_buffer)), ImVec2(block_width - 25.f * scale, 22.f * scale),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll, nullptr, nullptr);
            control_hovered = ImGui::IsItemHovered();
            rage_state().m_preset_names[slot] = rename_buffer;
            if (rename_enter || ImGui::IsItemDeactivated())
                rename_slot = -1;
        } else {
            const bool select_clicked = ImGui::InvisibleButton("##select", ImVec2(block_width - 25.f * scale, 24.f * scale));
            const bool name_hovered = ImGui::IsItemHovered();
            control_hovered = name_hovered;
            if (name_hovered)
                draw->AddRectFilled(block_pos, block_pos + ImVec2(block_width, 24.f * scale), gui.frame_inactive.to_im_color(0.45f), 5.f * scale);
            draw->PushClipRect(block_pos, block_pos + ImVec2(block_width - 26.f * scale, 24.f * scale), true);
            draw->AddText(block_pos + ImVec2(9.f * scale, (24.f * scale - ImGui::GetFontSize()) * 0.5f),
                          gui.text.to_im_color(), preset_name(slot));
            draw->PopClipRect();
            if (select_clicked) {
                rage_state().m_selected_group = slot;
                rename_slot = slot;
                std::snprintf(rename_buffer, sizeof(rename_buffer), "%s", preset_name(slot));
                rename_focus = true;
            }
        }

        ImGui::SameLine(0.f, 0.f);
        const bool delete_clicked = ImGui::InvisibleButton("##delete", ImVec2(25.f * scale, 24.f * scale));
        const ImRect delete_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const bool delete_hovered = ImGui::IsItemHovered();
        control_hovered = control_hovered || delete_hovered;
        draw_text_centered(draw, delete_rect, ICON_FA_TIMES,
                           delete_hovered ? IM_COL32(235, 85, 100, 255)
                                          : static_cast<ImU32>(gui.text_disabled.to_im_color()));
        if (delete_clicked)
            pending_delete = slot;

        ImGui::Indent(6.f * scale);
        int column = 0;
        for (int weapon = 0; weapon < WEAPON_COUNT; ++weapon) {
            if (!preset_has_weapon(slot, weapon))
                continue;
            if (column != 0)
                ImGui::SameLine(0.f, 4.f * scale);
            ImGui::PushID(weapon);
            const bool remove_clicked = ImGui::InvisibleButton("##remove", ImVec2(38.f * scale, 26.f * scale));
            const ImRect icon_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const bool icon_hovered = ImGui::IsItemHovered();
            control_hovered = control_hovered || icon_hovered;
            draw->PushClipRect(icon_rect.Min, icon_rect.Max, true);
            weapon_icons::draw(draw, weapon, icon_rect.GetCenter(), ImVec2(32.f * scale, 16.f * scale),
                               icon_hovered ? IM_COL32(255, 0, 0, 255)
                                            : static_cast<ImU32>(gui.text.to_im_color()));
            draw->PopClipRect();
            if (remove_clicked) {
                initialize_legacy_preset(slot);
                rage_state().m_preset_weapons[slot][weapon] = false;
            }
            ImGui::PopID();
            column = (column + 1) % 5;
        }

        if (column != 0)
            ImGui::SameLine(0.f, 4.f * scale);
        const bool add_clicked = ImGui::InvisibleButton("##add", ImVec2(28.f * scale, 26.f * scale));
        const ImRect add_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const bool add_hovered = ImGui::IsItemHovered();
        control_hovered = control_hovered || add_hovered;
        if (add_hovered)
            draw->AddRectFilled(add_rect.Min, add_rect.Max, gui.frame_inactive.to_im_color(0.65f), 4.f * scale);
        draw_text_centered(draw, add_rect, ICON_FA_PLUS,
                           add_hovered ? gui.accent_color.to_im_color() : gui.text_disabled.to_im_color());
        if (add_clicked) {
            picker_slot = slot;
            ImGui::OpenPopupEx(picker_popup_id, ImGuiPopupFlags_None);
        }

        const bool card_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            ImGui::IsMouseHoveringRect(block_pos, block_pos + ImVec2(block_width, block_h));
        if (card_hovered && !control_hovered && ImGui::IsMouseClicked(0))
            rage_state().m_selected_group = slot;

        ImGui::Unindent(6.f * scale);
        ImGui::Dummy(ImVec2(1.f, 7.f * scale));
        ImGui::PopID();
    }

    if (pending_delete != -1) {
        delete_preset(pending_delete);
        if (picker_slot == pending_delete)
            picker_slot = -1;
        if (rename_slot == pending_delete) {
            rename_slot = -1;
            rename_focus = false;
        }
    }
    draw_weapon_picker_popup(picker_popup_id, picker_slot, side_anchor, scale);
    ImGui::EndChild();

    draw_create_group_popup(create_popup_id, side_anchor, scale);
}
} // namespace

namespace weapon_presets_ui {
void draw(ImVec2 button_pos) {
    auto& rage = rage_state();
    rage.m_selected_group = ImClamp(rage.m_selected_group, 0, RAGE_WEAPON_PRESET_MAX - 1);
    if (rage.m_selected_group != 0 && !preset_active(rage.m_selected_group))
        rage.m_selected_group = 0;

    const float scale = gui.m_scale;
    const float button_w = 182.f * scale;
    const float button_h = 30.f * scale;
    ImGui::SetCursorScreenPos(button_pos);
    const bool clicked = ImGui::InvisibleButton("##top_weapon_presets", ImVec2(button_w, button_h));
    const bool hovered = ImGui::IsItemHovered();
    static float hover = 0.f;
    hover = ImLerp(hover, hovered ? 1.f : 0.f, ImClamp(ImGui::GetIO().DeltaTime * 15.f, 0.f, 1.f));

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(button_pos, button_pos + ImVec2(button_w, button_h),
                        gui.frame_inactive.to_im_color(0.5f + hover * 0.3f), 6.f * scale);

    const int icon = rage.m_selected_group == 0 ? -1 : preset_first_weapon(rage.m_selected_group);
    float text_x = button_pos.x + 12.f * scale;
    if (icon >= 0) {
        weapon_icons::draw(draw, icon, button_pos + ImVec2(21.f * scale, button_h * 0.5f),
                           ImVec2(28.f * scale, 17.f * scale), gui.text.to_im_color());
        text_x = button_pos.x + 39.f * scale;
    }
    draw->PushClipRect(ImVec2(text_x, button_pos.y),
                       button_pos + ImVec2(button_w - 26.f * scale, button_h), true);
    draw->AddText(ImVec2(IM_ROUND(text_x), IM_ROUND(button_pos.y + (button_h - ImGui::GetFontSize()) * 0.5f)),
                  gui.text.to_im_color(), preset_name(rage.m_selected_group));
    draw->PopClipRect();

    const ImVec2 arrow_size = ImGui::CalcTextSize(ICON_FA_CHEVRON_DOWN);
    draw->AddText(button_pos + ImVec2(button_w - arrow_size.x - 12.f * scale, (button_h - arrow_size.y) * 0.5f),
                  gui.text_disabled.to_im_color(), ICON_FA_CHEVRON_DOWN);

    bool open = false;
    const float alpha = gui.popup_animation("##top_weapon_presets_popup", clicked, open);
    const ImVec2 anchor = button_pos + ImVec2(0.f, 36.f * scale);
    if (gui.begin_popup_card_at("##top_weapon_presets_popup", open, alpha, anchor, ImVec2(0.f, 0.f), 262.f)) {
        draw_main_popup();
        gui.end_popup_card();
    }
}
} // namespace weapon_presets_ui
