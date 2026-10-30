#include "../menu.h"
#include "../../config_vars.h"
using namespace ImGui;

bool c_menu::selectable(const char* label, bool selected, float alpha_pass, ImGuiSelectableFlags flags, const ImVec2& size_arg)
{
	float alpha = this->get_alpha();

	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;

	if ((flags & ImGuiSelectableFlags_SpanAllColumns) && window->DC.CurrentColumns) // FIXME-OPT: Avoid if vertically clipped.
		PushColumnsBackground();

	ImGuiID id = window->GetID(label);
	ImVec2 label_size = CalcTextSize(label, NULL, true);
	ImVec2 size(size_arg.x != 0.0f ? size_arg.x : label_size.x, size_arg.y != 0.0f ? size_arg.y : label_size.y);
	ImVec2 pos = window->DC.CursorPos;
	// pos.y += window->DC.CurrLineTextBaseOffset;
	ImRect bb_inner(pos, pos + size);
	ItemSize(size, 0.0f);

	// Fill horizontal space.
	ImVec2 window_padding = window->WindowPadding;
	float max_x = (flags & ImGuiSelectableFlags_SpanAllColumns) ? GetWindowContentRegionMax().x : GetContentRegionMax().x;
	float w_draw = ImMax(label_size.x, window->Pos.x + max_x - window_padding.x - pos.x);
	ImVec2 size_draw((size_arg.x != 0 && !(flags & ImGuiSelectableFlags_DrawFillAvailWidth)) ? size_arg.x : w_draw, size_arg.y != 0.0f ? size_arg.y : size.y);
	ImRect bb(pos, pos + size_draw);
	if (size_arg.x == 0.0f || (flags & ImGuiSelectableFlags_DrawFillAvailWidth))
		bb.Max.x += window_padding.x;

	// Selectables are tightly packed together so we extend the box to cover spacing between selectable.
	// const float spacing_x = style.ItemSpacing.x;
	// const float spacing_y = style.ItemSpacing.y;
	// const float spacing_L = IM_FLOOR(spacing_x * 0.50f);
	// const float spacing_U = IM_FLOOR(spacing_y * 0.50f);
	// bb.Min.x -= spacing_L;
	// bb.Min.y -= spacing_U;
	// bb.Max.x += (spacing_x - spacing_L);
	// bb.Max.y += (spacing_y - spacing_U);

	bool item_add;
	if (flags & ImGuiSelectableFlags_Disabled)
	{
		ImGuiItemFlags backup_item_flags = window->DC.ItemFlags;
		window->DC.ItemFlags |= ImGuiItemFlags_Disabled | ImGuiItemFlags_NoNavDefaultFocus;
		item_add = ItemAdd(bb, id);
		window->DC.ItemFlags = backup_item_flags;
	}
	else
	{
		item_add = ItemAdd(bb, id);
	}
	if (!item_add)
	{
		if ((flags & ImGuiSelectableFlags_SpanAllColumns) && window->DC.CurrentColumns)
			PopColumnsBackground();
		return false;
	}

	// We use NoHoldingActiveID on menus so user can click and _hold_ on a menu then drag to browse child entries
	ImGuiButtonFlags button_flags = 0;
	if (flags & ImGuiSelectableFlags_NoHoldingActiveID)
		button_flags |= ImGuiButtonFlags_NoHoldingActiveID;
	if (flags & ImGuiSelectableFlags_PressedOnClick)
		button_flags |= ImGuiButtonFlags_PressedOnClick;
	if (flags & ImGuiSelectableFlags_PressedOnRelease)
		button_flags |= ImGuiButtonFlags_PressedOnRelease;
	if (flags & ImGuiSelectableFlags_Disabled)
		button_flags |= ImGuiButtonFlags_Disabled;
	if (flags & ImGuiSelectableFlags_AllowDoubleClick)
		button_flags |= ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnDoubleClick;
	if (flags & ImGuiSelectableFlags_AllowItemOverlap)
		button_flags |= ImGuiButtonFlags_AllowItemOverlap;

	if (flags & ImGuiSelectableFlags_Disabled)
		selected = false;

	const bool was_selected = selected;
	bool hovered, held;
	bool pressed = ButtonBehavior(bb, id, &hovered, &held, button_flags);

	auto& mod = item_animations[CONST_HASH(label) + CONST_HASH("_sel")];

	this->create_animation(mod.hovered_alpha, hovered, 1.f, lerp_animation);
	this->create_animation(mod.alpha, pressed, 0.6f, lerp_animation);

	// Update NavId when clicking or when Hovering (this doesn't happen on most widgets), so navigation can be resumed with gamepad/keyboard
	if (pressed || (hovered && (flags & ImGuiSelectableFlags_SetNavIdOnHover)))
	{
		if (!g.NavDisableMouseHover && g.NavWindow == window && g.NavLayer == window->DC.NavLayerCurrent)
		{
			g.NavDisableHighlight = true;
			SetNavID(id, window->DC.NavLayerCurrent);
		}
	}
	if (pressed)
		MarkItemEdited(id);

	if (flags & ImGuiSelectableFlags_AllowItemOverlap)
		SetItemAllowOverlap();

	// In this branch, Selectable() cannot toggle the selection so this will never trigger.
	if (selected != was_selected) //-V547
		window->DC.LastItemStatusFlags |= ImGuiItemStatusFlags_ToggledSelection;

	// on_directx
	if (held && (flags & ImGuiSelectableFlags_DrawHoveredWhenHeld))
		hovered = true;

	if ((flags & ImGuiSelectableFlags_SpanAllColumns) && window->DC.CurrentColumns)
	{
		PopColumnsBackground();
		bb.Max.x -= (GetContentRegionMax().x - max_x);
	}

	if (selected || mod.hovered_alpha > 0.01f)
	{
		const auto accent = g_cfg.misc.ui_color.base();
		const float fill_alpha = (selected ? 42.f : 20.f * mod.hovered_alpha) * alpha * alpha_pass;
		window->DrawList->AddRectFilled(bb.Min, bb.Max, accent.new_alpha((int)fill_alpha).as_imcolor(), 4.f);
	}

	auto text_clr = selected ? 1.f : 0.58f + 0.42f * mod.hovered_alpha;
	PushStyleColor(ImGuiCol_Text, ImVec4(text_clr, text_clr, text_clr, text_clr * alpha * alpha_pass));
	RenderTextClipped(bb_inner.Min, bb_inner.Max, label, NULL, &label_size, style.SelectableTextAlign, &bb);
	PopStyleColor();

	// Automatically close popups
	if (pressed && (window->Flags & ImGuiWindowFlags_Popup) && !(flags & ImGuiSelectableFlags_DontClosePopups) && !(window->DC.ItemFlags & ImGuiItemFlags_SelectableDontClosePopup))
		CloseCurrentPopup();

	IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.ItemFlags);
	return pressed;
}

bool c_menu::begin_combo(const char* label, const char* preview_value, ImGuiComboFlags flags, int item_cnt)
{
	const float alpha = get_alpha();
	ImGuiContext& g = *GImGui;
	g.NextWindowData.Flags &= ~ImGuiNextWindowDataFlags_HasSizeConstraint;
	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
		return false;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const float w = ImMax(130.f, GetContentRegionAvail().x);
	const ImVec2 label_size = CalcTextSize(label, NULL, true);
	const float row_h = ImMax(19.f, label_size.y) + style.FramePadding.y * 2.f;
	const float combo_w = ImMin(130.f, ImMax(70.f, w - 72.f));
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect bb(pos + ImVec2(w - combo_w, (row_h - 20.f) * 0.5f), pos + ImVec2(w, (row_h + 20.f) * 0.5f));
	const ImRect total_bb(pos, pos + ImVec2(w, row_h));
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id, &bb))
		return false;
	bool hovered, held;
	const bool pressed = ButtonBehavior(bb, id, &hovered, &held);
	bool popup_open = IsPopupOpen(id);
	if ((pressed || g.NavActivateId == id) && !popup_open)
	{
		OpenPopupEx(id);
		popup_open = true;
	}
	auto& mod = item_animations[CONST_HASH(label)];
	create_animation(mod.hovered_alpha, hovered, 1.f, lerp_animation);
	create_animation(mod.alpha, popup_open, 0.5f, lerp_animation);
	mod.active = popup_open;
	const float bright = ImMax(mod.alpha, mod.hovered_alpha);
	draw_list->AddRectFilled(bb.Min, bb.Max, c_color(25, 27, 36, 255.f * alpha).as_imcolor(), 8.f);
	draw_list->AddRect(bb.Min, bb.Max, c_color(30, 33, 44, 255.f * alpha).as_imcolor(), 8.f);
	if (preview_value)
	{
		const auto preview_size = CalcTextSize(preview_value);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.922f, 0.922f, 0.941f, alpha));
		RenderTextClipped(bb.Min + ImVec2(10, 0), bb.Max - ImVec2(24, 0), preview_value, NULL, &preview_size, ImVec2(0.f, 0.5f));
		ImGui::PopStyleColor();
	}
	const auto center = ImVec2(bb.Max.x - 11.f, bb.GetCenter().y);
	const float turn = mod.alpha * IM_PI;
	const auto rotate = [&](float x, float y) {
		return ImVec2(center.x + x * cosf(turn) - y * sinf(turn), center.y + x * sinf(turn) + y * cosf(turn));
	};
	const ImVec2 chevron[3] = { rotate(-3.f, -1.5f), rotate(0.f, 1.5f), rotate(3.f, -1.5f) };
	draw_list->AddPolyline(chevron, 3, c_color(235, 235, 240, 255.f * alpha).as_imcolor(), false, 1.f);
	const int grey = (int)std::lerp(130.f, 235.f, bright);
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(grey / 255.f, grey / 255.f, grey / 255.f, alpha));
	if (label_size.x > 0.f)
		RenderTextClipped(total_bb.Min, ImVec2(bb.Min.x - 8.f, total_bb.Max.y), label, NULL, &label_size, ImVec2(0.f, 0.5f));
	ImGui::PopStyleColor();
	draw_list->AddLine(ImVec2(pos.x, total_bb.Max.y), ImVec2(pos.x + w, total_bb.Max.y), c_color(255, 255, 255, 12.f * alpha).as_imcolor());
	if (!popup_open)
		return false;

	char name[32];
	ImFormatString(name, IM_ARRAYSIZE(name), CXOR("##Combo_%02d"), g.BeginPopupStack.Size);
	SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 4.f));
	const float max_size = std::clamp(calc_max_popup_height(item_cnt), 0.f, 200.f);
	SetNextWindowSize(ImVec2(combo_w, max_size * mod.alpha));
	SetNextWindowBgAlpha(alpha * mod.alpha);
	const ImGuiWindowFlags window_flags = ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
	PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
	PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
	PushStyleColor(ImGuiCol_PopupBg, ImVec4(18.f / 255.f, 20.f / 255.f, 29.f / 255.f, alpha * mod.alpha));
	const bool ret = Begin(name, NULL, window_flags);
	PopStyleColor();
	PopStyleVar(2);
	if (!ret)
	{
		EndPopup();
		return false;
	}
	return true;
}

bool c_menu::combo_wrapper(const char* label, int* current_item, bool (*items_getter)(void*, int, const char**), void* data, int items_count, int popup_max_height_in_items)
{
	ImGuiContext& g = *GImGui;

	// Call the getter to obtain the preview string which is a parameter to BeginCombo()
	const char* preview_value = NULL;
	if (*current_item >= 0 && *current_item < items_count)
		items_getter(data, *current_item, &preview_value);
	else
		preview_value = CXOR("Select");

	auto str = std::string(preview_value);
	if (str.length() > 15)
	{
		str.erase(15, str.length() - 15);
		str.append(CXOR("..."));
	}

	// The old Combo() API exposed "popup_max_height_in_items". The new more general BeginCombo() API doesn't have/need it, but we emulate it here.
	if (popup_max_height_in_items != -1 && !(g.NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSizeConstraint))
		SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, calc_max_popup_height(popup_max_height_in_items)));

	if (!this->begin_combo(label, str.c_str(), ImGuiComboFlags_None, items_count))
		return false;

	auto& mod = item_animations[CONST_HASH(label)];

	// Display items
	// FIXME-OPT: Use clipper (but we need to disable it on the appearing frame to make sure our call to SetItemDefaultFocus() is processed)
	bool value_changed = false;
	for (int i = 0; i < items_count; i++)
	{
		PushID((void*)(intptr_t)i);
		const bool item_selected = (i == *current_item);
		const char* item_text;
		if (!items_getter(data, i, &item_text))
			item_text = XOR("item?").c_str();

		if (this->selectable(item_text, item_selected, mod.alpha))
		{
			value_changed = true;
			*current_item = i;
		}
		/*if (item_selected)
		  SetItemDefaultFocus();*/
		PopID();
	}

	EndCombo();
	return value_changed;
}

bool c_menu::combo(const char* label, int* current_item, const char* const items[], int items_count, int height_in_items)
{
	const bool value_changed = this->combo_wrapper(label, current_item, items_array_getter, (void*)items, items_count, height_in_items);
	return value_changed;
}

bool c_menu::selectable2(const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2& size_arg)
{
	if (this->selectable(label, *p_selected, flags, 1.f, size_arg))
	{
		*p_selected = !*p_selected;
		return true;
	}
	return false;
}

bool c_menu::selectable_flags(const char* label, unsigned int* flags, unsigned int flags_value)
{
	bool v = ((*flags & flags_value) == flags_value);
	bool pressed = this->selectable2(label, &v, ImGuiSelectableFlags_DontClosePopups);
	if (pressed)
	{
		if (v)
			*flags |= flags_value;
		else
			*flags &= ~flags_value;
	}

	return pressed;
}

void c_menu::multi_combo(const char* name, unsigned int& var, std::vector< std::string > elements)
{
	int t = 0;
	auto& items = combo_items[name];
	items.clear();

	int items_cnt = 0;
	for (int i = 0; i < elements.size(); i++)
	{
		if (var & (1 << i))
		{
			if (t++ > 0)
				items += XOR(", ");
			items += elements[i];

			items_cnt++;
		}
	}

	if (items.length() >= 12)
	{
		items.erase(12);
		items.append(XOR("..."));
	}

	if (this->begin_combo(name, items.empty() ? CXOR("Select") : items.c_str(), 0, elements.size()))
	{
		for (int i = 0; i < elements.size(); i++)
			this->selectable_flags(elements[i].c_str(), &var, (1 << i));
		ImGui::EndCombo();
	}
}